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
#include <sys/event.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_port.h>
#include <mach/message.h>
#include <mach/task.h>
#include <mach/vm_map.h>
#include <mach/mach_time.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>
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

#define RAVYN_MAX_MONITORS 16
#define RAVYN_MAX_FRAMES_IN_FLIGHT 3
#define RAVYN_DEFAULT_WIDTH 1920
#define RAVYN_DEFAULT_HEIGHT 1080

struct Point { double x; double y; };
struct Size { double w; double h; };
struct Rect { Point origin; Size size; };

extern "C" {
    typedef struct { void* data; size_t size; int fd; size_t mapped_size; } IOSurfaceObj;
    typedef IOSurfaceObj* IOSurfaceRef;

    IOSurfaceRef IOSurfaceCreate(void* dict) {
        IOSurfaceRef ref = (IOSurfaceRef)malloc(sizeof(IOSurfaceObj));
        if (!ref) return nullptr;
        char name[64];
        snprintf(name, sizeof(name), "/iosurf_%d_%llu", getpid(), (unsigned long long)mach_absolute_time());
        ref->fd = shm_open(name, O_CREAT | O_RDWR, 0600);
        if (ref->fd < 0) { free(ref); return nullptr; }
        ref->mapped_size = 4096 * 4096 * 4;
        if (ftruncate(ref->fd, ref->mapped_size) < 0) {
            close(ref->fd);
            shm_unlink(name);
            free(ref);
            return nullptr;
        }
        ref->data = mmap(nullptr, ref->mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED, ref->fd, 0);
        if (ref->data == MAP_FAILED) {
            close(ref->fd);
            shm_unlink(name);
            free(ref);
            return nullptr;
        }
        ref->size = ref->mapped_size;
        return ref;
    }

    void IOSurfaceDestroy(IOSurfaceRef ref) {
        if (!ref) return;
        if (ref->data && ref->data != MAP_FAILED)
            munmap(ref->data, ref->mapped_size);
        if (ref->fd >= 0) close(ref->fd);
        free(ref);
    }

    void* IOSurfaceGetBaseAddress(IOSurfaceRef ref) { return ref ? ref->data : nullptr; }
    size_t IOSurfaceGetSize(IOSurfaceRef ref) { return ref ? ref->mapped_size : 0; }

    Point decodeMachMessage(mach_msg_header_t* msg) {
        struct MousePayload { mach_msg_header_t header; double x; double y; };
        if (!msg) return {0.0, 0.0};
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

static std::mutex g_vulkan_mutex;

static VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* userdata)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << "[Vulkan] " << data->pMessage << "\n";
    return VK_FALSE;
}

uint32_t findMemoryType(VkPhysicalDevice physDev, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)))
            return i;
    }
    throw std::runtime_error("ravynOS: no suitable memory type found");
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
    , graphicsQueueFamilyIndex(0)
    , transferQueueFamilyIndex(0)
    , debugMessenger(VK_NULL_HANDLE)
    , currentFrame(0)
    , monitorCount(0)
{}

VulkanRenderContext::~VulkanRenderContext() {
    std::lock_guard<std::mutex> lock(g_vulkan_mutex);
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        for (auto& fence : inFlightFences)
            if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
        for (auto& sem : renderFinishedSemaphores)
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device, sem, nullptr);
        for (auto& sem : imageAvailableSemaphores)
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device, sem, nullptr);
        if (commandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(device, commandPool, nullptr);
        for (auto& fb : framebuffers)
            if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device, fb, nullptr);
        if (renderPass != VK_NULL_HANDLE)
            vkDestroyRenderPass(device, renderPass, nullptr);
        for (auto& view : swapchainImageViews)
            if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
        if (swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(device, swapchain, nullptr);
        if (surface != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (debugMessenger != VK_NULL_HANDLE) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(instance, debugMessenger, nullptr);
    }
    if (instance != VK_NULL_HANDLE)
        vkDestroyInstance(instance, nullptr);
}

struct MonitorInfo {
    uint32_t width;
    uint32_t height;
    float refreshRate;
    int32_t posX;
    int32_t posY;
    bool isPrimary;
    char name[128];
};

static uint32_t g_monitorCount = 0;
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
    NSUInteger count = (NSUInteger)objc_msgSend(screens, sel_registerName("count"));
    if (count == 0) count = 1;
    if (count > RAVYN_MAX_MONITORS) count = RAVYN_MAX_MONITORS;

    for (NSUInteger i = 0; i < count; ++i) {
        id screen = objc_msgSend(screens, sel_registerName("objectAtIndex:"), i);
        if (!screen) continue;

        struct CGRect { struct { double x; double y; } origin; struct { double w; double h; } size; };
        CGRect frame;
        typedef CGRect (*FrameFn)(id, SEL);
        FrameFn frameFn = (FrameFn)objc_msgSend;
        frame = frameFn(screen, sel_registerName("frame"));

        MonitorInfo& m = g_monitors[g_monitorCount];
        m.width  = (uint32_t)frame.size.w;
        m.height = (uint32_t)frame.size.h;
        m.posX   = (int32_t)frame.origin.x;
        m.posY   = (int32_t)frame.origin.y;
        m.isPrimary = (i == 0);
        m.refreshRate = 60.0f;

        id desc = objc_msgSend(screen, sel_registerName("deviceDescription"));
        if (desc) {
            id refreshNum = objc_msgSend(desc,
                sel_registerName("objectForKey:"),
                objc_msgSend((id)objc_getClass("NSString"),
                    sel_registerName("stringWithUTF8String:"),
                    "NSScreenNumber"));
            (void)refreshNum;
        }

        snprintf(m.name, sizeof(m.name), "Display%lu_%ux%u", (unsigned long)i, m.width, m.height);
        if (m.width == 0) m.width = RAVYN_DEFAULT_WIDTH;
        if (m.height == 0) m.height = RAVYN_DEFAULT_HEIGHT;
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

static VkPhysicalDevice selectBestPhysicalDevice(VkInstance instance) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) throw std::runtime_error("ravynOS: no Vulkan physical devices");
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(instance, &count, devs.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    uint32_t bestScore = 0;

    for (auto& pd : devs) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(pd, &props);
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(pd, &memProps);

        uint32_t score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 10000;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 5000;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) score += 1000;

        for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
            if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                score += (uint32_t)(memProps.memoryHeaps[i].size / (1024 * 1024));
        }

        score += props.limits.maxImageDimension2D / 256;

        std::cerr << "[ravynOS GPU] " << props.deviceName
                  << " score=" << score
                  << " type=" << props.deviceType << "\n";

        if (score > bestScore) {
            bestScore = score;
            best = pd;
        }
    }

    VkPhysicalDeviceProperties chosen{};
    vkGetPhysicalDeviceProperties(best, &chosen);
    std::cerr << "[ravynOS] Selected GPU: " << chosen.deviceName << "\n";
    return best;
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

    bool hasDebug = checkInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (hasDebug) instanceExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::vector<const char*> layers;
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availLayers.data());
    for (auto& l : availLayers) {
        if (strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            break;
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
    if (r != VK_SUCCESS) throw std::runtime_error("ravynOS: vkCreateInstance failed");

    if (hasDebug) {
        VkDebugUtilsMessengerCreateInfoEXT dbgCI{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        dbgCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                              | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgCI.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                              | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                              | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
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

    uint32_t gIdx = UINT32_MAX, tIdx = UINT32_MAX, cIdx = UINT32_MAX;
    for (uint32_t i = 0; i < qCount; ++i) {
        if ((qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && gIdx == UINT32_MAX) gIdx = i;
        if ((qProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && tIdx == UINT32_MAX) tIdx = i;
        if ((qProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && cIdx == UINT32_MAX) cIdx = i;
    }
    graphicsQueueFamilyIndex = (gIdx != UINT32_MAX) ? gIdx : 0;
    transferQueueFamilyIndex = (tIdx != UINT32_MAX) ? tIdx : graphicsQueueFamilyIndex;
    uint32_t computeQueueFamilyIndex = (cIdx != UINT32_MAX) ? cIdx : graphicsQueueFamilyIndex;

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> qCIs;
    std::vector<uint32_t> uniqueQueues = { graphicsQueueFamilyIndex };
    if (transferQueueFamilyIndex != graphicsQueueFamilyIndex)
        uniqueQueues.push_back(transferQueueFamilyIndex);
    if (computeQueueFamilyIndex != graphicsQueueFamilyIndex &&
        computeQueueFamilyIndex != transferQueueFamilyIndex)
        uniqueQueues.push_back(computeQueueFamilyIndex);

    for (uint32_t qi : uniqueQueues) {
        VkDeviceQueueCreateInfo qCI{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qCI.queueFamilyIndex = qi;
        qCI.queueCount       = 1;
        qCI.pQueuePriorities = &priority;
        qCIs.push_back(qCI);
    }

    std::vector<const char*> devExts;
    auto addDevExt = [&](const char* name) {
        if (checkDeviceExtension(physicalDevice, name)) {
            devExts.push_back(name);
            return true;
        }
        return false;
    };

    addDevExt(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    addDevExt(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
    addDevExt(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    addDevExt(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
    addDevExt(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
    addDevExt(VK_KHR_MAINTENANCE1_EXTENSION_NAME);
    addDevExt(VK_KHR_MAINTENANCE2_EXTENSION_NAME);
    addDevExt(VK_KHR_MAINTENANCE3_EXTENSION_NAME);
    addDevExt(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    addDevExt(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    addDevExt(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    addDevExt(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

    VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    features2.pNext   = &features12;
    features12.pNext  = &features13;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

    features12.descriptorIndexing         = features12.descriptorIndexing;
    features12.timelineSemaphore          = features12.timelineSemaphore;
    features13.dynamicRendering           = features13.dynamicRendering;
    features13.synchronization2           = features13.synchronization2;

    VkDeviceCreateInfo dCI{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dCI.pNext                   = &features2;
    dCI.queueCreateInfoCount    = (uint32_t)qCIs.size();
    dCI.pQueueCreateInfos       = qCIs.data();
    dCI.enabledExtensionCount   = (uint32_t)devExts.size();
    dCI.ppEnabledExtensionNames = devExts.data();

    r = vkCreateDevice(physicalDevice, &dCI, nullptr, &device);
    if (r != VK_SUCCESS) throw std::runtime_error("ravynOS: vkCreateDevice failed");

    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    vkGetDeviceQueue(device, transferQueueFamilyIndex, 0, &transferQueue);
    vkGetDeviceQueue(device, computeQueueFamilyIndex,  0, &computeQueue);

    VkCommandPoolCreateInfo cpCI{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpCI.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpCI.queueFamilyIndex = graphicsQueueFamilyIndex;
    vkCreateCommandPool(device, &cpCI, nullptr, &commandPool);

    std::cerr << "[ravynOS] Vulkan device ready. Monitors: " << monitorCount << "\n";
}

static VkSurfaceKHR createDarwinSurface(VkInstance instance, uint32_t monitorIndex) {
    id nsApp = objc_msgSend((id)objc_getClass("NSApplication"),
                             sel_registerName("sharedApplication"));

    MonitorInfo& mon = g_monitors[monitorIndex < g_monitorCount ? monitorIndex : 0];

    struct CGRect { struct { double x; double y; } origin; struct { double w; double h; } size; };
    CGRect winRect;
    winRect.origin.x = mon.posX;
    winRect.origin.y = mon.posY;
    winRect.size.w   = mon.width;
    winRect.size.h   = mon.height;

    uint64_t styleMask = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3);

    id window = objc_msgSend(
        objc_msgSend((id)objc_getClass("NSWindow"), sel_registerName("alloc")),
        sel_registerName("initWithContentRect:styleMask:backing:defer:"),
        winRect, styleMask, (uint64_t)2, (bool)false
    );

    if (!window) throw std::runtime_error("ravynOS: failed to create NSWindow");

    id title = objc_msgSend((id)objc_getClass("NSString"),
                             sel_registerName("stringWithUTF8String:"),
                             "ravynOS");
    objc_msgSend(window, sel_registerName("setTitle:"), title);
    objc_msgSend(window, sel_registerName("makeKeyAndOrderFront:"), nullptr);
    objc_msgSend(window, sel_registerName("center"));

    id view = objc_msgSend(window, sel_registerName("contentView"));

    id metalLayerClass = (id)objc_getClass("CAMetalLayer");
    if (!metalLayerClass) throw std::runtime_error("ravynOS: CAMetalLayer not found");

    id layer = objc_msgSend(metalLayerClass, sel_registerName("layer"));
    objc_msgSend(view, sel_registerName("setLayer:"), layer);
    objc_msgSend(view, sel_registerName("setWantsLayer:"), (bool)true);

    VkMetalSurfaceCreateInfoEXT surfInfo{ VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT };
    surfInfo.pLayer = (CAMetalLayer*)layer;

    auto fn = (PFN_vkCreateMetalSurfaceEXT)
        vkGetInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT");
    if (!fn) throw std::runtime_error("ravynOS: vkCreateMetalSurfaceEXT not available");

    VkSurfaceKHR surf = VK_NULL_HANDLE;
    VkResult r = fn(instance, &surfInfo, nullptr, &surf);
    if (r != VK_SUCCESS) throw std::runtime_error("ravynOS: vkCreateMetalSurfaceEXT failed");

    if (nsApp) objc_msgSend(nsApp, sel_registerName("activateIgnoringOtherApps:"), (bool)true);

    return surf;
}

void VulkanRenderContext::createSwapchain(VkInstance inst, VkPhysicalDevice phys, VkDevice dev,
                                           uint32_t w, uint32_t h) {
    surface = createDarwinSurface(inst, 0);

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, formats.data());

    VkSurfaceFormatKHR chosenFmt = formats[0];
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFmt = f;
            break;
        }
    }
    if (chosenFmt.format == formats[0].format) {
        for (auto& f : formats) {
            if (f.format == VK_FORMAT_R8G8B8A8_UNORM) { chosenFmt = f; break; }
        }
    }

    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> pmodes(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pmCount, pmodes.data());

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto& pm : pmodes) {
        if (pm == VK_PRESENT_MODE_MAILBOX_KHR) { presentMode = pm; break; }
    }

    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width  = std::max(caps.minImageExtent.width,  std::min(caps.maxImageExtent.width,  w));
        extent.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, h));
    }
    swapchainExtent = extent;
    swapchainFormat = chosenFmt.format;

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
        imgCount = caps.maxImageCount;
    imgCount = std::min(imgCount, (uint32_t)RAVYN_MAX_FRAMES_IN_FLIGHT);

    VkSwapchainCreateInfoKHR scCI{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    scCI.surface          = surface;
    scCI.minImageCount    = imgCount;
    scCI.imageFormat      = chosenFmt.format;
    scCI.imageColorSpace  = chosenFmt.colorSpace;
    scCI.imageExtent      = extent;
    scCI.imageArrayLayers = 1;
    scCI.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    scCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scCI.preTransform     = caps.currentTransform;
    scCI.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    scCI.presentMode      = presentMode;
    scCI.clipped          = VK_TRUE;

    VkResult r = vkCreateSwapchainKHR(dev, &scCI, nullptr, &swapchain);
    if (r != VK_SUCCESS) throw std::runtime_error("ravynOS: vkCreateSwapchainKHR failed");

    uint32_t swapCount = 0;
    vkGetSwapchainImagesKHR(dev, swapchain, &swapCount, nullptr);
    swapchainImages.resize(swapCount);
    vkGetSwapchainImagesKHR(dev, swapchain, &swapCount, swapchainImages.data());

    swapchainImageViews.resize(swapCount);
    for (uint32_t i = 0; i < swapCount; ++i) {
        VkImageViewCreateInfo ivCI{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        ivCI.image                           = swapchainImages[i];
        ivCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ivCI.format                          = chosenFmt.format;
        ivCI.components                      = { VK_COMPONENT_SWIZZLE_IDENTITY,
                                                 VK_COMPONENT_SWIZZLE_IDENTITY,
                                                 VK_COMPONENT_SWIZZLE_IDENTITY,
                                                 VK_COMPONENT_SWIZZLE_IDENTITY };
        ivCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ivCI.subresourceRange.baseMipLevel   = 0;
        ivCI.subresourceRange.levelCount     = 1;
        ivCI.subresourceRange.baseArrayLayer = 0;
        ivCI.subresourceRange.layerCount     = 1;
        vkCreateImageView(dev, &ivCI, nullptr, &swapchainImageViews[i]);
    }

    VkAttachmentDescription colorAtt{};
    colorAtt.format         = chosenFmt.format;
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
    rpCI.attachmentCount = 1;
    rpCI.pAttachments    = &colorAtt;
    rpCI.subpassCount    = 1;
    rpCI.pSubpasses      = &subpass;
    rpCI.dependencyCount = 1;
    rpCI.pDependencies   = &dep;
    vkCreateRenderPass(dev, &rpCI, nullptr, &renderPass);

    framebuffers.resize(swapCount);
    for (uint32_t i = 0; i < swapCount; ++i) {
        VkFramebufferCreateInfo fbCI{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fbCI.renderPass      = renderPass;
        fbCI.attachmentCount = 1;
        fbCI.pAttachments    = &swapchainImageViews[i];
        fbCI.width           = extent.width;
        fbCI.height          = extent.height;
        fbCI.layers          = 1;
        vkCreateFramebuffer(dev, &fbCI, nullptr, &framebuffers[i]);
    }

    commandBuffers.resize(swapCount);
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
        vkCreateSemaphore(dev, &semCI, nullptr, &imageAvailableSemaphores[i]);
        vkCreateSemaphore(dev, &semCI, nullptr, &renderFinishedSemaphores[i]);
        vkCreateFence(dev, &fenceCI, nullptr, &inFlightFences[i]);
    }

    std::cerr << "[ravynOS] Swapchain ready: " << extent.width << "x" << extent.height
              << " format=" << chosenFmt.format
              << " images=" << swapCount << "\n";
}

VkImage VulkanRenderContext::importExternalMemory(int fd, uint32_t w, uint32_t h) {
    VkExternalMemoryImageCreateInfo extICI{ VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
    extICI.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkImageCreateInfo iCI{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    iCI.pNext         = &extICI;
    iCI.imageType     = VK_IMAGE_TYPE_2D;
    iCI.format        = swapchainFormat != VK_FORMAT_UNDEFINED ? swapchainFormat : VK_FORMAT_B8G8R8A8_UNORM;
    iCI.extent        = { w, h, 1 };
    iCI.mipLevels     = 1;
    iCI.arrayLayers   = 1;
    iCI.samples       = VK_SAMPLE_COUNT_1_BIT;
    iCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
    iCI.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                      | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    iCI.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    iCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage img = VK_NULL_HANDLE;
    VkResult r = vkCreateImage(device, &iCI, nullptr, &img);
    if (r != VK_SUCCESS) return VK_NULL_HANDLE;

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device, img, &req);

    VkImportMemoryFdInfoKHR importInfo{ VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR };
    importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    importInfo.fd         = fd;

    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.pNext           = &importInfo;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(physicalDevice, req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory mem = VK_NULL_HANDLE;
    r = vkAllocateMemory(device, &ai, nullptr, &mem);
    if (r != VK_SUCCESS) { vkDestroyImage(device, img, nullptr); return VK_NULL_HANDLE; }

    vkBindImageMemory(device, img, mem, 0);
    return img;
}

VkDeviceMemory VulkanRenderContext::allocateVideoMemory(VkMemoryRequirements req,
                                                         VkMemoryPropertyFlags props) {
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(physicalDevice, req.memoryTypeBits, props);

    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkResult r = vkAllocateMemory(device, &ai, nullptr, &mem);
    if (r != VK_SUCCESS) throw std::runtime_error("ravynOS: vkAllocateMemory failed");
    return mem;
}

void VulkanRenderContext::recordAndSubmitFrame(uint32_t imageIndex) {
    VkCommandBuffer cb = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &beginInfo);

    VkClearValue clearVal{};
    clearVal.color = {{ 0.05f, 0.05f, 0.08f, 1.0f }};

    VkRenderPassBeginInfo rpBI{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBI.renderPass               = renderPass;
    rpBI.framebuffer              = framebuffers[imageIndex];
    rpBI.renderArea.offset        = { 0, 0 };
    rpBI.renderArea.extent        = swapchainExtent;
    rpBI.clearValueCount          = 1;
    rpBI.pClearValues             = &clearVal;

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
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        std::cerr << "[ravynOS] Swapchain out of date, needs recreation\n";
    }

    currentFrame = (currentFrame + 1) % RAVYN_MAX_FRAMES_IN_FLIGHT;
}

void presentNextFrame(VulkanRenderContext* ctx) {
    if (!ctx || ctx->device == VK_NULL_HANDLE || ctx->swapchain == VK_NULL_HANDLE) return;

    vkWaitForFences(ctx->device, 1, &ctx->inFlightFences[ctx->currentFrame],
                    VK_TRUE, UINT64_MAX);
    vkResetFences(ctx->device, 1, &ctx->inFlightFences[ctx->currentFrame]);

    uint32_t imageIndex = 0;
    VkResult r = vkAcquireNextImageKHR(ctx->device, ctx->swapchain, UINT64_MAX,
                                        ctx->imageAvailableSemaphores[ctx->currentFrame],
                                        VK_NULL_HANDLE, &imageIndex);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return;

    ctx->recordAndSubmitFrame(imageIndex);
    ctx->presentFrame(imageIndex);
}

static void dispatchMouseEvent(id nsApp, Point p, uint32_t type) {
    if (!nsApp) return;
    id ev = objc_msgSend(
        (id)objc_getClass("NSEvent"),
        sel_registerName("mouseEventWithType:location:modifierFlags:timestamp:windowNumber:context:eventNumber:clickCount:pressure:"),
        (unsigned long)type, p.x, p.y,
        (unsigned long)0, 0.0, 0,
        (id)nullptr, 0, 1, 1.0f
    );
    if (ev) objc_msgSend(nsApp, sel_registerName("sendEvent:"), ev);
}

static void dispatchKeyEvent(id nsApp, uint16_t keyCode, bool down) {
    if (!nsApp) return;
    id ev = objc_msgSend(
        (id)objc_getClass("NSEvent"),
        sel_registerName("keyEventWithType:location:modifierFlags:timestamp:windowNumber:context:characters:charactersIgnoringModifiers:isARepeat:keyCode:"),
        (unsigned long)(down ? 10 : 11),
        0.0, 0.0,
        (unsigned long)0,
        0.0, 0,
        (id)nullptr,
        objc_msgSend((id)objc_getClass("NSString"), sel_registerName("string")),
        objc_msgSend((id)objc_getClass("NSString"), sel_registerName("string")),
        (bool)false,
        (unsigned short)keyCode
    );
    if (ev) objc_msgSend(nsApp, sel_registerName("sendEvent:"), ev);
}

struct MachMousePayload {
    mach_msg_header_t header;
    double x;
    double y;
    uint32_t type;
    uint32_t _pad;
};

struct MachKeyPayload {
    mach_msg_header_t header;
    uint16_t keyCode;
    uint8_t  down;
    uint8_t  _pad[5];
};

static void runDarwinEventLoop(VulkanRenderContext* ctx) {
    int kq = kqueue();
    if (kq < 0) throw std::runtime_error("ravynOS: kqueue() failed");

    struct kevent changes[3];
    EV_SET(&changes[0], 1, EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_MSECONDS, 16, nullptr);

    mach_port_t mousePort = MACH_PORT_NULL, keyPort = MACH_PORT_NULL;
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &mousePort);
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &keyPort);

    EV_SET(&changes[1], mousePort, EVFILT_MACHPORT, EV_ADD | EV_ENABLE, 0, 0, (void*)(uintptr_t)1);
    EV_SET(&changes[2], keyPort,   EVFILT_MACHPORT, EV_ADD | EV_ENABLE, 0, 0, (void*)(uintptr_t)2);

    int nchanges = kevent(kq, changes, 3, nullptr, 0, nullptr);
    if (nchanges < 0) {
        std::cerr << "[ravynOS] kevent registration warning: " << strerror(errno) << "\n";
    }

    id nsApp = objc_msgSend((id)objc_getClass("NSApplication"),
                             sel_registerName("sharedApplication"));

    alignas(16) char buffer[4096];
    uint64_t frameCount = 0;
    mach_timebase_info_data_t tbInfo{};
    mach_timebase_info(&tbInfo);
    uint64_t lastFrameTime = mach_absolute_time();
    const uint64_t targetFrameNs = 16666667ULL;

    std::cerr << "[ravynOS] Event loop started\n";

    while (true) {
        struct kevent ev{};
        struct timespec timeout{ 0, 1000000 };
        int n = kevent(kq, nullptr, 0, &ev, 1, &timeout);

        if (n > 0) {
            if (ev.filter == EVFILT_MACHPORT) {
                mach_msg_header_t* msg = (mach_msg_header_t*)buffer;
                memset(buffer, 0, sizeof(buffer));
                mach_port_t rcvPort = (mach_port_t)ev.ident;
                kern_return_t kr = mach_msg(msg, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                                             0, sizeof(buffer), rcvPort, 0, MACH_PORT_NULL);
                if (kr == KERN_SUCCESS) {
                    uintptr_t tag = (uintptr_t)ev.udata;
                    if (tag == 1 && msg->msgh_size >= sizeof(MachMousePayload)) {
                        MachMousePayload* mp = (MachMousePayload*)msg;
                        dispatchMouseEvent(nsApp, {mp->x, mp->y}, mp->type ? mp->type : 1);
                    } else if (tag == 2 && msg->msgh_size >= sizeof(MachKeyPayload)) {
                        MachKeyPayload* kp = (MachKeyPayload*)msg;
                        dispatchKeyEvent(nsApp, kp->keyCode, kp->down != 0);
                    }
                }
            }
            else if (ev.filter == EVFILT_TIMER) {
                if (nsApp) {
                    id event = objc_msgSend(nsApp,
                        sel_registerName("nextEventMatchingMask:untilDate:inMode:dequeue:"),
                        (unsigned long)~0UL,
                        nullptr,
                        objc_msgSend((id)objc_getClass("NSString"),
                                     sel_registerName("stringWithUTF8String:"),
                                     "kCFRunLoopDefaultMode"),
                        (bool)true
                    );
                    if (event) objc_msgSend(nsApp, sel_registerName("sendEvent:"), event);
                    objc_msgSend(nsApp, sel_registerName("updateWindows"));
                }
                presentNextFrame(ctx);
                ++frameCount;
            }
        } else {
            uint64_t now = mach_absolute_time();
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

bool AIRToSPIRVCompiler::compileAIRToSPIRV(const std::vector<uint8_t>& airBytecode,
                                             std::vector<uint32_t>& outSpirv) {
    if (airBytecode.empty()) return false;

    std::string bin(airBytecode.begin(), airBytecode.end());
    llvm::MemoryBufferRef buf(bin, "AIR_Stream");
    llvm::LLVMContext llvmCtx;

    auto modOrErr = llvm::parseBitcodeFile(buf, llvmCtx);
    if (!modOrErr) {
        std::cerr << "[ravynOS AIR->SPIR-V] parseBitcodeFile failed\n";
        return false;
    }

    std::unique_ptr<llvm::Module> M = std::move(modOrErr.get());

    parseAppleMetalMetadata(M.get());
    transformAppleMemoryBarriers(M.get());
    transformThreadgroupAddressSpace(M.get());
    processAppleIntrinsics(M.get());

    SPIRV::TranslatorOpts opts;
    opts.enableAllExtensions();
    opts.setDesiredBIsFormat(SPIRV::BIsRepresentation::OpenCL100);
    opts.setDebugInfoEIS(SPIRV::DebugInfoEIS::OpenCL_DebugInfo_100);

    std::string errLog;
    std::ostringstream spirvStream(std::ios::binary);

    if (!llvm::writeSpirv(M.get(), opts, spirvStream, errLog)) {
        std::cerr << "[ravynOS AIR->SPIR-V] translation failed: " << errLog << "\n";
        return false;
    }

    std::string spirvStr = spirvStream.str();
    if (spirvStr.empty()) return false;

    size_t words = spirvStr.size() / sizeof(uint32_t);
    outSpirv.resize(words);
    std::memcpy(outSpirv.data(), spirvStr.data(), words * sizeof(uint32_t));

    std::cerr << "[ravynOS AIR->SPIR-V] OK, " << words << " words\n";
    return true;
}

static id sharedApplication_impl(id self, SEL) { return self; }
static void run_impl(id self, SEL) {
    VulkanRenderContext* ctx = GetGlobalVulkanContext();
    try {
        ctx->initContext();
        MonitorInfo& mon = g_monitors[0];
        ctx->createSwapchain(ctx->instance, ctx->physicalDevice, ctx->device,
                              mon.width, mon.height);
        runDarwinEventLoop(ctx);
    } catch (std::exception& e) {
        std::cerr << "[ravynOS] Fatal: " << e.what() << "\n";
    }
}
static id winInit_func(id self, SEL, CGRect rect, uint64_t mask, uint64_t backing, bool defer) {
    return self;
}
static id srgb_func(id self, SEL) { return self; }
static id sysFont_func(id self, SEL, double size) { return self; }
static id screens_func(id self, SEL) { return nullptr; }
static void setTitle_func(id self, SEL, id title) {}
static void makeKey_func(id self, SEL, id sender) {}
static void center_func(id self, SEL) {}
static id contentView_func(id self, SEL) { return self; }
static void setLayer_func(id self, SEL, id layer) {}
static void setWantsLayer_func(id self, SEL, bool v) {}
static id layer_func(id self, SEL) { return self; }

extern "C" {
    int NSApplicationMain(int argc, const char* argv[]) {
        Class nsObject = objc_getClass("NSObject");
        if (!nsObject) {
            std::cerr << "[ravynOS] NSObject not found — ObjC runtime not available\n";
            return 1;
        }

        auto safeAddClass = [&](const char* name,
                                 std::function<void(Class, Class)> setup) {
            if (!objc_getClass(name)) {
                Class c = objc_allocateClassPair(nsObject, name, 0);
                if (c) {
                    Class meta = object_getClass((id)c);
                    setup(c, meta);
                    objc_registerClassPair(c);
                }
            }
        };

        safeAddClass("NSApplication", [](Class c, Class meta) {
            class_addMethod(meta, sel_registerName("sharedApplication"),
                            (IMP)sharedApplication_impl, "@:");
            class_addMethod(c, sel_registerName("run"),
                            (IMP)run_impl, "v@:");
        });

        safeAddClass("NSWindow", [](Class c, Class meta) {
            class_addMethod(c,
                sel_registerName("initWithContentRect:styleMask:backing:defer:"),
                (IMP)winInit_func,
                "@@:{CGRect={CGPoint=dd}{CGSize=dd}}QQB");
            class_addMethod(c, sel_registerName("setTitle:"),     (IMP)setTitle_func,   "v@:@");
            class_addMethod(c, sel_registerName("makeKeyAndOrderFront:"), (IMP)makeKey_func, "v@:@");
            class_addMethod(c, sel_registerName("center"),        (IMP)center_func,     "v@:");
            class_addMethod(c, sel_registerName("contentView"),   (IMP)contentView_func,"@@:");
        });

        safeAddClass("NSView", [](Class c, Class meta) {
            class_addMethod(c, sel_registerName("setLayer:"),      (IMP)setLayer_func,      "v@:@");
            class_addMethod(c, sel_registerName("setWantsLayer:"), (IMP)setWantsLayer_func, "v@:B");
            class_addMethod(c, sel_registerName("layer"),          (IMP)layer_func,         "@@:");
        });

        safeAddClass("NSColorSpace", [](Class c, Class meta) {
            class_addMethod(meta, sel_registerName("sRGBColorSpace"), (IMP)srgb_func, "@:");
        });

        safeAddClass("NSFont", [](Class c, Class meta) {
            class_addMethod(meta, sel_registerName("systemFontOfSize:"), (IMP)sysFont_func, "@:d");
        });

        safeAddClass("NSScreen", [](Class c, Class meta) {
            class_addMethod(meta, sel_registerName("screens"), (IMP)screens_func, "@@:");
        });

        id app = objc_msgSend((id)objc_getClass("NSApplication"),
                               sel_registerName("sharedApplication"));
        if (app) {
            objc_msgSend(app, sel_registerName("run"));
        } else {
            VulkanRenderContext* ctx = GetGlobalVulkanContext();
            try {
                ctx->initContext();
                ctx->createSwapchain(ctx->instance, ctx->physicalDevice, ctx->device,
                                      RAVYN_DEFAULT_WIDTH, RAVYN_DEFAULT_HEIGHT);
                runDarwinEventLoop(ctx);
            } catch (std::exception& e) {
                std::cerr << "[ravynOS] Fatal: " << e.what() << "\n";
                return 1;
            }
        }

        return 0;
    }
}
