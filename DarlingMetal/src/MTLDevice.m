#import "../include/MTLDevice.h"
#import <Foundation/Foundation.h>
#include <vulkan/vulkan.h>

typedef struct {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    uint32_t queueFamilyIndex;
} RavynMetalCoreContext;

@interface MyMTLDevice : NSObject <MTLDevice>
- (VkDevice)vulkanDevice;
@end

@interface MyMTLCommandQueue : NSObject <MTLCommandQueue>
@property (readonly) id<MTLDevice> device;
@end

@implementation MyMTLCommandQueue {
    id<MTLDevice> _device;
    VkCommandPool _pool;
}

- (instancetype)initWithDevice:(id<MTLDevice>)device queueFamilyIndex:(uint32_t)idx vulkanDevice:(VkDevice)vulkanDevice {
    self = [super init];
    if (!self) return nil;
    _device = device;

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = idx;

    if (vkCreateCommandPool(vulkanDevice, &poolInfo, NULL, &_pool) != VK_SUCCESS) {
        [self release];
        return nil;
    }
    return self;
}

- (id<MTLDevice>)device {
    return _device;
}

- (void)dealloc {
    if (_pool && _device && [_device respondsToSelector:@selector(vulkanDevice)]) {
        VkDevice rawDevice = [(MyMTLDevice *)_device vulkanDevice];
        if (rawDevice) {
            vkDestroyCommandPool(rawDevice, _pool, NULL);
        }
    }
    [super dealloc];
}
@end

@interface MyMTLBuffer : NSObject <MTLBuffer>
@property (readonly) NSUInteger length;
@property (readonly) void *contents;
@end

@implementation MyMTLBuffer {
    VkBuffer _buffer;
    VkDeviceMemory _memory;
    NSUInteger _length;
    void *_mappedData;
    id<MTLDevice> _device;
}

- (instancetype)initWithDevice:(id<MTLDevice>)device length:(NSUInteger)length bytes:(const void *)pointer vulkanDevice:(VkDevice)vulkanDevice memoryProperties:(VkPhysicalDeviceMemoryProperties)memProps {
    self = [super init];
    if (!self) return nil;
    _device = device;
    _length = length;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = length;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vulkanDevice, &bufferInfo, NULL, &_buffer) != VK_SUCCESS) {
        [self release];
        return nil;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(vulkanDevice, _buffer, &memReqs);

    uint32_t memTypeIdx = UINT32_MAX;
    VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReqs.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & flags) == flags) {
            memTypeIdx = i;
            break;
        }
    }

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memTypeIdx;

    if (vkAllocateMemory(vulkanDevice, &allocInfo, NULL, &_memory) != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice, _buffer, NULL);
        [self release];
        return nil;
    }

    vkBindBufferMemory(vulkanDevice, _buffer, _memory, 0);

    if (vkMapMemory(vulkanDevice, _memory, 0, length, 0, &_mappedData) == VK_SUCCESS && pointer) {
        memcpy(_mappedData, pointer, length);
    }

    return self;
}

- (NSUInteger)length { return _length; }
- (void *)contents { return _mappedData; }

- (void)dealloc {
    if (_device && [_device respondsToSelector:@selector(vulkanDevice)]) {
        VkDevice rawDevice = [(MyMTLDevice *)_device vulkanDevice];
        if (rawDevice) {
            if (_mappedData) vkUnmapMemory(rawDevice, _memory);
            if (_buffer) vkDestroyBuffer(rawDevice, _buffer, NULL);
            if (_memory) vkFreeMemory(rawDevice, _memory, NULL);
        }
    }
    [super dealloc];
}
@end

@interface MyMTLTexture : NSObject <MTLTexture>
@end

@implementation MyMTLTexture {
    VkImage _image;
    VkDeviceMemory _memory;
    id<MTLDevice> _device;
}

- (instancetype)initWithDevice:(id<MTLDevice>)device vulkanDevice:(VkDevice)vulkanDevice memoryProperties:(VkPhysicalDeviceMemoryProperties)memProps {
    self = [super init];
    if (!self) return nil;
    _device = device;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    imageInfo.extent.width = 1024;
    imageInfo.extent.height = 1024;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(vulkanDevice, &imageInfo, NULL, &_image) != VK_SUCCESS) {
        [self release];
        return nil;
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(vulkanDevice, _image, &memReqs);

    uint32_t memTypeIdx = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReqs.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memTypeIdx = i;
            break;
        }
    }

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memTypeIdx;

    if (vkAllocateMemory(vulkanDevice, &allocInfo, NULL, &_memory) != VK_SUCCESS) {
        vkDestroyImage(vulkanDevice, _image, NULL);
        [self release];
        return nil;
    }

    vkBindImageMemory(vulkanDevice, _image, _memory, 0);
    return self;
}

- (void)dealloc {
    if (_device && [_device respondsToSelector:@selector(vulkanDevice)]) {
        VkDevice rawDevice = [(MyMTLDevice *)_device vulkanDevice];
        if (rawDevice) {
            if (_image) vkDestroyImage(rawDevice, _image, NULL);
            if (_memory) vkFreeMemory(rawDevice, _memory, NULL);
        }
    }
    [super dealloc];
}
@end

@implementation MyMTLDevice {
    RavynMetalCoreContext *_context;
    NSString *_deviceName;
    VkPhysicalDeviceMemoryProperties _memProps;
}

- (VkDevice)vulkanDevice {
    return _context ? _context->device : VK_NULL_HANDLE;
}

- (instancetype)init {
    self = [super init];
    if (!self) return nil;

    _context = malloc(sizeof(RavynMetalCoreContext));
    if (!_context) return nil;
    memset(_context, 0, sizeof(RavynMetalCoreContext));

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "ravynOS Metal Runtime";
    appInfo.apiVersion = VK_API_VERSION_1_4;

    const char* instanceExtensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(__linux__)
        "VK_KHR_xlib_surface"
#elif defined(__APPLE__) || defined(__MACH__)
        "VK_EXT_metal_surface"
#endif
    };

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = sizeof(instanceExtensions) / sizeof(const char*);
    createInfo.ppEnabledExtensionNames = instanceExtensions;

    if (vkCreateInstance(&createInfo, NULL, &_context->instance) != VK_SUCCESS) {
        free(_context);
        return nil;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(_context->instance, &deviceCount, NULL);
    if (deviceCount == 0) {
        vkDestroyInstance(_context->instance, NULL);
        free(_context);
        return nil;
    }

    VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(_context->instance, &deviceCount, devices);

    _context->physicalDevice = devices[0];
    for (uint32_t i = 0; i < deviceCount; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            _context->physicalDevice = devices[i];
            break;
        }
    }
    free(devices);

    VkPhysicalDeviceProperties activeProperties;
    vkGetPhysicalDeviceProperties(_context->physicalDevice, &activeProperties);
    vkGetPhysicalDeviceMemoryProperties(_context->physicalDevice, &_memProps);
    _deviceName = [[NSString alloc] initWithUTF8String:activeProperties.deviceName];

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(_context->physicalDevice, &queueFamilyCount, NULL);
    VkQueueFamilyProperties *queueFamilies = malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(_context->physicalDevice, &queueFamilyCount, queueFamilies);

    _context->queueFamilyIndex = UINT32_MAX;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            _context->queueFamilyIndex = i;
            break;
        }
    }
    free(queueFamilies);

    if (_context->queueFamilyIndex == UINT32_MAX) {
        [_deviceName release];
        vkDestroyInstance(_context->instance, NULL);
        free(_context);
        return nil;
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = _context->queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.enabledExtensionCount = 1;
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;

    if (vkCreateDevice(_context->physicalDevice, &deviceCreateInfo, NULL, &_context->device) != VK_SUCCESS) {
        [_deviceName release];
        vkDestroyInstance(_context->instance, NULL);
        free(_context);
        return nil;
    }

    vkGetDeviceQueue(_context->device, _context->queueFamilyIndex, 0, &_context->graphicsQueue);
    return self;
}

- (NSString *)name {
    return _deviceName;
}

- (id<MTLCommandQueue>)newCommandQueue {
    return (id<MTLCommandQueue>)[[MyMTLCommandQueue alloc] initWithDevice:self queueFamilyIndex:_context->queueFamilyIndex vulkanDevice:_context->device];
}

- (id<MTLCommandQueue>)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)maxCount {
    return [self newCommandQueue];
}

- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(NSUInteger)options {
    return (id<MTLBuffer>)[[MyMTLBuffer alloc] initWithDevice:self length:length bytes:NULL vulkanDevice:_context->device memoryProperties:_memProps];
}

- (id<MTLBuffer>)newBufferWithBytes:(const void *)pointer length:(NSUInteger)length options:(NSUInteger)options {
    return (id<MTLBuffer>)[[MyMTLBuffer alloc] initWithDevice:self length:length bytes:pointer vulkanDevice:_context->device memoryProperties:_memProps];
}

- (id<MTLTexture>)newTextureWithDescriptor:(id)descriptor {
    return (id<MTLTexture>)[[MyMTLTexture alloc] initWithDevice:self vulkanDevice:_context->device memoryProperties:_memProps];
}

- (id)newLibraryWithSource:(NSString *)source options:(id)options error:(NSError **)error { return nil; }
- (id)newDefaultLibrary { return nil; }
- (id)newRenderPipelineStateWithDescriptor:(id)descriptor error:(NSError **)error { return nil; }
- (id)newComputePipelineStateWithDescriptor:(id)descriptor error:(NSError **)error { return nil; }

- (BOOL)supportsFamily:(NSInteger)family {
    return (family == 1 || family == 2 || family == 3 || family == 1001);
}

- (BOOL)supportsFeatureSet:(NSUInteger)featureSet {
    return (featureSet == 1 || featureSet == 2);
}

- (void)dealloc {
    if (_context) {
        if (_context->device) {
            vkDeviceWaitIdle(_context->device);
            vkDestroyDevice(_context->device, NULL);
        }
        if (_context->instance) {
            vkDestroyInstance(_context->instance, NULL);
        }
        free(_context);
    }
    if (_deviceName) [_deviceName release];
    [super dealloc];
}
@end

id<MTLDevice> MTLCreateSystemDefaultDevice(void) {
    return [[MyMTLDevice alloc] init];
}
