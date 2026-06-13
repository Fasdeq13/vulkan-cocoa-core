#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <sys/event.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_port.h>
#include <mach/message.h>
#include <unistd.h>
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

extern "C" id objc_msgSend(id self, SEL op, ...);

struct Point { double x; double y; };

extern "C" {
    typedef struct { void* data; size_t size; int fd; } IOSurfaceObj;
    typedef IOSurfaceObj* IOSurfaceRef;

    IOSurfaceRef IOSurfaceCreate(void* dict) {
        IOSurfaceRef ref = (IOSurfaceRef)malloc(sizeof(IOSurfaceObj));
        char name[32];
        snprintf(name, 32, "/iosurf_%d", getpid());
        ref->fd = shm_open(name, O_CREAT | O_RDWR, 0666);
        ftruncate(ref->fd, 1024 * 1024);
        ref->data = mmap(NULL, 1024 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, ref->fd, 0);
        return ref;
    }
    void* IOSurfaceGetBaseAddress(IOSurfaceRef ref) { return ref ? ref->data : NULL; }

    Point decodeMachMessage(mach_msg_header_t* msg) {
        struct MousePayload { mach_msg_header_t header; double x; double y; };
        MousePayload* m = (MousePayload*)msg;
        return { m->x, m->y };
    }
}

extern void parseAppleMetalMetadata(llvm::Module* M);
extern void transformAppleMemoryBarriers(llvm::Module* M);
extern void transformThreadgroupAddressSpace(llvm::Module* M);
extern void processAppleIntrinsics(llvm::Module* M);
extern void injectBindlessDescriptors(llvm::Module* M, llvm::Instruction* node, int count);
extern void presentNextFrame(VulkanRenderContext* ctx);

uint32_t findMemoryType(VkPhysicalDevice physDev, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    return 0;
}

VulkanRenderContext* GetGlobalVulkanContext() {
    static VulkanRenderContext ctx;
    return &ctx;
}

VulkanRenderContext::VulkanRenderContext() : instance(VK_NULL_HANDLE), physicalDevice(VK_NULL_HANDLE), device(VK_NULL_HANDLE) {}
VulkanRenderContext::~VulkanRenderContext() {
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
}

void VulkanRenderContext::initContext() {
    VkInstanceCreateInfo iCI{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    vkCreateInstance(&iCI, nullptr, &instance);

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(instance, &count, devs.data());
    physicalDevice = devs[0];

    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> props(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, props.data());

    uint32_t gIdx = 0xFFFFFFFF, tIdx = 0xFFFFFFFF;
    for (uint32_t i = 0; i < qCount; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) gIdx = i;
        if (props[i].queueFlags & VK_QUEUE_TRANSFER_BIT) tIdx = i;
    }
    graphicsQueueFamilyIndex = (gIdx != 0xFFFFFFFF) ? gIdx : 0;
    transferQueueFamilyIndex = (tIdx != 0xFFFFFFFF) ? tIdx : graphicsQueueFamilyIndex;

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> qCIs;
    VkDeviceQueueCreateInfo gCI{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    gCI.queueFamilyIndex = graphicsQueueFamilyIndex;
    gCI.queueCount = 1;
    gCI.pQueuePriorities = &priority;
    qCIs.push_back(gCI);

    if (graphicsQueueFamilyIndex != transferQueueFamilyIndex) {
        VkDeviceQueueCreateInfo tCI{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        tCI.queueFamilyIndex = transferQueueFamilyIndex;
        tCI.queueCount = 1;
        tCI.pQueuePriorities = &priority;
        qCIs.push_back(tCI);
    }

    std::vector<const char*> ext = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
        VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
        VK_EXT_METAL_SURFACE_EXTENSION_NAME
    };
    VkDeviceCreateInfo dCI{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dCI.queueCreateInfoCount = static_cast<uint32_t>(qCIs.size());
    dCI.pQueueCreateInfos = qCIs.data();
    dCI.enabledExtensionCount = static_cast<uint32_t>(ext.size());
    dCI.ppEnabledExtensionNames = ext.data();
    vkCreateDevice(physicalDevice, &dCI, nullptr, &device);
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    vkGetDeviceQueue(device, transferQueueFamilyIndex, 0, &transferQueue);
}

void VulkanRenderContext::createSwapchain(VkInstance inst, VkPhysicalDevice phys, VkDevice dev, uint32_t w, uint32_t h) {
    id nsApp = objc_msgSend((id)objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    id window = objc_msgSend(nsApp, sel_registerName("keyWindow"));
    id view = objc_msgSend(window, sel_registerName("contentView"));
    id layer = objc_msgSend(view, sel_registerName("layer"));
    VkMetalSurfaceCreateInfoEXT surfInfo{ VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT };
    surfInfo.pLayer = (CAMetalLayer*)layer;
    VkSurfaceKHR surface;
    vkCreateMetalSurfaceEXT(inst, &surfInfo, nullptr, &surface);
    VkSwapchainCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    createInfo.surface = surface;
    createInfo.minImageCount = 3;
    createInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    createInfo.imageExtent = { w, h };
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    vkCreateSwapchainKHR(dev, &createInfo, nullptr, &swapchain);
}

VkImage VulkanRenderContext::importExternalMemory(int fd, uint32_t w, uint32_t h) {
    VkExternalMemoryImageCreateInfo extICI{ VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
    extICI.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkImageCreateInfo iCI{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    iCI.pNext = &extICI;
    iCI.imageType = VK_IMAGE_TYPE_2D;
    iCI.format = VK_FORMAT_B8G8R8A8_UNORM;
    iCI.extent = { w, h, 1 };
    iCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImage img;
    vkCreateImage(device, &iCI, nullptr, &img);
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, img, &req);
    uint32_t type = findMemoryType(physicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkImportMemoryFdInfoKHR importInfo{ VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR };
    importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    importInfo.fd = fd;
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.pNext = &importInfo;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    VkDeviceMemory mem;
    vkAllocateMemory(device, &ai, nullptr, &mem);
    vkBindImageMemory(device, img, mem, 0);
    return img;
}

VkDeviceMemory VulkanRenderContext::allocateVideoMemory(VkMemoryRequirements req, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    uint32_t type = 0;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((req.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
            type = i; break;
        }
    }
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    VkDeviceMemory mem;
    vkAllocateMemory(device, &ai, nullptr, &mem);
    return mem;
}

static void runDarwinEventLoop(VulkanRenderContext* ctx) {
    int kq = kqueue();
    struct kevent events[2];
    EV_SET(&events[0], 1, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, 16, NULL);
    mach_port_t port;
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port);
    EV_SET(&events[1], port, EVFILT_MACHPORT, EV_ADD | EV_ENABLE, 0, 0, NULL);
    kevent(kq, events, 2, NULL, 0, NULL);
    id nsApp = objc_msgSend((id)objc_getClass("NSApplication"), sel_registerName("sharedApplication"));

    char buffer[1024];
    while (true) {
        struct kevent ev;
        kevent(kq, NULL, 0, &ev, 1, NULL);
        if (ev.filter == EVFILT_MACHPORT) {
            mach_msg_header_t* msg = (mach_msg_header_t*)buffer;
            mach_msg(msg, MACH_RCV_MSG, 0, 1024, port, 0, MACH_PORT_NULL);
            Point p = decodeMachMessage(msg);
            id mouseEvent = objc_msgSend((id)objc_getClass("NSEvent"), sel_registerName("mouseEventWithType:location:modifierFlags:timestamp:windowNumber:context:eventNumber:clickCount:pressure:"), (unsigned long)1, p.x, p.y, (unsigned long)0, 0.0, 0, nullptr, 0, 1, 0.0f);
            if (mouseEvent && nsApp) {
                objc_msgSend(nsApp, sel_registerName("sendEvent:"), mouseEvent);
            }
        }
        else {
            presentNextFrame(ctx);
        }
    }
}

bool AIRToSPIRVCompiler::compileAIRToSPIRV(const std::vector<uint8_t>& airBytecode, std::vector<uint32_t>& outSpirv) {
    if (airBytecode.empty()) return false;
    std::string bin(airBytecode.begin(), airBytecode.end());
    llvm::MemoryBufferRef buf(bin, "AIR_Stream");
    llvm::LLVMContext llvmCtx;
    auto modOrErr = llvm::parseBitcodeFile(buf, llvmCtx);
    if (!modOrErr) return false;
    std::unique_ptr<llvm::Module> M = std::move(modOrErr.get());
    parseAppleMetalMetadata(M.get());
    transformAppleMemoryBarriers(M.get());
    transformThreadgroupAddressSpace(M.get());
    processAppleIntrinsics(M.get());
    SPIRV::TranslatorOpts opts;
    opts.enableAllExtensions();
    std::string errLog;
    std::ostringstream spirvStream(std::ios::binary);
    if (!llvm::writeSpirv(M.get(), opts, spirvStream, errLog)) return false;
    std::string spirvStr = spirvStream.str();
    size_t words = spirvStr.size() / sizeof(uint32_t);
    outSpirv.resize(words);
    std::memcpy(outSpirv.data(), spirvStr.data(), words * sizeof(uint32_t));
    return true;
}

static id sharedApplication_impl(id self, SEL _cmd) { return self; }
static void run_impl(id self, SEL _cmd) {}
static id winInit_func(id self, SEL _cmd, CGRect rect, uint64_t mask, uint64_t backing, bool defer) { return self; }
static id srgb_func(id self, SEL _cmd) { return self; }
static id sysFont_func(id self, SEL _cmd, double size) { return self; }

extern "C" {
    int NSApplicationMain(int argc, const char* argv[]) {
        Class nsObject = objc_getClass("NSObject");
        if (!objc_getClass("NSApplication")) {
            Class c = objc_allocateClassPair(nsObject, "NSApplication", 0);
            if (c) {
                Class meta = object_getClass((id)c);
                class_addMethod(meta, sel_registerName("sharedApplication"), (IMP)sharedApplication_impl, "@:");
                class_addMethod(c, sel_registerName("run"), (IMP)run_impl, "v@:");
                objc_registerClassPair(c);
            }
        }
        if (!objc_getClass("NSWindow")) {
            Class c = objc_allocateClassPair(nsObject, "NSWindow", 0);
            if (c) {
                class_addMethod(c, sel_registerName("initWithContentRect:styleMask:backing:defer:"), (IMP)winInit_func, "@@:{CGRect={CGPoint=dd}{CGSize=dd}}QQB");
                objc_registerClassPair(c);
            }
        }
        if (!objc_getClass("NSColorSpace")) {
            Class c = objc_allocateClassPair(nsObject, "NSColorSpace", 0);
            if (c) {
                class_addMethod(object_getClass((id)c), sel_registerName("sRGBColorSpace"), (IMP)srgb_func, "@:");
                objc_registerClassPair(c);
            }
        }
        if (!objc_getClass("NSFont")) {
            Class c = objc_allocateClassPair(nsObject, "NSFont", 0);
            if (c) {
                class_addMethod(object_getClass((id)c), sel_registerName("systemFontOfSize:"), (IMP)sysFont_func, "@:d");
                objc_registerClassPair(c);
            }
        }
        return 0;
    }
}
