#import <Foundation/Foundation.h>
#include <vulkan/vulkan.h>

#if defined(__APPLE__) || defined(__MACH__)
#include <vulkan/vulkan_metal.h>
#else
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#endif

typedef struct {
    double width;
    double height;
} DarlingCGSize;

@protocol MyVulkanDeviceExport <NSObject>
- (VkDevice)vulkanDevice;
- (void*)vulkanInstance;
- (void*)vulkanPhysicalDevice;
@end

@interface CAMetalLayer : NSObject {
    id _device;
    unsigned long _pixelFormat;
    unsigned char _framebufferOnly;
    DarlingCGSize _drawableSize;
    unsigned char _presentsWithTransaction;
    id _colorspace;
    double _contentsScale;
    VkSurfaceKHR _surface;
    VkSwapchainKHR _swapchain;
    uint32_t _imageCount;
    VkImage* _swapchainImages;
}
@property (retain) id device;
@property unsigned long pixelFormat;
@property unsigned char framebufferOnly;
@property DarlingCGSize drawableSize;
@property unsigned char presentsWithTransaction;
@property (retain) id colorspace;
@property double contentsScale;
@property (nonatomic, assign) void* nativeWindowHandle;
@property (nonatomic, assign) void* nativeDisplayHandle;
- (id)nextDrawable;
- (void)display;
- (void)setNeedsDisplay;
- (BOOL)initializeVulkanSwapchain;
@end

@interface DarlingCAMetalDrawable : NSObject {
    id _texture;
    CAMetalLayer* _layer;
    unsigned long _drawableID;
}
- (id)initWithLayer:(CAMetalLayer*)layer;
- (id)texture;
- (CAMetalLayer*)layer;
- (void)present;
- (void)presentAtTime:(double)time;
@end

@implementation DarlingCAMetalDrawable

- (id)initWithLayer:(CAMetalLayer*)layer {
    self = [super init];
    if (self) {
        _layer = layer;
        _drawableID = 100001;
        _texture = [[NSClassFromString(@"DarlingMTLTexture") alloc] init];
    }
    return self;
}

- (void)dealloc {
    if (_texture) {
        [_texture release];
    }
    [super dealloc];
}

- (id)texture {
    if (!_texture) {
        _texture = [[NSClassFromString(@"DarlingMTLTexture") alloc] init];
    }
    return _texture;
}

- (CAMetalLayer*)layer {
    return _layer;
}

- (void)present {}
- (void)presentAtTime:(double)time {}

@end

@implementation CAMetalLayer

@synthesize device = _device;
@synthesize pixelFormat = _pixelFormat;
@synthesize framebufferOnly = _framebufferOnly;
@synthesize drawableSize = _drawableSize;
@synthesize presentsWithTransaction = _presentsWithTransaction;
@synthesize colorspace = _colorspace;
@synthesize contentsScale = _contentsScale;
@synthesize nativeWindowHandle = _nativeWindowHandle;
@synthesize nativeDisplayHandle = _nativeDisplayHandle;

- (id)init {
    self = [super init];
    if (self) {
        _pixelFormat = 80;
        _framebufferOnly = 1;
        _drawableSize.width = 1920.0;
        _drawableSize.height = 1080.0;
        _contentsScale = 1.0;
        _presentsWithTransaction = 0;
        _surface = VK_NULL_HANDLE;
        _swapchain = VK_NULL_HANDLE;
        _swapchainImages = NULL;
        _imageCount = 0;
        _nativeWindowHandle = NULL;
        _nativeDisplayHandle = NULL;
    }
    return self;
}

- (BOOL)initializeVulkanSwapchain {
    if (!self.device || ![_device respondsToSelector:@selector(vulkanDevice)]) {
        return NO;
    }

    id<MyVulkanDeviceExport> devExport = (id<MyVulkanDeviceExport>)self.device;
    VkDevice rawDevice = [devExport vulkanDevice];
    VkInstance inst = (VkInstance)[devExport vulkanInstance];
    VkPhysicalDevice phys = (VkPhysicalDevice)[devExport vulkanPhysicalDevice];

    if (!rawDevice || !inst || !phys) return NO;

#if defined(__APPLE__) || defined(__MACH__)
    VkMetalSurfaceCreateInfoEXT surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    surfaceInfo.pLayer = (const void*)self;
    if (vkCreateMetalSurfaceEXT(inst, &surfaceInfo, NULL, &_surface) != VK_SUCCESS) {
        return NO;
    }
#else
    if (!self.nativeWindowHandle || !self.nativeDisplayHandle) return NO;
    VkXlibSurfaceCreateInfoKHR surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.dpy = (Display*)self.nativeDisplayHandle;
    surfaceInfo.window = (Window)self.nativeWindowHandle;
    if (vkCreateXlibSurfaceKHR(inst, &surfaceInfo, NULL, &_surface) != VK_SUCCESS) {
        return NO;
    }
#endif

    VkSurfaceCapabilitiesKHR capabilities;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, _surface, &capabilities) != VK_SUCCESS) {
        return NO;
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, _surface, &formatCount, NULL);
    if (formatCount == 0) return NO;

    VkSurfaceFormatKHR* formats = malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, _surface, &formatCount, formats);
    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (uint32_t i = 0; i < formatCount; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = formats[i];
            break;
        }
    }
    free(formats);

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, _surface, &presentModeCount, NULL);
    if (presentModeCount == 0) return NO;

    VkPresentModeKHR* presentModes = malloc(sizeof(VkPresentModeKHR) * presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, _surface, &presentModeCount, presentModes);
    VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < presentModeCount; i++) {
        if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            chosenPresentMode = presentModes[i];
            break;
        } else if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            chosenPresentMode = presentModes[i];
        }
    }
    free(presentModes);

    VkExtent2D swapchainExtent = {(uint32_t)_drawableSize.width, (uint32_t)_drawableSize.height};
    if (capabilities.currentExtent.width != UINT32_MAX) {
        swapchainExtent = capabilities.currentExtent;
    }

    uint32_t desiredImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && desiredImageCount > capabilities.maxImageCount) {
        desiredImageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchainInfo = {};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = _surface;
    swapchainInfo.minImageCount = desiredImageCount;
    swapchainInfo.imageFormat = chosenFormat.format;
    swapchainInfo.imageColorSpace = chosenFormat.colorSpace;
    swapchainInfo.imageExtent = swapchainExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = chosenPresentMode;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(rawDevice, &swapchainInfo, NULL, &_swapchain) != VK_SUCCESS) {
        return NO;
    }

    vkGetSwapchainImagesKHR(rawDevice, _swapchain, &_imageCount, NULL);
    _swapchainImages = malloc(sizeof(VkImage) * _imageCount);
    vkGetSwapchainImagesKHR(rawDevice, _swapchain, &_imageCount, _swapchainImages);

    return YES;
}

- (id)nextDrawable {
    if (_swapchain == VK_NULL_HANDLE) {
        [self initializeVulkanSwapchain];
    }
    return [[[DarlingCAMetalDrawable alloc] initWithLayer:self] autorelease];
}

- (void)display {}
- (void)setNeedsDisplay {}

- (void)dealloc {
    if (_swapchainImages) {
        free(_swapchainImages);
    }
    if (_swapchain && _device && [_device respondsToSelector:@selector(vulkanDevice)]) {
        VkDevice rawDevice = [(id<MyVulkanDeviceExport>)_device vulkanDevice];
        if (rawDevice) {
            vkDestroySwapchainKHR(rawDevice, _swapchain, NULL);
        }
    }
    if (_surface && _device && [_device respondsToSelector:@selector(vulkanInstance)]) {
        VkInstance inst = (VkInstance)[(id<MyVulkanDeviceExport>)_device vulkanInstance];
        if (inst) {
            vkDestroySurfaceKHR(inst, _surface, NULL);
        }
    }
    if (_device) {
        [_device release];
    }
    if (_colorspace) {
        [_colorspace release];
    }
    [super dealloc];
}

@end
