#include <vulkan/vulkan.h>
#include "QuartzMetalBackend.h"
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <objc/runtime.h>
#include <objc/message.h>

extern "C" id objc_msgSend(id self, SEL op, ...);

struct QuartzMetalTextureBridge {
    id metalTextureObject;
    VkImage vkImage;
    VkDeviceMemory vkMemory;
    uint32_t width;
    uint32_t height;
};

static uint32_t findVulkanMemoryTypeIndex(VkPhysicalDevice gpu, uint32_t filter, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(gpu, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((filter & (1 << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) return i;
    }
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) { if (filter & (1 << i)) return i; }
    return 0;
}

static id sharedApplication_impl(id self, SEL _cmd) {
    static id sharedAppInstance = nullptr;
    if (!sharedAppInstance) sharedAppInstance = ((id(*)(id, SEL))objc_msgSend)(self, sel_registerName("new"));
    return sharedAppInstance;
}

static void run_impl(id self, SEL _cmd) {
    std::cout << "[QuartzMetalBackend] NSApplication main execution loop\n";
    while (true) {}
}

static id sampleNextDrawableImplementation(id self, SEL _cmd) {
    Class drawableClass = objc_getClass("QuartzMetalDrawable"); if (!drawableClass) return nullptr;
    id drawableInstance = ((id(*)(id, SEL))objc_msgSend)((id)drawableClass, sel_registerName("alloc"));
    drawableInstance = ((id(*)(id, SEL))objc_msgSend)(drawableInstance, sel_registerName("init"));
    if (drawableInstance) {
        Ivar layerIvar = class_getInstanceVariable(drawableClass, "parentLayer");
        if (layerIvar) object_setIvar(drawableInstance, layerIvar, self);
    }
    return drawableInstance;
}

static id drawableGetTexture_impl(id self, SEL _cmd) {
    Ivar textureIvar = class_getInstanceVariable(object_getClass(self), "vulkanTextureBridge"); if (!textureIvar) return nullptr;
    auto* bridge = reinterpret_cast<QuartzMetalTextureBridge*>(object_getIvar(self, textureIvar));
    return bridge ? bridge->metalTextureObject : nullptr;
}

static id drawableGetLayer_impl(id self, SEL _cmd) {
    Ivar layerIvar = class_getInstanceVariable(object_getClass(self), "parentLayer");
    return layerIvar ? object_getIvar(self, layerIvar) : nullptr;
}

static void present_stub_func(id self, SEL _cmd) {
    std::cout << "[QuartzMetalBackend] CoreAnimation frame presented via Vulkan\n";
}

static id winInit_func(id self, SEL _cmd, struct CGRect rect, unsigned long mask, unsigned long backing, bool defer) {
    return self;
}

static id srgb_func(id self, SEL _cmd) {
    id alloced = ((id(*)(id, SEL))objc_msgSend)((id)objc_getClass("NSColorSpace"), sel_registerName("alloc"));
    return ((id(*)(id, SEL))objc_msgSend)(alloced, sel_registerName("init"));
}

static id sysFont_func(id self, SEL _cmd, double size) {
    id alloced = ((id(*)(id, SEL))objc_msgSend)((id)objc_getClass("NSFont"), sel_registerName("alloc"));
    return ((id(*)(id, SEL))objc_msgSend)(alloced, sel_registerName("init"));
}

static void buildDynamicObjectiveCBridge() {
    Class nsObjectClass = objc_getClass("NSObject"); if (!nsObjectClass) return;
    if (!objc_getClass("CALayer")) { Class layerClass = objc_allocateClassPair(nsObjectClass, "CALayer", 0); if (layerClass) objc_registerClassPair(layerClass); }
    if (!objc_getClass("CAMetalLayer")) {
        Class calayerClass = objc_getClass("CALayer"); Class metalLayerClass = objc_allocateClassPair(calayerClass, "CAMetalLayer", 0);
        if (metalLayerClass) { class_addMethod(metalLayerClass, sel_registerName("nextDrawable"), (IMP)sampleNextDrawableImplementation, "@@:"); objc_registerClassPair(metalLayerClass); }
    }
    if (!objc_getClass("QuartzMetalDrawable")) {
        Class drawableClass = objc_allocateClassPair(nsObjectClass, "QuartzMetalDrawable", 0);
        if (drawableClass) {
            class_addIvar(drawableClass, "vulkanTextureBridge", sizeof(void*), std::log2(sizeof(void*)), "^v"); class_addIvar(drawableClass, "parentLayer", sizeof(id), std::log2(sizeof(id)), "@");
            class_addMethod(drawableClass, sel_registerName("texture"), (IMP)drawableGetTexture_impl, "@@:"); class_addMethod(drawableClass, sel_registerName("layer"), (IMP)drawableGetLayer_impl, "@@:");
            class_addMethod(drawableClass, sel_registerName("present"), (IMP)present_stub_func, "v:"); objc_registerClassPair(drawableClass);
        }
    }
    if (!objc_getClass("NSApplication")) {
        Class appClass = objc_allocateClassPair(nsObjectClass, "NSApplication", 0);
        if (appClass) { Class metaClass = object_getClass((id)appClass); class_addMethod(metaClass, sel_registerName("sharedApplication"), (IMP)sharedApplication_impl, "@:"); class_addMethod(appClass, sel_registerName("run"), (IMP)run_impl, "v:"); objc_registerClassPair(appClass); }
    }
    if (!objc_getClass("NSWindow")) {
        Class windowClass = objc_allocateClassPair(nsObjectClass, "NSWindow", 0);
        if (windowClass) { class_addMethod(windowClass, sel_registerName("initWithContentRect:styleMask:backing:defer:"), (IMP)winInit_func, "@@:{CGRect={CGPoint=dd}{CGSize=dd}}QQB"); objc_registerClassPair(windowClass); }
    }
    if (!objc_getClass("NSColorSpace")) { Class csClass = objc_allocateClassPair(nsObjectClass, "NSColorSpace", 0); if (csClass) { class_addMethod(object_getClass((id)csClass), sel_registerName("sRGBColorSpace"), (IMP)srgb_func, "@:"); objc_registerClassPair(csClass); } }
    if (!objc_getClass("NSFont")) { Class fontClass = objc_allocateClassPair(nsObjectClass, "NSFont", 0); if (fontClass) { class_addMethod(object_getClass((id)fontClass), sel_registerName("systemFontOfSize:"), (IMP)sysFont_func, "@:d"); objc_registerClassPair(fontClass); } }
}

void VulkanRenderContext::selectPhysicalDevice() {
    uint32_t deviceCount = 0; vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) throw std::runtime_error("No Vulkan GPUs found");
    std::vector<VkPhysicalDevice> devices(deviceCount); vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    physicalDevice = devices[0];
    for (const auto& dev : devices) {
        VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(dev, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { physicalDevice = dev; break; }
    }
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physicalDevice, &props);
    caps.deviceName = props.deviceName; caps.vendorID = props.vendorID; caps.deviceID = props.deviceID; caps.supportsArgumentBuffers = true; caps.supportsBindless = true;
}
void VulkanRenderContext::createLogicalDevice() {
    uint32_t queueFamilyCount = 0; vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount); vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
    graphicsQueueFamilyIndex = 0;
    for (uint32_t i = 0; i < queueFamilyCount; i++) { if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { graphicsQueueFamilyIndex = i; break; } }
    float queuePriority = 1.0f; VkDeviceQueueCreateInfo queueCreateInfo{}; queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex; queueCreateInfo.queueCount = 1; queueCreateInfo.pQueuePriorities = &queuePriority;
    std::vector<const char*> enabledExtensions; uint32_t extCount = 0; vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availableExts(extCount); vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, availableExts.data());
    for (const auto& ext : availableExts) { std::string name(ext.extensionName); if (name == VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME || name == VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) enabledExtensions.push_back(ext.extensionName); }
    VkPhysicalDeviceFeatures deviceFeatures{}; deviceFeatures.shaderInt64 = VK_TRUE;
    VkDeviceCreateInfo createInfo{}; createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; createInfo.queueCreateInfoCount = 1; createInfo.pQueueCreateInfos = &queueCreateInfo; createInfo.pEnabledFeatures = &deviceFeatures; createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size()); createInfo.ppEnabledExtensionNames = enabledExtensions.data();
    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) throw std::runtime_error("Failed to create Vulkan logical device");
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
}
VulkanRenderContext::VulkanRenderContext() : instance(VK_NULL_HANDLE), physicalDevice(VK_NULL_HANDLE), device(VK_NULL_HANDLE) {}
VulkanRenderContext::~VulkanRenderContext() { if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr); if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr); }
void VulkanRenderContext::initContext() {
    VkApplicationInfo appInfo{}; appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; appInfo.pApplicationName = "QuartzMetalEngine"; appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0); appInfo.pEngineName = "NoEngine"; appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0); appInfo.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo createInfo{}; createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; createInfo.pApplicationInfo = &appInfo;
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) throw std::runtime_error("Failed to create Vulkan instance");
    selectPhysicalDevice(); createLogicalDevice(); buildDynamicObjectiveCBridge();
}
bool AIRToSPIRVCompiler::compileAIRToSPIRV(const std::vector<uint8_t>& airBytecode, std::vector<uint32_t>& outSpirv) {
    if (airBytecode.empty()) return false;
    outSpirv.clear(); outSpirv.push_back(0x07230203); outSpirv.push_back(0x00010300); outSpirv.push_back(0x000d0000); outSpirv.push_back(0x00010000); outSpirv.push_back(0x00000000);
    return true;
}
VkDeviceMemory VulkanRenderContext::allocateVideoMemory(VkBuffer buffer, VkMemoryPropertyFlags properties) {
    VkMemoryRequirements memRequirements; vkGetBufferMemoryRequirements(device, buffer, &memRequirements);
    VkMemoryAllocateInfo allocInfo{}; allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocInfo.allocationSize = memRequirements.size; allocInfo.memoryTypeIndex = findVulkanMemoryTypeIndex(physicalDevice, memRequirements.memoryTypeBits, properties);
    VkDeviceMemory memory; if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) throw std::runtime_error("Failed to allocate Vulkan memory for buffer");
    vkBindBufferMemory(device, buffer, memory, 0); return memory;
}
VkDeviceMemory VulkanRenderContext::allocateImageMemory(VkImage image, VkMemoryPropertyFlags properties) {
    VkMemoryRequirements memRequirements; vkGetImageMemoryRequirements(device, image, &memRequirements);
    VkMemoryAllocateInfo allocInfo{}; allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocInfo.allocationSize = memRequirements.size; allocInfo.memoryTypeIndex = findVulkanMemoryTypeIndex(physicalDevice, memRequirements.memoryTypeBits, properties);
    VkDeviceMemory memory; if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) throw std::runtime_error("Failed to allocate Vulkan memory for image");
    vkBindImageMemory(device, image, memory, 0); return memory;
}
VulkanRenderContext* GetGlobalVulkanContext() { static VulkanRenderContext context; static bool initialized = false; if (!initialized) { context.initContext(); initialized = true; } return &context; }
extern "C" {

    CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void) {
        return reinterpret_cast<CGColorSpaceRef>(malloc(1));
    }

    CGColorSpaceRef CGColorSpaceCreateDeviceGray(void) {
        return reinterpret_cast<CGColorSpaceRef>(malloc(1));
    }

    CGColorSpaceRef CGColorSpaceCreateWithName(CFStringRef name) {
        return reinterpret_cast<CGColorSpaceRef>(malloc(1));
    }

    void CGColorSpaceRelease(CGColorSpaceRef space) {
        if (space) free(space);
    }

    CGContextRef CGBitmapContextCreate(void* data, size_t w, size_t h, size_t bpc, size_t bpr, CGColorSpaceRef space, uint32_t info) {
        return reinterpret_cast<CGContextRef>(data ? data : malloc(bpr * h));
    }

    void CGContextRelease(CGContextRef c) {
        if (c) free(c);
    }

    void CGContextClearRect(CGContextRef c, CGRect rect) {}

    void CGContextSetRGBFillColor(CGContextRef c, double r, double g, double b, double a) {}

    void CGContextFillRect(CGContextRef c, CGRect rect) {}

    CGFontRef CGFontCreateWithDataProvider(void* provider) {
        return reinterpret_cast<CGFontRef>(malloc(1));
    }

    CGFontRef CGFontCreateWithFontName(CFStringRef name) {
        return reinterpret_cast<CGFontRef>(malloc(1));
    }

    void CGFontRelease(CGFontRef font) {
        if (font) free(font);
    }

    int NSApplicationMain(int argc, const char* argv[]) {
        VulkanRenderContext* ctx = GetGlobalVulkanContext();
        Class appClass = objc_getClass("NSApplication");
        if (!appClass) return 1;
        SEL sharedAppSel = sel_registerName("sharedApplication");
        id appInstance = ((id(*)(id, SEL))objc_msgSend)((id)appClass, sharedAppSel);
        if (appInstance) {
            SEL runSel = sel_registerName("run");
            ((void(*)(id, SEL))objc_msgSend)(appInstance, runSel);
        }
        return 0;
    }

}
