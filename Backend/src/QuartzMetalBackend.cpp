// QuartzMetalBackend.cpp
// ravynOS Quartz/Metal → Vulkan backend
// Target: Darwin XNU kernel (ravynOS transition from FreeBSD)
// Date: 16-06-2025
// IOSurface: Mach port-based, IOKit-compatible (no shm_open stub)



#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <vector>
#include <array>
#include <sstream>
#include <stdexcept>
#include <optional>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <algorithm>

#include <sys/event.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>

#include <mach/mach.h>
#include <mach/mach_port.h>
#include <mach/message.h>
#include <mach/task.h>
#include <mach/vm_map.h>
#include <mach/mach_time.h>
#include <mach/mach_vm.h>
#include <mach/vm_prot.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>
#include <CoreFoundation/CoreFoundation.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>

#include <LLVMSPIRVLib/LLVMSPIRVLib.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/MemoryBuffer.h>

#include <objc/runtime.h>
#include <objc/message.h>

#include "QuartzMetalBackend.h"

#ifdef __x86_64__
extern "C" id objc_msgSend(id, SEL, ...);
extern "C" void objc_msgSend_stret(void*, id, SEL, ...);
#else
extern "C" id objc_msgSend(id, SEL, ...);
#endif

#define RAVYN_MAX_MONITORS          16
#define RAVYN_MAX_FRAMES_IN_FLIGHT  3
#define RAVYN_DEFAULT_WIDTH         1920
#define RAVYN_DEFAULT_HEIGHT        1080
#define RAVYN_IOSURFACE_MAGIC       0x52415653u
#define RAVYN_WS_MSG_MOVE           0x4D565345u
#define RAVYN_WS_MSG_TITLE          0x54544C45u
#define RAVYN_WS_MSG_FOCUS          0x464F4353u
#define RAVYN_WS_MSG_RESIZE         0x52535A45u
#define RAVYN_WS_MSG_SURFACE        0x53524641u

#ifndef __CGGEOMETRY__
struct CGPoint { double x; double y; };
struct CGSize  { double width; double height; };
struct CGRect  { CGPoint origin; CGSize size; };
#endif

typedef struct { double x; double y; } NSPoint;
typedef struct { double width; double height; } NSSize;
typedef struct { NSPoint origin; NSSize size; } NSRect;

static std::mutex g_vulkan_mutex;

enum {
    kIOSurfaceMethodCreate         = 0,
    kIOSurfaceMethodRelease        = 1,
    kIOSurfaceMethodGetBaseAddress = 2,
    kIOSurfaceMethodLock           = 6,
    kIOSurfaceMethodUnlock         = 7,
    kIOSurfaceMethodGetMachPort    = 8,
    kIOSurfaceMethodLookup         = 9,
};

struct IOSurfaceObj {
    mach_port_t       machPort;
    io_connect_t      ioConnect;
    mach_vm_address_t baseAddress;
    mach_vm_size_t    mappedSize;
    uint32_t          width;
    uint32_t          height;
    uint32_t          bytesPerRow;
    uint32_t          pixelFormat;
    uint32_t          magic;
    std::atomic<int>  refCount;
    VkImage           vulkanImage;
    VkDeviceMemory    vulkanMemory;
    VkImageView       vulkanImageView;
    VkImageLayout     currentLayout;

    IOSurfaceObj()
        : machPort(MACH_PORT_NULL)
        , ioConnect(IO_OBJECT_NULL)
        , baseAddress(0)
        , mappedSize(0)
        , width(0), height(0), bytesPerRow(0)
        , pixelFormat(0x42475241u)
        , magic(RAVYN_IOSURFACE_MAGIC)
        , refCount(1)
        , vulkanImage(VK_NULL_HANDLE)
        , vulkanMemory(VK_NULL_HANDLE)
        , vulkanImageView(VK_NULL_HANDLE)
        , currentLayout(VK_IMAGE_LAYOUT_UNDEFINED)
    {}
};

typedef IOSurfaceObj* IOSurfaceRef;

static io_connect_t openIOSurfaceUserClient() {
    CFMutableDictionaryRef matching = IOServiceMatching("IOSurfaceRoot");
    if (!matching) return IO_OBJECT_NULL;
    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, matching);
    if (service == IO_OBJECT_NULL) return IO_OBJECT_NULL;
    io_connect_t conn = IO_OBJECT_NULL;
    kern_return_t kr = IOServiceOpen(service, mach_task_self(), 0, &conn);
    IOObjectRelease(service);
    if (kr != KERN_SUCCESS) return IO_OBJECT_NULL;
    return conn;
}

static mach_vm_address_t mapSurfaceViaIOKit(io_connect_t conn,
                                             uint64_t surfaceId,
                                             mach_vm_size_t size) {
    mach_vm_address_t addr    = 0;
    mach_vm_size_t    outSize = size;
    kern_return_t kr = IOConnectMapMemory64(conn,
                                             (uint32_t)surfaceId,
                                             mach_task_self(),
                                             &addr, &outSize,
                                             kIOMapAnywhere | kIOMapDefaultCache);
    return (kr == KERN_SUCCESS) ? addr : 0;
}

extern "C" {

IOSurfaceRef IOSurfaceCreate(CFDictionaryRef properties) {
    IOSurfaceObj* surf = new (std::nothrow) IOSurfaceObj();
    if (!surf) return nullptr;

    uint32_t w = 0, h = 0, bpp = 4;
    if (properties) {
        CFNumberRef wn = (CFNumberRef)CFDictionaryGetValue(properties, CFSTR("IOSurfaceWidth"));
        CFNumberRef hn = (CFNumberRef)CFDictionaryGetValue(properties, CFSTR("IOSurfaceHeight"));
        CFNumberRef bn = (CFNumberRef)CFDictionaryGetValue(properties, CFSTR("IOSurfaceBytesPerElement"));
        if (wn) CFNumberGetValue(wn, kCFNumberSInt32Type, &w);
        if (hn) CFNumberGetValue(hn, kCFNumberSInt32Type, &h);
        if (bn) CFNumberGetValue(bn, kCFNumberSInt32Type, &bpp);
    }
    if (w == 0) w = RAVYN_DEFAULT_WIDTH;
    if (h == 0) h = RAVYN_DEFAULT_HEIGHT;

    surf->width       = w;
    surf->height      = h;
    surf->bytesPerRow = (w * bpp + 63u) & ~63u;
    surf->mappedSize  = (mach_vm_size_t)surf->bytesPerRow * h;
    surf->pixelFormat = 0x42475241u;
    surf->ioConnect   = openIOSurfaceUserClient();

    if (surf->ioConnect != IO_OBJECT_NULL) {
        struct __attribute__((packed)) CreateParams {
            uint32_t width;
            uint32_t height;
            uint32_t bytesPerRow;
            uint32_t pixelFormat;
            uint32_t flags;
        } params;
        params.width       = w;
        params.height      = h;
        params.bytesPerRow = surf->bytesPerRow;
        params.pixelFormat = surf->pixelFormat;
        params.flags       = 0;

        uint64_t surfaceId = 0;
        uint32_t outCount  = 1;
        kern_return_t kr = IOConnectCallMethod(surf->ioConnect,
                                               kIOSurfaceMethodCreate,
                                               nullptr, 0,
                                               &params, sizeof(params),
                                               &surfaceId, &outCount,
                                               nullptr, nullptr);
        if (kr == KERN_SUCCESS && surfaceId != 0) {
            surf->baseAddress = mapSurfaceViaIOKit(surf->ioConnect, surfaceId, surf->mappedSize);
            if (surf->baseAddress != 0) {
                uint64_t portOut     = 0;
                uint32_t portCount   = 1;
                kern_return_t kr2 = IOConnectCallMethod(surf->ioConnect,
                                                         kIOSurfaceMethodGetMachPort,
                                                         &surfaceId, 1,
                                                         nullptr, 0,
                                                         &portOut, &portCount,
                                                         nullptr, nullptr);
                if (kr2 == KERN_SUCCESS)
                    surf->machPort = (mach_port_t)portOut;
                std::cerr << "[ravynOS IOSurface] IOKit: " << w << "x" << h
                          << " port=" << surf->machPort << "\n";
                return surf;
            }
        }
        IOServiceClose(surf->ioConnect);
        surf->ioConnect = IO_OBJECT_NULL;
    }

    kern_return_t kr = mach_vm_allocate(mach_task_self(),
                                         &surf->baseAddress,
                                         surf->mappedSize,
                                         VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        std::cerr << "[ravynOS IOSurface] mach_vm_allocate: " << mach_error_string(kr) << "\n";
        delete surf;
        return nullptr;
    }

    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &surf->machPort);
    if (kr != KERN_SUCCESS) {
        mach_vm_deallocate(mach_task_self(), surf->baseAddress, surf->mappedSize);
        delete surf;
        return nullptr;
    }
    mach_port_insert_right(mach_task_self(), surf->machPort, surf->machPort, MACH_MSG_TYPE_MAKE_SEND);

    std::cerr << "[ravynOS IOSurface] vm_allocate fallback: " << w << "x" << h
              << " port=" << surf->machPort << "\n";
    return surf;
}

IOSurfaceRef IOSurfaceRetain(IOSurfaceRef ref) {
    if (!ref || ref->magic != RAVYN_IOSURFACE_MAGIC) return nullptr;
    ref->refCount.fetch_add(1, std::memory_order_relaxed);
    return ref;
}

void IOSurfaceRelease(IOSurfaceRef ref) {
    if (!ref || ref->magic != RAVYN_IOSURFACE_MAGIC) return;
    if (ref->refCount.fetch_sub(1, std::memory_order_acq_rel) != 1) return;

    if (ref->ioConnect != IO_OBJECT_NULL) {
        if (ref->baseAddress)
            IOConnectUnmapMemory64(ref->ioConnect, 0, mach_task_self(), ref->baseAddress);
        IOServiceClose(ref->ioConnect);
    } else {
        if (ref->baseAddress)
            mach_vm_deallocate(mach_task_self(), ref->baseAddress, ref->mappedSize);
    }

    if (ref->machPort != MACH_PORT_NULL) {
        mach_port_mod_refs(mach_task_self(), ref->machPort, MACH_PORT_RIGHT_RECEIVE, -1);
        mach_port_deallocate(mach_task_self(), ref->machPort);
    }

    ref->magic = 0;
    delete ref;
}

void*         IOSurfaceGetBaseAddress(IOSurfaceRef r)  { return (!r || r->magic != RAVYN_IOSURFACE_MAGIC) ? nullptr  : (void*)r->baseAddress; }
size_t        IOSurfaceGetAllocSize(IOSurfaceRef r)    { return (!r || r->magic != RAVYN_IOSURFACE_MAGIC) ? 0        : (size_t)r->mappedSize;  }
uint32_t      IOSurfaceGetWidth(IOSurfaceRef r)        { return r ? r->width       : 0;              }
uint32_t      IOSurfaceGetHeight(IOSurfaceRef r)       { return r ? r->height      : 0;              }
uint32_t      IOSurfaceGetBytesPerRow(IOSurfaceRef r)  { return r ? r->bytesPerRow : 0;              }
mach_port_t   IOSurfaceGetMachPort(IOSurfaceRef r)     { return (!r || r->magic != RAVYN_IOSURFACE_MAGIC) ? MACH_PORT_NULL : r->machPort; }

kern_return_t IOSurfaceLock(IOSurfaceRef ref, uint32_t options, uint32_t* seed) {
    if (!ref || ref->magic != RAVYN_IOSURFACE_MAGIC) return KERN_INVALID_ARGUMENT;
    if (ref->ioConnect != IO_OBJECT_NULL) {
        uint64_t args[2] = { (uint64_t)ref->machPort, (uint64_t)options };
        return (kern_return_t)IOConnectCallMethod(ref->ioConnect, kIOSurfaceMethodLock,
                                                   args, 2, nullptr, 0, nullptr, nullptr, nullptr, nullptr);
    }
    __asm__ __volatile__("" ::: "memory");
    if (seed) *seed = 0;
    return KERN_SUCCESS;
}

kern_return_t IOSurfaceUnlock(IOSurfaceRef ref, uint32_t options, uint32_t* seed) {
    if (!ref || ref->magic != RAVYN_IOSURFACE_MAGIC) return KERN_INVALID_ARGUMENT;
    if (ref->ioConnect != IO_OBJECT_NULL) {
        uint64_t args[2] = { (uint64_t)ref->machPort, (uint64_t)options };
        return (kern_return_t)IOConnectCallMethod(ref->ioConnect, kIOSurfaceMethodUnlock,
                                                   args, 2, nullptr, 0, nullptr, nullptr, nullptr, nullptr);
    }
    __asm__ __volatile__("" ::: "memory");
    if (seed) *seed = 0;
    return KERN_SUCCESS;
}

} // extern "C"

struct RavynMsgMove {
    mach_msg_header_t hdr;
    uint32_t msgId;
    double x, y, w, h;
};

struct RavynMsgTitle {
    mach_msg_header_t hdr;
    uint32_t msgId;
    char text[252];
};

struct RavynMsgFocus {
    mach_msg_header_t hdr;
    uint32_t msgId;
    uint32_t focused;
};

struct RavynMsgResize {
    mach_msg_header_t hdr;
    uint32_t msgId;
    double w, h;
};

struct RavynMsgSurface {
    mach_msg_header_t hdr;
    mach_msg_body_t   body;
    mach_msg_port_descriptor_t surfacePort;
    uint32_t msgId;
    uint32_t width;
    uint32_t height;
};

static void ravynSendMsg(mach_port_t port, void* msg, mach_msg_size_t size) {
    if (port == MACH_PORT_NULL) return;
    mach_msg_header_t* hdr  = (mach_msg_header_t*)msg;
    hdr->msgh_bits          = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    hdr->msgh_size          = size;
    hdr->msgh_remote_port   = port;
    hdr->msgh_local_port    = MACH_PORT_NULL;
    hdr->msgh_voucher_port  = MACH_PORT_NULL;
    hdr->msgh_reserved      = 0;
    mach_msg(hdr, MACH_SEND_MSG | MACH_SEND_TIMEOUT, size, 0, MACH_PORT_NULL, 1, MACH_PORT_NULL);
}

static void ravynSendSurfacePort(mach_port_t dest, mach_port_t surfPort,
                                  uint32_t w, uint32_t h) {
    if (dest == MACH_PORT_NULL || surfPort == MACH_PORT_NULL) return;
    RavynMsgSurface msg{};
    msg.hdr.msgh_bits          = MACH_MSGH_BITS_COMPLEX | MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    msg.hdr.msgh_size          = sizeof(msg);
    msg.hdr.msgh_remote_port   = dest;
    msg.hdr.msgh_local_port    = MACH_PORT_NULL;
    msg.hdr.msgh_voucher_port  = MACH_PORT_NULL;
    msg.hdr.msgh_reserved      = 0;
    msg.body.msgh_descriptor_count = 1;
    msg.surfacePort.name        = surfPort;
    msg.surfacePort.disposition = MACH_MSG_TYPE_COPY_SEND;
    msg.surfacePort.type        = MACH_MSG_PORT_DESCRIPTOR;
    msg.msgId  = RAVYN_WS_MSG_SURFACE;
    msg.width  = w;
    msg.height = h;
    mach_msg(&msg.hdr, MACH_SEND_MSG | MACH_SEND_TIMEOUT,
              sizeof(msg), 0, MACH_PORT_NULL, 1, MACH_PORT_NULL);
}

struct MonitorInfo {
    uint32_t width;
    uint32_t height;
    float    refreshRate;
    int32_t  posX;
    int32_t  posY;
    bool     isPrimary;
    char     name[128];
};

static uint32_t    g_monitorCount = 0;
static MonitorInfo g_monitors[RAVYN_MAX_MONITORS];

static void enumerateDisplaysViaDarwin() {
    g_monitorCount = 0;

    id screenClass = (id)objc_getClass("NSScreen");
    if (!screenClass) {
        g_monitors[0] = { RAVYN_DEFAULT_WIDTH, RAVYN_DEFAULT_HEIGHT, 60.0f, 0, 0, true, "Primary" };
        g_monitorCount = 1;
        return;
    }

    id screens = objc_msgSend(screenClass, sel_registerName("screens"));
    if (!screens) {
        g_monitors[0] = { RAVYN_DEFAULT_WIDTH, RAVYN_DEFAULT_HEIGHT, 60.0f, 0, 0, true, "Primary" };
        g_monitorCount = 1;
        return;
    }

    typedef unsigned long NSUInteger;
    NSUInteger count = (NSUInteger)(uintptr_t)objc_msgSend(screens, sel_registerName("count"));
    if (count == 0) count = 1;
    if (count > RAVYN_MAX_MONITORS) count = RAVYN_MAX_MONITORS;

#ifdef __x86_64__
    typedef void (*FrameStretFn)(NSRect*, id, SEL);
    FrameStretFn frameStret = (FrameStretFn)(void*)objc_msgSend_stret;
#endif

    for (NSUInteger i = 0; i < count; ++i) {
        id screen = objc_msgSend(screens, sel_registerName("objectAtIndex:"), i);
        if (!screen) continue;

        NSRect frame{};
#ifdef __x86_64__
        frameStret(&frame, screen, sel_registerName("frame"));
#else
        typedef NSRect (*FrameFn)(id, SEL);
        frame = ((FrameFn)(void*)objc_msgSend)(screen, sel_registerName("frame"));
#endif

        MonitorInfo& m = g_monitors[g_monitorCount];
        m.width      = frame.size.width  > 0.0 ? (uint32_t)frame.size.width  : RAVYN_DEFAULT_WIDTH;
        m.height     = frame.size.height > 0.0 ? (uint32_t)frame.size.height : RAVYN_DEFAULT_HEIGHT;
        m.posX       = (int32_t)frame.origin.x;
        m.posY       = (int32_t)frame.origin.y;
        m.isPrimary  = (i == 0);
        m.refreshRate = 60.0f;

        id desc = objc_msgSend(screen, sel_registerName("deviceDescription"));
        if (desc) {
            id key = objc_msgSend((id)objc_getClass("NSString"),
                                   sel_registerName("stringWithUTF8String:"),
                                   "NSScreenNumber");
            id rateObj = objc_msgSend(desc, sel_registerName("objectForKey:"), key);
            if (rateObj) {
                BOOL resp = (BOOL)(uintptr_t)objc_msgSend(rateObj,
                                sel_registerName("respondsToSelector:"),
                                sel_registerName("doubleValue"));
                if (resp) {
                    double hz = ((double(*)(id,SEL))(void*)objc_msgSend)(rateObj, sel_registerName("doubleValue"));
                    if (hz > 0.0) m.refreshRate = (float)hz;
                }
            }
        }

        snprintf(m.name, sizeof(m.name), "Display%lu_%ux%u@%.0fHz",
                 (unsigned long)i, m.width, m.height, (double)m.refreshRate);
        ++g_monitorCount;
    }

    if (g_monitorCount == 0) {
        g_monitors[0] = { RAVYN_DEFAULT_WIDTH, RAVYN_DEFAULT_HEIGHT, 60.0f, 0, 0, true, "Fallback" };
        g_monitorCount = 1;
    }
}

static bool checkInstanceExtension(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
    for (auto& e : exts)
        if (strcmp(e.extensionName, name) == 0) return true;
    return false;
}

static bool checkDeviceExtension(VkPhysicalDevice pd, const char* name) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, exts.data());
    for (auto& e : exts)
        if (strcmp(e.extensionName, name) == 0) return true;
    return false;
}

static uint32_t findMemoryType(VkPhysicalDevice physDev,
                                uint32_t typeFilter,
                                VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeFilter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if (typeFilter & (1u << i)) return i;
    throw std::runtime_error("[ravynOS] no suitable Vulkan memory type");
}

static VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT sev,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* d, void*) {
    if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << "[Vulkan] " << d->pMessage << "\n";
    return VK_FALSE;
}

static VkPhysicalDevice selectBestPhysicalDevice(VkInstance instance) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) throw std::runtime_error("[ravynOS] no Vulkan physical devices");
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(instance, &count, devs.data());

    VkPhysicalDevice best  = VK_NULL_HANDLE;
    uint32_t     bestScore = 0;
    for (auto& pd : devs) {
        VkPhysicalDeviceProperties     props{};
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceProperties(pd, &props);
        vkGetPhysicalDeviceMemoryProperties(pd, &mp);
        uint32_t score = 0;
        if      (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   score += 10000;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 5000;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)    score += 1000;
        for (uint32_t i = 0; i < mp.memoryHeapCount; ++i)
            if (mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                score += (uint32_t)(mp.memoryHeaps[i].size / (1024u * 1024u));
        score += props.limits.maxImageDimension2D / 256u;
        std::cerr << "[ravynOS GPU] " << props.deviceName << " score=" << score << "\n";
        if (score > bestScore) { bestScore = score; best = pd; }
    }
    VkPhysicalDeviceProperties chosen{};
    vkGetPhysicalDeviceProperties(best, &chosen);
    std::cerr << "[ravynOS] Selected GPU: " << chosen.deviceName << "\n";
    return best;
}

VulkanRenderContext* GetGlobalVulkanContext() {
    static VulkanRenderContext ctx;
    return &ctx;
}

VulkanRenderContext::VulkanRenderContext()
    : instance(VK_NULL_HANDLE)
    , physicalDevice(VK_NULL_HANDLE)
    , device(VK_NULL_HANDLE)
    , graphicsQueue(VK_NULL_HANDLE)
    , transferQueue(VK_NULL_HANDLE)
    , swapchain(VK_NULL_HANDLE)
    , surface(VK_NULL_HANDLE)
    , renderPass(VK_NULL_HANDLE)
    , commandPool(VK_NULL_HANDLE)
    , debugMessenger(VK_NULL_HANDLE)
    , swapchainFormat(VK_FORMAT_UNDEFINED)
    , swapchainExtent{0, 0}
    , graphicsQueueFamilyIndex(0)
    , transferQueueFamilyIndex(0)
    , currentFrame(0)
    , monitorCount(0)
    , needsSwapchainRecreation(false)
{}

VulkanRenderContext::~VulkanRenderContext() {
    std::lock_guard<std::mutex> lock(g_vulkan_mutex);
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        for (auto& f : inFlightFences)
            if (f != VK_NULL_HANDLE) vkDestroyFence(device, f, nullptr);
        for (auto& s : renderFinishedSemaphores)
            if (s != VK_NULL_HANDLE) vkDestroySemaphore(device, s, nullptr);
        for (auto& s : imageAvailableSemaphores)
            if (s != VK_NULL_HANDLE) vkDestroySemaphore(device, s, nullptr);
        if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
        for (auto& fb : framebuffers)
            if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device, fb, nullptr);
        if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
        for (auto& iv : swapchainImageViews)
            if (iv != VK_NULL_HANDLE) vkDestroyImageView(device, iv, nullptr);
        if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
        if (surface   != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (debugMessenger != VK_NULL_HANDLE) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(instance, debugMessenger, nullptr);
    }
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
}

void VulkanRenderContext::initContext() {
    std::lock_guard<std::mutex> lock(g_vulkan_mutex);
    enumerateDisplaysViaDarwin();
    monitorCount = g_monitorCount;

    std::vector<const char*> instanceExts;
    instanceExts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    if (checkInstanceExtension(VK_EXT_METAL_SURFACE_EXTENSION_NAME))
        instanceExts.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
    if (checkInstanceExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        instanceExts.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    if (checkInstanceExtension(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME))
        instanceExts.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
    bool hasDebug = checkInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (hasDebug) instanceExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::vector<const char*> layers;
    {
        uint32_t lc = 0;
        vkEnumerateInstanceLayerProperties(&lc, nullptr);
        std::vector<VkLayerProperties> avail(lc);
        vkEnumerateInstanceLayerProperties(&lc, avail.data());
        for (auto& l : avail)
            if (strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                layers.push_back("VK_LAYER_KHRONOS_validation"); break;
            }
    }

    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName   = "ravynOS QuartzMetal";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "QuartzMetalBackend";
    appInfo.engineVersion      = VK_MAKE_VERSION(2, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    VkInstanceCreateInfo iCI{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    iCI.pApplicationInfo        = &appInfo;
    iCI.enabledExtensionCount   = (uint32_t)instanceExts.size();
    iCI.ppEnabledExtensionNames = instanceExts.data();
    iCI.enabledLayerCount       = (uint32_t)layers.size();
    iCI.ppEnabledLayerNames     = layers.data();

    VkResult r = vkCreateInstance(&iCI, nullptr, &instance);
    if (r != VK_SUCCESS) throw std::runtime_error("[ravynOS] vkCreateInstance failed");

    if (hasDebug) {
        VkDebugUtilsMessengerCreateInfoEXT dbgCI{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        dbgCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgCI.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgCI.pfnUserCallback = vulkanDebugCallback;
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (fn) fn(instance, &dbgCI, nullptr, &debugMessenger);
    }

    physicalDevice = selectBestPhysicalDevice(instance);

    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> qProps(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, qProps.data());

    uint32_t gIdx = UINT32_MAX, tIdx = UINT32_MAX;
    for (uint32_t i = 0; i < qCount; ++i) {
        if ((qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && gIdx == UINT32_MAX) gIdx = i;
        if ((qProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && tIdx == UINT32_MAX) tIdx = i;
    }
    if (gIdx == UINT32_MAX) throw std::runtime_error("[ravynOS] no graphics queue family");
    if (tIdx == UINT32_MAX) tIdx = gIdx;
    graphicsQueueFamilyIndex = gIdx;
    transferQueueFamilyIndex = tIdx;

    std::vector<VkDeviceQueueCreateInfo> qCIs;
    float prio = 1.0f;
    {
        VkDeviceQueueCreateInfo qi{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qi.queueFamilyIndex = gIdx; qi.queueCount = 1; qi.pQueuePriorities = &prio;
        qCIs.push_back(qi);
    }
    if (tIdx != gIdx) {
        VkDeviceQueueCreateInfo qi{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qi.queueFamilyIndex = tIdx; qi.queueCount = 1; qi.pQueuePriorities = &prio;
        qCIs.push_back(qi);
    }

    std::vector<const char*> devExts;
    devExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (checkDeviceExtension(physicalDevice, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME))
        devExts.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
    if (checkDeviceExtension(physicalDevice, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME))
        devExts.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    if (checkDeviceExtension(physicalDevice, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME))
        devExts.push_back(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);

    VkPhysicalDeviceFeatures feats{};
    feats.samplerAnisotropy                = VK_TRUE;
    feats.shaderStorageImageExtendedFormats = VK_TRUE;

    VkDeviceCreateInfo dCI{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dCI.queueCreateInfoCount    = (uint32_t)qCIs.size();
    dCI.pQueueCreateInfos       = qCIs.data();
    dCI.enabledExtensionCount   = (uint32_t)devExts.size();
    dCI.ppEnabledExtensionNames = devExts.data();
    dCI.pEnabledFeatures        = &feats;

    r = vkCreateDevice(physicalDevice, &dCI, nullptr, &device);
    if (r != VK_SUCCESS) throw std::runtime_error("[ravynOS] vkCreateDevice failed");

    vkGetDeviceQueue(device, gIdx, 0, &graphicsQueue);
    vkGetDeviceQueue(device, tIdx, 0, &transferQueue);

    VkCommandPoolCreateInfo cpCI{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpCI.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpCI.queueFamilyIndex = gIdx;
    vkCreateCommandPool(device, &cpCI, nullptr, &commandPool);

    std::cerr << "[ravynOS] Vulkan device ready on Darwin XNU kernel\n";
}

VkSurfaceKHR VulkanRenderContext::createDarwinSurface(VkInstance inst, uintptr_t nsWindowId) {
    id window = (id)nsWindowId;
    id view   = nullptr;
    if (window)
        view = objc_msgSend(window, sel_registerName("contentView"));

    id mlCls = (id)objc_getClass("CAMetalLayer");
    if (!mlCls) throw std::runtime_error("[ravynOS] CAMetalLayer not found");

    id layer = nullptr;
    if (view) {
        layer = objc_msgSend(view, sel_registerName("layer"));
        BOOL isML = (mlCls && layer)
            ? (BOOL)(uintptr_t)objc_msgSend(layer, sel_registerName("isKindOfClass:"), mlCls)
            : NO;
        if (!isML) {
            layer = objc_msgSend(mlCls, sel_registerName("layer"));
            if (layer) {
                objc_msgSend(view, sel_registerName("setLayer:"), layer);
                objc_msgSend(view, sel_registerName("setWantsLayer:"), (BOOL)YES);
            }
        }
    } else {
        layer = objc_msgSend(mlCls, sel_registerName("layer"));
    }
    if (!layer) throw std::runtime_error("[ravynOS] could not obtain CAMetalLayer");

    id nscs = objc_msgSend((id)objc_getClass("NSColorSpace"), sel_registerName("sRGBColorSpace"));
    if (nscs) objc_msgSend(layer, sel_registerName("setColorspace:"), nscs);
    objc_msgSend(layer, sel_registerName("setPixelFormat:"),          (unsigned long)80);
    objc_msgSend(layer, sel_registerName("setFramebufferOnly:"),      (BOOL)YES);
    objc_msgSend(layer, sel_registerName("setMaximumDrawableCount:"), (unsigned long)RAVYN_MAX_FRAMES_IN_FLIGHT);

    auto createFn = (PFN_vkCreateMetalSurfaceEXT)
        vkGetInstanceProcAddr(inst, "vkCreateMetalSurfaceEXT");
    if (!createFn) throw std::runtime_error("[ravynOS] vkCreateMetalSurfaceEXT unavailable");

    VkMetalSurfaceCreateInfoEXT sci{ VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT };
    sci.pLayer = (CAMetalLayer*)layer;

    VkSurfaceKHR surf = VK_NULL_HANDLE;
    if (createFn(inst, &sci, nullptr, &surf) != VK_SUCCESS)
        throw std::runtime_error("[ravynOS] vkCreateMetalSurfaceEXT failed");

    std::cerr << "[ravynOS] Metal surface created from "
              << (window ? "NSWindow" : "detached layer") << "\n";
    return surf;
}

void VulkanRenderContext::destroySwapchainResources() {
    vkDeviceWaitIdle(device);
    if (!commandBuffers.empty()) {
        vkFreeCommandBuffers(device, commandPool,
                              (uint32_t)commandBuffers.size(), commandBuffers.data());
        commandBuffers.clear();
    }
    for (auto& fb : framebuffers)
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device, fb, nullptr);
    framebuffers.clear();
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
    for (auto& iv : swapchainImageViews)
        if (iv != VK_NULL_HANDLE) vkDestroyImageView(device, iv, nullptr);
    swapchainImageViews.clear();
    swapchainImages.clear();
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}

void VulkanRenderContext::createSwapchain(VkInstance inst,
                                           VkPhysicalDevice phys,
                                           VkDevice dev,
                                           uint32_t w, uint32_t h) {
    if (surface == VK_NULL_HANDLE)
        surface = createDarwinSurface(inst, 0);

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    for (auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; break; }
    if (chosen.format == formats[0].format)
        for (auto& f : formats)
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM) { chosen = f; break; }

    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> pmodes(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pmCount, pmodes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto& pm : pmodes)
        if (pm == VK_PRESENT_MODE_MAILBOX_KHR) { presentMode = pm; break; }

    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width  = std::max(caps.minImageExtent.width,  std::min(caps.maxImageExtent.width,  w));
        extent.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, h));
    }
    swapchainExtent = extent;
    swapchainFormat = chosen.format;

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;
    imgCount = std::min(imgCount, (uint32_t)RAVYN_MAX_FRAMES_IN_FLIGHT);

    VkSwapchainKHR oldSwapchain = swapchain;

    VkSwapchainCreateInfoKHR scCI{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    scCI.surface          = surface;
    scCI.minImageCount    = imgCount;
    scCI.imageFormat      = chosen.format;
    scCI.imageColorSpace  = chosen.colorSpace;
    scCI.imageExtent      = extent;
    scCI.imageArrayLayers = 1;
    scCI.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    scCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scCI.preTransform     = caps.currentTransform;
    scCI.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    scCI.presentMode      = presentMode;
    scCI.clipped          = VK_TRUE;
    scCI.oldSwapchain     = oldSwapchain;

    VkResult r = vkCreateSwapchainKHR(dev, &scCI, nullptr, &swapchain);
    if (oldSwapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(dev, oldSwapchain, nullptr);
    if (r != VK_SUCCESS) throw std::runtime_error("[ravynOS] vkCreateSwapchainKHR failed");

    uint32_t sc = 0;
    vkGetSwapchainImagesKHR(dev, swapchain, &sc, nullptr);
    swapchainImages.resize(sc);
    vkGetSwapchainImagesKHR(dev, swapchain, &sc, swapchainImages.data());

    swapchainImageViews.resize(sc);
    for (uint32_t i = 0; i < sc; ++i) {
        VkImageViewCreateInfo ivCI{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        ivCI.image    = swapchainImages[i];
        ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivCI.format   = chosen.format;
        ivCI.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        ivCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(dev, &ivCI, nullptr, &swapchainImageViews[i]);
    }

    VkAttachmentDescription colorAtt{};
    colorAtt.format         = chosen.format;
    colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpCI{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpCI.attachmentCount = 1; rpCI.pAttachments  = &colorAtt;
    rpCI.subpassCount    = 1; rpCI.pSubpasses    = &subpass;
    rpCI.dependencyCount = 1; rpCI.pDependencies = &dep;
    vkCreateRenderPass(dev, &rpCI, nullptr, &renderPass);

    framebuffers.resize(sc);
    for (uint32_t i = 0; i < sc; ++i) {
        VkFramebufferCreateInfo fbCI{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fbCI.renderPass      = renderPass;
        fbCI.attachmentCount = 1;
        fbCI.pAttachments    = &swapchainImageViews[i];
        fbCI.width           = extent.width;
        fbCI.height          = extent.height;
        fbCI.layers          = 1;
        vkCreateFramebuffer(dev, &fbCI, nullptr, &framebuffers[i]);
    }

    commandBuffers.resize(sc);
    VkCommandBufferAllocateInfo cbAI{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbAI.commandPool        = commandPool;
    cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAI.commandBufferCount = (uint32_t)commandBuffers.size();
    vkAllocateCommandBuffers(dev, &cbAI, commandBuffers.data());

    imageAvailableSemaphores.resize(RAVYN_MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    renderFinishedSemaphores.resize(RAVYN_MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    inFlightFences.resize(RAVYN_MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < RAVYN_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (imageAvailableSemaphores[i] == VK_NULL_HANDLE)
            vkCreateSemaphore(dev, &semCI, nullptr, &imageAvailableSemaphores[i]);
        if (renderFinishedSemaphores[i] == VK_NULL_HANDLE)
            vkCreateSemaphore(dev, &semCI, nullptr, &renderFinishedSemaphores[i]);
        if (inFlightFences[i] == VK_NULL_HANDLE)
            vkCreateFence(dev, &fenceCI, nullptr, &inFlightFences[i]);
    }

    needsSwapchainRecreation = false;
    std::cerr << "[ravynOS] Swapchain: " << extent.width << "x" << extent.height
              << " fmt=" << chosen.format
              << " mode=" << (presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX" : "FIFO")
              << " images=" << sc << "\n";
}

void VulkanRenderContext::recreateSwapchain() {
    vkDeviceWaitIdle(device);
    enumerateDisplaysViaDarwin();
    MonitorInfo& mon = g_monitors[0];

    for (auto& fb : framebuffers)
        if (fb != VK_NULL_HANDLE) { vkDestroyFramebuffer(device, fb, nullptr); fb = VK_NULL_HANDLE; }
    if (!commandBuffers.empty()) {
        vkFreeCommandBuffers(device, commandPool,
                              (uint32_t)commandBuffers.size(), commandBuffers.data());
        commandBuffers.clear();
    }
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass, nullptr); renderPass = VK_NULL_HANDLE;
    }
    for (auto& iv : swapchainImageViews)
        if (iv != VK_NULL_HANDLE) { vkDestroyImageView(device, iv, nullptr); iv = VK_NULL_HANDLE; }
    swapchainImageViews.clear();
    swapchainImages.clear();

    createSwapchain(instance, physicalDevice, device, mon.width, mon.height);
    std::cerr << "[ravynOS] Swapchain recreated: " << mon.width << "x" << mon.height << "\n";
}

static void transitionImageLayout(VkDevice dev, VkCommandPool pool, VkQueue queue,
                                   VkImage image,
                                   VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool        = pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cb = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(dev, &ai, &cb);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkPipelineStageFlags srcStage, dstStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    vkCreateFence(dev, &fci, nullptr, &fence);
    vkQueueSubmit(queue, 1, &si, fence);
    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(dev, fence, nullptr);
    vkFreeCommandBuffers(dev, pool, 1, &cb);
}

VkImage VulkanRenderContext::importExternalMemory(int fd, uint32_t w, uint32_t h) {
    VkExternalMemoryImageCreateInfo extICI{ VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
    extICI.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkImageCreateInfo iCI{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    iCI.pNext         = &extICI;
    iCI.imageType     = VK_IMAGE_TYPE_2D;
    iCI.format        = (swapchainFormat != VK_FORMAT_UNDEFINED) ? swapchainFormat : VK_FORMAT_B8G8R8A8_UNORM;
    iCI.extent        = { w, h, 1 };
    iCI.mipLevels     = 1;
    iCI.arrayLayers   = 1;
    iCI.samples       = VK_SAMPLE_COUNT_1_BIT;
    iCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
    iCI.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    iCI.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    iCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage img = VK_NULL_HANDLE;
    if (vkCreateImage(device, &iCI, nullptr, &img) != VK_SUCCESS) return VK_NULL_HANDLE;

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device, img, &req);

    VkImportMemoryFdInfoKHR importFd{ VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR };
    importFd.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    importFd.fd         = fd;

    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.pNext           = &importFd;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(physicalDevice, req.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &ai, nullptr, &mem) != VK_SUCCESS) {
        vkDestroyImage(device, img, nullptr);
        return VK_NULL_HANDLE;
    }
    vkBindImageMemory(device, img, mem, 0);

    transitionImageLayout(device, commandPool, graphicsQueue,
                           img,
                           VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return img;
}

VkDeviceMemory VulkanRenderContext::allocateVideoMemory(VkMemoryRequirements req,
                                                          VkMemoryPropertyFlags props) {
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(physicalDevice, req.memoryTypeBits, props);
    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &ai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("[ravynOS] vkAllocateMemory failed");
    return mem;
}

void VulkanRenderContext::recordAndSubmitFrame(uint32_t imageIndex) {
    VkCommandBuffer cb = commandBuffers[imageIndex];
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkClearValue clearVal{};
    clearVal.color = {{ 0.05f, 0.05f, 0.08f, 1.0f }};

    VkRenderPassBeginInfo rpBI{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBI.renderPass        = renderPass;
    rpBI.framebuffer       = framebuffers[imageIndex];
    rpBI.renderArea.offset = { 0, 0 };
    rpBI.renderArea.extent = swapchainExtent;
    rpBI.clearValueCount   = 1;
    rpBI.pClearValues      = &clearVal;

    vkCmdBeginRenderPass(cb, &rpBI, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &imageAvailableSemaphores[currentFrame];
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &renderFinishedSemaphores[currentFrame];

    vkQueueSubmit(graphicsQueue, 1, &si, inFlightFences[currentFrame]);
}

void VulkanRenderContext::presentFrame(uint32_t imageIndex) {
    VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &renderFinishedSemaphores[currentFrame];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &swapchain;
    pi.pImageIndices      = &imageIndex;

    VkResult r = vkQueuePresentKHR(graphicsQueue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
        needsSwapchainRecreation = true;

    currentFrame = (currentFrame + 1) % RAVYN_MAX_FRAMES_IN_FLIGHT;
}

void presentNextFrame(VulkanRenderContext* ctx) {
    if (!ctx || ctx->device == VK_NULL_HANDLE || ctx->swapchain == VK_NULL_HANDLE) return;

    if (ctx->needsSwapchainRecreation) {
        ctx->recreateSwapchain();
        return;
    }

    vkWaitForFences(ctx->device, 1, &ctx->inFlightFences[ctx->currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult r = vkAcquireNextImageKHR(ctx->device, ctx->swapchain, UINT64_MAX,
                                        ctx->imageAvailableSemaphores[ctx->currentFrame],
                                        VK_NULL_HANDLE, &imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        ctx->recreateSwapchain();
        return;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return;

    vkResetFences(ctx->device, 1, &ctx->inFlightFences[ctx->currentFrame]);
    ctx->recordAndSubmitFrame(imageIndex);
    ctx->presentFrame(imageIndex);
}

extern void parseAppleMetalMetadata(llvm::Module* M);
extern void transformAppleMemoryBarriers(llvm::Module* M);
extern void transformThreadgroupAddressSpace(llvm::Module* M);
extern void processAppleIntrinsics(llvm::Module* M);
extern void injectBindlessDescriptors(llvm::Module* M, llvm::Instruction* node, int count);

bool AIRToSPIRVCompiler::compileAIRToSPIRV(const std::vector<uint8_t>& airBytecode,
                                              std::vector<uint32_t>&       outSpirv) {
    if (airBytecode.empty()) return false;

    std::string bin(airBytecode.begin(), airBytecode.end());
    llvm::MemoryBufferRef bufRef(bin, "AIR_Stream");
    llvm::LLVMContext llvmCtx;

    auto modOrErr = llvm::parseBitcodeFile(bufRef, llvmCtx);
    if (!modOrErr) {
        std::cerr << "[ravynOS AIR->SPIRV] parseBitcodeFile failed\n";
        return false;
    }

    std::unique_ptr<llvm::Module> M = std::move(modOrErr.get());

    parseAppleMetalMetadata(M.get());
    transformAppleMemoryBarriers(M.get());
    transformThreadgroupAddressSpace(M.get());
    processAppleIntrinsics(M.get());

    SPIRV::TranslatorOpts opts;
    opts.enableAllExtensions();
    opts.setDesiredBIsFormat(SPIRV::BIsRepresentation::SPIRVFriendlyIR);
    opts.setDebugInfoEIS(SPIRV::DebugInfoEIS::OpenCL_DebugInfo_100);

    std::string        errLog;
    std::ostringstream spirvStream(std::ios::binary);

    if (!llvm::writeSpirv(M.get(), opts, spirvStream, errLog)) {
        std::cerr << "[ravynOS AIR->SPIRV] " << errLog << "\n";
        return false;
    }

    std::string spirvStr = spirvStream.str();
    if (spirvStr.empty()) return false;

    size_t words = spirvStr.size() / sizeof(uint32_t);
    outSpirv.resize(words);
    std::memcpy(outSpirv.data(), spirvStr.data(), words * sizeof(uint32_t));
    std::cerr << "[ravynOS AIR->SPIRV] OK, " << words << " words\n";
    return true;
}

static void dispatchMouseEvent(id nsApp, NSPoint p, uint32_t nsEventType) {
    if (!nsApp) return;
    typedef id (*MouseEventFn)(id, SEL,
                               unsigned long,
                               NSPoint,
                               unsigned long,
                               double,
                               long,
                               id,
                               long,
                               long,
                               float);
    static SEL sel = sel_registerName(
        "mouseEventWithType:location:modifierFlags:"
        "timestamp:windowNumber:context:eventNumber:clickCount:pressure:");
    auto fn = (MouseEventFn)(void*)objc_msgSend;
    id ev = fn((id)objc_getClass("NSEvent"), sel,
               (unsigned long)nsEventType, p,
               (unsigned long)0, 0.0, 0L, (id)nullptr, 0L, 1L, 1.0f);
    if (ev) objc_msgSend(nsApp, sel_registerName("sendEvent:"), ev);
}

static void dispatchKeyEvent(id nsApp, uint16_t keyCode, bool down) {
    if (!nsApp) return;
    typedef id (*KeyEventFn)(id, SEL,
                              unsigned long,
                              NSPoint,
                              unsigned long,
                              double,
                              long,
                              id,
                              id,
                              id,
                              BOOL,
                              unsigned short);
    static SEL sel = sel_registerName(
        "keyEventWithType:location:modifierFlags:timestamp:"
        "windowNumber:context:characters:charactersIgnoringModifiers:"
        "isARepeat:keyCode:");
    id emptyStr = objc_msgSend((id)objc_getClass("NSString"), sel_registerName("string"));
    NSPoint zero{ 0.0, 0.0 };
    auto fn = (KeyEventFn)(void*)objc_msgSend;
    id ev = fn((id)objc_getClass("NSEvent"), sel,
               (unsigned long)(down ? 10u : 11u), zero,
               (unsigned long)0, 0.0, 0L,
               (id)nullptr, emptyStr, emptyStr,
               (BOOL)NO, (unsigned short)keyCode);
    if (ev) objc_msgSend(nsApp, sel_registerName("sendEvent:"), ev);
}

struct MachMousePayload {
    mach_msg_header_t hdr;
    double   x, y;
    uint32_t type;
    uint32_t _pad;
};

struct MachKeyPayload {
    mach_msg_header_t hdr;
    uint16_t keyCode;
    uint8_t  down;
    uint8_t  _pad[5];
};

static void runDarwinEventLoop(VulkanRenderContext* ctx) {
    int kq = kqueue();
    if (kq < 0) throw std::runtime_error("[ravynOS] kqueue() failed");

    mach_port_t mousePort = MACH_PORT_NULL, keyPort = MACH_PORT_NULL;
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &mousePort);
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &keyPort);

    MonitorInfo& mon0 = g_monitors[0];
    uint64_t targetFrameNs = (uint64_t)(1e9 / (double)(mon0.refreshRate > 0.0f ? mon0.refreshRate : 60.0f));

    struct kevent changes[3];
    EV_SET(&changes[0], 1,         EVFILT_TIMER,    EV_ADD | EV_ENABLE, 0,
           (intptr_t)(1000 / (int)(mon0.refreshRate > 0.0f ? mon0.refreshRate : 60.0f)), nullptr);
    EV_SET(&changes[1], mousePort, EVFILT_MACHPORT, EV_ADD | EV_ENABLE, 0, 0, (void*)(uintptr_t)1);
    EV_SET(&changes[2], keyPort,   EVFILT_MACHPORT, EV_ADD | EV_ENABLE, 0, 0, (void*)(uintptr_t)2);
    kevent(kq, changes, 3, nullptr, 0, nullptr);

    id nsApp = objc_msgSend((id)objc_getClass("NSApplication"),
                             sel_registerName("sharedApplication"));

    mach_timebase_info_data_t tbInfo{};
    mach_timebase_info(&tbInfo);
    uint64_t lastFrameTime = mach_absolute_time();

    alignas(16) char buffer[4096];
    uint64_t frameCount = 0;
    std::cerr << "[ravynOS] Darwin event loop @ " << mon0.refreshRate << " Hz\n";

    while (true) {
        struct kevent ev{};
        struct timespec timeout{ 0, 1000000 };
        int n = kevent(kq, nullptr, 0, &ev, 1, &timeout);

        if (n > 0) {
            if (ev.filter == EVFILT_MACHPORT) {
                memset(buffer, 0, sizeof(buffer));
                mach_msg_header_t* msg = (mach_msg_header_t*)buffer;
                mach_port_t rcv = (mach_port_t)ev.ident;
                kern_return_t kr = mach_msg(msg, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                                             0, sizeof(buffer), rcv, 0, MACH_PORT_NULL);
                if (kr == KERN_SUCCESS) {
                    uintptr_t tag = (uintptr_t)ev.udata;
                    if (tag == 1 && msg->msgh_size >= sizeof(MachMousePayload)) {
                        MachMousePayload* mp = (MachMousePayload*)msg;
                        NSPoint pt{ mp->x, mp->y };
                        dispatchMouseEvent(nsApp, pt, mp->type ? mp->type : 1u);
                    } else if (tag == 2 && msg->msgh_size >= sizeof(MachKeyPayload)) {
                        MachKeyPayload* kp = (MachKeyPayload*)msg;
                        dispatchKeyEvent(nsApp, kp->keyCode, kp->down != 0);
                    }
                }
            } else if (ev.filter == EVFILT_TIMER) {
                if (nsApp) {
                    id runLoopMode = objc_msgSend(
                        (id)objc_getClass("NSString"),
                        sel_registerName("stringWithUTF8String:"),
                        "kCFRunLoopDefaultMode");
                    id event = objc_msgSend(nsApp,
                        sel_registerName("nextEventMatchingMask:untilDate:inMode:dequeue:"),
                        (unsigned long)~0UL, nullptr, runLoopMode, (BOOL)YES);
                    if (event) objc_msgSend(nsApp, sel_registerName("sendEvent:"), event);
                    objc_msgSend(nsApp, sel_registerName("updateWindows"));
                }
                presentNextFrame(ctx);
                ++frameCount;
            }
        } else {
            uint64_t now       = mach_absolute_time();
            uint64_t elapsedNs = (now - lastFrameTime) * tbInfo.numer / tbInfo.denom;
            if (elapsedNs >= targetFrameNs) {
                presentNextFrame(ctx);
                lastFrameTime = now;
                ++frameCount;
            }
        }
    }

    mach_port_deallocate(mach_task_self(), mousePort);
    mach_port_deallocate(mach_task_self(), keyPort);
    close(kq);
}

struct RavynWindowState {
    mach_port_t  machPort;
    double       x, y, w, h;
    id           metalLayer;
    char         title[256];
    bool         isKey;
    bool         wantsLayer;
    IOSurfaceRef surface;
};

static std::unordered_map<uintptr_t, RavynWindowState> g_windowStates;
static std::mutex g_windowMutex;
static id         g_sharedAppInstance = nullptr;

static RavynWindowState* findWindowState(uintptr_t key) {
    std::lock_guard<std::mutex> lk(g_windowMutex);
    auto it = g_windowStates.find(key);
    return (it != g_windowStates.end()) ? &it->second : nullptr;
}

static RavynWindowState& getOrCreateWindowState(id self) {
    uintptr_t key = (uintptr_t)self;

    {
        std::lock_guard<std::mutex> lk(g_windowMutex);
        auto it = g_windowStates.find(key);
        if (it != g_windowStates.end()) return it->second;
    }

    mach_port_t newPort = MACH_PORT_NULL;
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &newPort);
    mach_port_insert_right(mach_task_self(), newPort, newPort, MACH_MSG_TYPE_MAKE_SEND);

    std::lock_guard<std::mutex> lk(g_windowMutex);
    auto it2 = g_windowStates.find(key);
    if (it2 != g_windowStates.end()) {
        if (newPort != MACH_PORT_NULL) {
            mach_port_mod_refs(mach_task_self(), newPort, MACH_PORT_RIGHT_RECEIVE, -1);
            mach_port_deallocate(mach_task_self(), newPort);
        }
        return it2->second;
    }

    RavynWindowState ws{};
    ws.machPort   = newPort;
    ws.x = ws.y   = 0.0;
    ws.w          = (double)RAVYN_DEFAULT_WIDTH;
    ws.h          = (double)RAVYN_DEFAULT_HEIGHT;
    ws.metalLayer = nullptr;
    ws.surface    = nullptr;
    ws.isKey      = false;
    ws.wantsLayer = false;
    strncpy(ws.title, "ravynOS", sizeof(ws.title) - 1);
    ws.title[sizeof(ws.title) - 1] = '\0';
    g_windowStates.emplace(key, std::move(ws));
    return g_windowStates[key];
}

static void allocateWindowSurface(RavynWindowState& ws) {
    if (ws.surface) return;
    if (ws.w <= 0.0 || ws.h <= 0.0) return;

    int32_t iw = (int32_t)ws.w, ih = (int32_t)ws.h, bpe = 4;

    CFNumberRef wn = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &iw);
    CFNumberRef hn = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &ih);
    CFNumberRef bn = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &bpe);

    CFMutableDictionaryRef props = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 4,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);

    CFDictionarySetValue(props, CFSTR("IOSurfaceWidth"),           wn);
    CFDictionarySetValue(props, CFSTR("IOSurfaceHeight"),          hn);
    CFDictionarySetValue(props, CFSTR("IOSurfaceBytesPerElement"), bn);

    CFRelease(wn);
    CFRelease(hn);
    CFRelease(bn);

    ws.surface = IOSurfaceCreate(props);
    CFRelease(props);

    if (ws.surface) {
        std::cerr << "[ravynOS] Window IOSurface: " << iw << "x" << ih
                  << " port=" << IOSurfaceGetMachPort(ws.surface) << "\n";
        if (ws.machPort != MACH_PORT_NULL)
            ravynSendSurfacePort(ws.machPort,
                                  IOSurfaceGetMachPort(ws.surface),
                                  (uint32_t)iw, (uint32_t)ih);
    }
}

static id sharedApplication_impl(id self, SEL) {
    if (!g_sharedAppInstance) {
        g_sharedAppInstance = self;
        std::cerr << "[ravynOS] NSApplication +sharedApplication init\n";
    }
    return g_sharedAppInstance;
}

static void run_impl(id, SEL) {
    VulkanRenderContext* ctx = GetGlobalVulkanContext();
    try {
        ctx->initContext();
        enumerateDisplaysViaDarwin();
        MonitorInfo& mon = g_monitors[0];
        ctx->createSwapchain(ctx->instance, ctx->physicalDevice, ctx->device,
                              mon.width, mon.height);
        runDarwinEventLoop(ctx);
    } catch (std::exception& e) {
        std::cerr << "[ravynOS] Fatal: " << e.what() << "\n";
    }
}

static id winInit_func(id self, SEL _cmd,
                        NSRect rect,
                        unsigned long mask,
                        unsigned long backing,
                        BOOL defer) {
    (void)_cmd; (void)backing; (void)defer;
    RavynWindowState& ws = getOrCreateWindowState(self);
    ws.x = rect.origin.x;
    ws.y = rect.origin.y;
    ws.w = rect.size.width  > 0.0 ? rect.size.width  : (double)RAVYN_DEFAULT_WIDTH;
    ws.h = rect.size.height > 0.0 ? rect.size.height : (double)RAVYN_DEFAULT_HEIGHT;
    allocateWindowSurface(ws);
    std::cerr << "[ravynOS] NSWindow -init: " << ws.w << "x" << ws.h
              << " style=0x" << std::hex << mask << std::dec
              << " port=" << ws.machPort << "\n";
    return self;
}

static void setTitle_func(id self, SEL _cmd, id title) {
    (void)_cmd;
    if (!title) return;
    const char* utf8 = (const char*)objc_msgSend(title, sel_registerName("UTF8String"));
    if (!utf8) return;
    RavynWindowState& ws = getOrCreateWindowState(self);
    strncpy(ws.title, utf8, sizeof(ws.title) - 1);
    ws.title[sizeof(ws.title) - 1] = '\0';
    if (ws.machPort != MACH_PORT_NULL) {
        RavynMsgTitle msg{};
        msg.msgId = RAVYN_WS_MSG_TITLE;
        strncpy(msg.text, ws.title, sizeof(msg.text) - 1);
        ravynSendMsg(ws.machPort, &msg, sizeof(msg));
    }
    std::cerr << "[ravynOS] NSWindow -setTitle: \"" << ws.title << "\"\n";
}

static void makeKey_func(id self, SEL _cmd, id sender) {
    (void)_cmd; (void)sender;
    RavynWindowState& ws = getOrCreateWindowState(self);
    ws.isKey = true;
    if (ws.machPort != MACH_PORT_NULL) {
        RavynMsgFocus msg{};
        msg.msgId = RAVYN_WS_MSG_FOCUS; msg.focused = 1;
        ravynSendMsg(ws.machPort, &msg, sizeof(msg));
    }
    if (g_sharedAppInstance)
        objc_msgSend(g_sharedAppInstance,
                     sel_registerName("activateIgnoringOtherApps:"), (BOOL)YES);
    std::cerr << "[ravynOS] NSWindow -makeKeyAndOrderFront:\n";
}

static void center_func(id self, SEL _cmd) {
    (void)_cmd;
    RavynWindowState& ws = getOrCreateWindowState(self);
    enumerateDisplaysViaDarwin();
    MonitorInfo& pri = g_monitors[0];
    ws.x = ((double)pri.width  - ws.w) * 0.5 + pri.posX;
    ws.y = ((double)pri.height - ws.h) * 0.5 + pri.posY;
    if (ws.machPort != MACH_PORT_NULL) {
        RavynMsgMove msg{};
        msg.msgId = RAVYN_WS_MSG_MOVE;
        msg.x = ws.x; msg.y = ws.y; msg.w = ws.w; msg.h = ws.h;
        ravynSendMsg(ws.machPort, &msg, sizeof(msg));
    }
    std::cerr << "[ravynOS] NSWindow -center: (" << ws.x << "," << ws.y << ")\n";
}

static id contentView_func(id self, SEL _cmd) {
    (void)_cmd;
    RavynWindowState& ws = getOrCreateWindowState(self);
    return ws.metalLayer ? ws.metalLayer : self;
}

static void setFrame_func(id self, SEL _cmd, NSRect frame, BOOL display) {
    (void)_cmd; (void)display;
    RavynWindowState& ws = getOrCreateWindowState(self);
    ws.x = frame.origin.x;
    ws.y = frame.origin.y;
    double nw = frame.size.width  > 0.0 ? frame.size.width  : ws.w;
    double nh = frame.size.height > 0.0 ? frame.size.height : ws.h;
    bool resized = (nw != ws.w || nh != ws.h);
    ws.w = nw; ws.h = nh;
    if (resized && ws.surface) {
        IOSurfaceRelease(ws.surface);
        ws.surface = nullptr;
        allocateWindowSurface(ws);
    }
    if (ws.machPort != MACH_PORT_NULL) {
        RavynMsgResize msg{};
        msg.msgId = RAVYN_WS_MSG_RESIZE; msg.w = ws.w; msg.h = ws.h;
        ravynSendMsg(ws.machPort, &msg, sizeof(msg));
    }
}

static void orderOut_func(id self, SEL _cmd, id sender) {
    (void)_cmd; (void)sender;
    RavynWindowState& ws = getOrCreateWindowState(self);
    ws.isKey = false;
    if (ws.machPort != MACH_PORT_NULL) {
        RavynMsgFocus msg{};
        msg.msgId = RAVYN_WS_MSG_FOCUS; msg.focused = 0;
        ravynSendMsg(ws.machPort, &msg, sizeof(msg));
    }
}

static void setLayer_func(id self, SEL _cmd, id layer) {
    (void)_cmd;
    if (!layer) return;
    RavynWindowState& ws = getOrCreateWindowState(self);
    ws.metalLayer = layer;
    id mlCls = (id)objc_getClass("CAMetalLayer");
    BOOL isML = (mlCls)
        ? (BOOL)(uintptr_t)objc_msgSend(layer, sel_registerName("isKindOfClass:"), mlCls)
        : NO;
    if (isML) {
        id srgb = objc_msgSend((id)objc_getClass("NSColorSpace"), sel_registerName("sRGBColorSpace"));
        if (srgb) objc_msgSend(layer, sel_registerName("setColorspace:"), srgb);
        objc_msgSend(layer, sel_registerName("setPixelFormat:"),     (unsigned long)80);
        objc_msgSend(layer, sel_registerName("setFramebufferOnly:"), (BOOL)NO);
        if (ws.w > 0.0 && ws.h > 0.0) {
            typedef void (*SetSizeFn)(id, SEL, NSSize);
            NSSize dsz{ ws.w, ws.h };
            ((SetSizeFn)(void*)objc_msgSend)(layer, sel_registerName("setDrawableSize:"), dsz);
        }
        std::cerr << "[ravynOS] NSView -setLayer: CAMetalLayer " << ws.w << "x" << ws.h << "\n";
    }
}

static void setWantsLayer_func(id self, SEL _cmd, BOOL v) {
    (void)_cmd;
    RavynWindowState& ws = getOrCreateWindowState(self);
    ws.wantsLayer = (v == YES);
    if (v == YES && !ws.metalLayer) {
        id mlCls = (id)objc_getClass("CAMetalLayer");
        if (mlCls) {
            id layer = objc_msgSend(mlCls, sel_registerName("layer"));
            if (layer) setLayer_func(self, sel_registerName("setLayer:"), layer);
        }
    }
    std::cerr << "[ravynOS] NSView -setWantsLayer: " << (v ? "YES" : "NO") << "\n";
}

static id layer_func(id self, SEL _cmd) {
    (void)_cmd;
    RavynWindowState& ws = getOrCreateWindowState(self);
    if (ws.metalLayer) return ws.metalLayer;
    id mlCls = (id)objc_getClass("CAMetalLayer");
    if (mlCls) {
        id layer = objc_msgSend(mlCls, sel_registerName("layer"));
        if (layer) {
            ws.metalLayer = layer;
            std::cerr << "[ravynOS] NSView -layer: auto-created CAMetalLayer\n";
            return layer;
        }
    }
    return self;
}

static NSRect frame_func(id self, SEL _cmd) {
    (void)_cmd;
    RavynWindowState* ws = findWindowState((uintptr_t)self);
    if (ws) return NSRect{ NSPoint{ ws->x, ws->y }, NSSize{ ws->w, ws->h } };
    return NSRect{ NSPoint{ 0.0, 0.0 }, NSSize{ (double)RAVYN_DEFAULT_WIDTH, (double)RAVYN_DEFAULT_HEIGHT } };
}

static NSRect bounds_func(id self, SEL _cmd) {
    (void)_cmd;
    RavynWindowState* ws = findWindowState((uintptr_t)self);
    if (ws) return NSRect{ NSPoint{ 0.0, 0.0 }, NSSize{ ws->w, ws->h } };
    return NSRect{ NSPoint{ 0.0, 0.0 }, NSSize{ (double)RAVYN_DEFAULT_WIDTH, (double)RAVYN_DEFAULT_HEIGHT } };
}

struct RavynColorSpaceState { uint8_t lut[256]; bool initialized; };
static RavynColorSpaceState g_srgbState{};

static id srgb_func(id self, SEL _cmd) {
    (void)_cmd;
    if (!g_srgbState.initialized) {
        for (int i = 0; i < 256; ++i) {
            double v   = i / 255.0;
            double lin = (v <= 0.04045) ? (v / 12.92) : std::pow((v + 0.055) / 1.055, 2.4);
            g_srgbState.lut[i] = (uint8_t)(lin * 255.0 + 0.5);
        }
        g_srgbState.initialized = true;
    }
    return self;
}

struct RavynFontState { double pointSize; uint8_t weight; char family[64]; };
static std::unordered_map<uintptr_t, RavynFontState> g_fontStates;
static std::mutex g_fontMutex;

static id sysFont_func(id self, SEL _cmd, double size) {
    (void)_cmd;
    if (size <= 0.0) size = 13.0;
    std::lock_guard<std::mutex> lk(g_fontMutex);
    RavynFontState& fs = g_fontStates[(uintptr_t)self];
    fs.pointSize = size;
    fs.weight    = 100;
    strncpy(fs.family, ".AppleSystemUIFont", sizeof(fs.family) - 1);
    fs.family[sizeof(fs.family) - 1] = '\0';
    std::cerr << "[ravynOS] NSFont +systemFontOfSize: " << size << "\n";
    return self;
}

static id boldFont_func(id self, SEL _cmd, double size) {
    (void)_cmd;
    if (size <= 0.0) size = 13.0;
    std::lock_guard<std::mutex> lk(g_fontMutex);
    RavynFontState& fs = g_fontStates[(uintptr_t)self];
    fs.pointSize = size;
    fs.weight    = 700;
    strncpy(fs.family, ".AppleSystemUIFont", sizeof(fs.family) - 1);
    fs.family[sizeof(fs.family) - 1] = '\0';
    return self;
}

static id screens_func(id, SEL _cmd) {
    (void)_cmd;
    enumerateDisplaysViaDarwin();
    id arrCls = (id)objc_getClass("NSMutableArray");
    if (!arrCls) return nullptr;
    id array = objc_msgSend(arrCls, sel_registerName("array"));
    if (!array) return nullptr;
    id valCls = (id)objc_getClass("NSValue");
    if (!valCls) return array;
    for (uint32_t i = 0; i < g_monitorCount; ++i) {
        id obj = objc_msgSend(valCls, sel_registerName("valueWithPointer:"),
                               (const void*)&g_monitors[i]);
        if (obj) objc_msgSend(array, sel_registerName("addObject:"), obj);
        std::cerr << "[ravynOS] NSScreen: [" << i << "] " << g_monitors[i].name << "\n";
    }
    return array;
}

static id mainScreen_func(id, SEL _cmd) {
    (void)_cmd;
    enumerateDisplaysViaDarwin();
    id valCls = (id)objc_getClass("NSValue");
    if (!valCls) return nullptr;
    return objc_msgSend(valCls, sel_registerName("valueWithPointer:"),
                         (const void*)&g_monitors[0]);
}

static NSRect screenFrame_func(id self, SEL _cmd) {
    (void)_cmd;
    for (uint32_t i = 0; i < g_monitorCount; ++i) {
        if ((const void*)&g_monitors[i] == (const void*)self) {
            return NSRect{
                NSPoint{ (double)g_monitors[i].posX, (double)g_monitors[i].posY },
                NSSize{  (double)g_monitors[i].width, (double)g_monitors[i].height }
            };
        }
    }
    return NSRect{ NSPoint{ 0.0, 0.0 },
                   NSSize{ (double)RAVYN_DEFAULT_WIDTH, (double)RAVYN_DEFAULT_HEIGHT } };
}

extern "C" {

int NSApplicationMain(int argc, const char* argv[]) {
    (void)argc; (void)argv;

    Class nsObject = objc_getClass("NSObject");
    if (!nsObject) {
        std::cerr << "[ravynOS] NSObject not found — ObjC runtime unavailable\n";
        return 1;
    }

    auto safeAddClass = [&](const char* name,
                             std::function<void(Class, Class)> setup) {
        if (objc_getClass(name)) return;
        Class c = objc_allocateClassPair(nsObject, name, 0);
        if (!c) return;
        Class meta = object_getClass((id)c);
        setup(c, meta);
        objc_registerClassPair(c);
        std::cerr << "[ravynOS] Registered: " << name << "\n";
    };

    safeAddClass("NSApplication", [](Class c, Class meta) {
        class_addMethod(meta, sel_registerName("sharedApplication"),
                         (IMP)sharedApplication_impl, "@:");
        class_addMethod(c, sel_registerName("run"),
                         (IMP)run_impl, "v@:");
    });

    safeAddClass("NSWindow", [](Class c, Class) {
        class_addMethod(c,
            sel_registerName("initWithContentRect:styleMask:backing:defer:"),
            (IMP)winInit_func,
            "@@:{NSRect={NSPoint=dd}{NSSize=dd}}LLB");
        class_addMethod(c, sel_registerName("setTitle:"),
                         (IMP)setTitle_func, "v@:@");
        class_addMethod(c, sel_registerName("makeKeyAndOrderFront:"),
                         (IMP)makeKey_func, "v@:@");
        class_addMethod(c, sel_registerName("center"),
                         (IMP)center_func, "v@:");
        class_addMethod(c, sel_registerName("contentView"),
                         (IMP)contentView_func, "@@:");
        class_addMethod(c, sel_registerName("setFrame:display:"),
                         (IMP)setFrame_func,
                         "v@:{NSRect={NSPoint=dd}{NSSize=dd}}B");
        class_addMethod(c, sel_registerName("orderOut:"),
                         (IMP)orderOut_func, "v@:@");
        class_addMethod(c, sel_registerName("frame"),
                         (IMP)frame_func,
                         "{NSRect={NSPoint=dd}{NSSize=dd}}@:");
    });

    safeAddClass("NSView", [](Class c, Class) {
        class_addMethod(c, sel_registerName("setLayer:"),
                         (IMP)setLayer_func, "v@:@");
        class_addMethod(c, sel_registerName("setWantsLayer:"),
                         (IMP)setWantsLayer_func, "v@:B");
        class_addMethod(c, sel_registerName("layer"),
                         (IMP)layer_func, "@@:");
        class_addMethod(c, sel_registerName("frame"),
                         (IMP)frame_func,
                         "{NSRect={NSPoint=dd}{NSSize=dd}}@:");
        class_addMethod(c, sel_registerName("bounds"),
                         (IMP)bounds_func,
                         "{NSRect={NSPoint=dd}{NSSize=dd}}@:");
    });

    safeAddClass("NSColorSpace", [](Class, Class meta) {
        class_addMethod(meta, sel_registerName("sRGBColorSpace"),
                         (IMP)srgb_func, "@:");
    });

    safeAddClass("NSFont", [](Class, Class meta) {
        class_addMethod(meta, sel_registerName("systemFontOfSize:"),
                         (IMP)sysFont_func, "@:d");
        class_addMethod(meta, sel_registerName("boldSystemFontOfSize:"),
                         (IMP)boldFont_func, "@:d");
    });

    safeAddClass("NSScreen", [](Class, Class meta) {
        class_addMethod(meta, sel_registerName("screens"),
                         (IMP)screens_func, "@@:");
        class_addMethod(meta, sel_registerName("mainScreen"),
                         (IMP)mainScreen_func, "@@:");
        class_addMethod((Class)class_getSuperclass((Class)meta),
                         sel_registerName("frame"),
                         (IMP)screenFrame_func,
                         "{NSRect={NSPoint=dd}{NSSize=dd}}@:");
    });

    id app = objc_msgSend((id)objc_getClass("NSApplication"),
                           sel_registerName("sharedApplication"));
    if (app) {
        objc_msgSend(app, sel_registerName("run"));
    } else {
        VulkanRenderContext* ctx = GetGlobalVulkanContext();
        try {
            ctx->initContext();
            enumerateDisplaysViaDarwin();
            ctx->createSwapchain(ctx->instance, ctx->physicalDevice, ctx->device,
                                  g_monitors[0].width, g_monitors[0].height);
            runDarwinEventLoop(ctx);
        } catch (std::exception& e) {
            std::cerr << "[ravynOS] Fatal: " << e.what() << "\n";
            return 1;
        }
    }

    return 0;
}

} // extern "C"
