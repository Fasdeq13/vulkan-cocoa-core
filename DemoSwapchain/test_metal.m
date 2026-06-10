#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib.h>

@interface CAMetalLayer : NSObject {
    VkInstance _instance;
    VkPhysicalDevice _physDevice;
    VkDevice _device;
    VkSurfaceKHR _surface;
    VkSwapchainKHR _swapchain;
    Display* _display;
    Window _window;
}
- (id)initWithInstance:(VkInstance)instance 
          physDevice:(VkPhysicalDevice)physDevice 
              device:(VkDevice)device 
             display:(Display*)display 
              window:(Window)window;
- (void)dealloc;
- (void)initSwapchain;
@end

@implementation CAMetalLayer

- (id)initWithInstance:(VkInstance)instance 
          physDevice:(VkPhysicalDevice)physDevice 
              device:(VkDevice)device 
             display:(Display*)display 
              window:(Window)window {
    self = [super init];
    if (self) {
        _instance = instance;
        _physDevice = physDevice;
        _device = device;
        _display = display;
        _window = window;
        
        VkXlibSurfaceCreateInfoKHR surfaceInfo = {};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.dpy = _display;
        surfaceInfo.window = _window;
        
        if (vkCreateXlibSurfaceKHR(_instance, &surfaceInfo, NULL, &_surface) == VK_SUCCESS) {
            printf("[QuartzCore] CAMetalLayer: X11 Surface linkage... [OK]\n");
            [self initSwapchain];
        } else {
            printf("[QuartzCore] CAMetalLayer: Surface linkage... [FAILED]\n");
        }
    }
    return self;
}

- (void)initSwapchain {
    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = _surface;
    createInfo.minImageCount = 2;
    createInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    createInfo.imageExtent = (VkExtent2D){800, 600};
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;
    
    PFN_vkCreateSwapchainKHR myVkCreateSwapchainKHR = 
        (PFN_vkCreateSwapchainKHR)vkGetDeviceProcAddr(_device, "vkCreateSwapchainKHR");
        
    if (myVkCreateSwapchainKHR && myVkCreateSwapchainKHR(_device, &createInfo, NULL, &_swapchain) == VK_SUCCESS) {
        printf("[QuartzCore] CAMetalLayer: Swapchain double-buffering... [OK]\n");
    } else {
        printf("[QuartzCore] CAMetalLayer: Swapchain allocation... [FAILED]\n");
    }
}

- (void)dealloc {
    PFN_vkDestroySwapchainKHR myVkDestroySwapchainKHR = 
        (PFN_vkDestroySwapchainKHR)vkGetDeviceProcAddr(_device, "vkDestroySwapchainKHR");
    if (myVkDestroySwapchainKHR && _swapchain) {
        myVkDestroySwapchainKHR(_device, _swapchain, NULL);
    }
    
    PFN_vkDestroySurfaceKHR myVkDestroySurfaceKHR = 
        (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr(_instance, "vkDestroySurfaceKHR");
    if (myVkDestroySurfaceKHR && _surface) {
        myVkDestroySurfaceKHR(_instance, _surface, NULL);
    }
    [super dealloc];
}
@end

@interface DarlingMTLDevice : NSObject {
    VkInstance _instance;
    VkPhysicalDevice _physDevice;
    VkDevice _device;
}
- (id)init;
- (void)dealloc;
- (VkInstance)instance;
- (VkPhysicalDevice)physDevice;
- (VkDevice)device;
@end

@implementation DarlingMTLDevice
- (id)init {
    self = [super init];
    if (self) {
        const char* extensions[] = { "VK_KHR_surface", "VK_KHR_xlib_surface" };
        
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.apiVersion = VK_API_VERSION_1_4;
        
        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = 2;
        createInfo.ppEnabledExtensionNames = extensions;
        
        if (vkCreateInstance(&createInfo, NULL, &_instance) == VK_SUCCESS) {
            uint32_t count = 0;
            vkEnumeratePhysicalDevices(_instance, &count, NULL);
            if (count > 0) {
                VkPhysicalDevice* devices = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * count);
                vkEnumeratePhysicalDevices(_instance, &count, devices);
                _physDevice = devices[0];
                free(devices);
                
                float priority = 1.0f;
                VkDeviceQueueCreateInfo queueInfo = {};
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &priority;
                
                const char* devExtensions[] = { "VK_KHR_swapchain" };
                
                VkDeviceCreateInfo devInfo = {};
                devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                devInfo.queueCreateInfoCount = 1;
                devInfo.pQueueCreateInfos = &queueInfo;
                devInfo.enabledExtensionCount = 1;
                devInfo.ppEnabledExtensionNames = devExtensions;
                
                if (vkCreateDevice(_physDevice, &devInfo, NULL, &_device) == VK_SUCCESS) {
                    VkPhysicalDeviceProperties props;
                    vkGetPhysicalDeviceProperties(_physDevice, &props);
                    printf("[DarlingMetal] Core: Device initialization complete... [OK]\n");
                    printf("[DarlingMetal] Core: Hardware GPU target: %s\n", props.deviceName);
                }
            }
        }
    }
    return self;
}
- (void)dealloc {
    if (_device) vkDestroyDevice(_device, NULL);
    if (_instance) vkDestroyInstance(_instance, NULL);
    [super dealloc];
}
- (VkInstance)instance { return _instance; }
- (VkPhysicalDevice)physDevice { return _physDevice; }
- (VkDevice)device { return _device; }
@end

int main() {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "[System] ERROR: Failed to open X11 display context.\n");
        [pool release];
        return 1;
    }
    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    Window window = XCreateSimpleWindow(display, root, 10, 10, 800, 600, 1, 0, 0);
    
    XSelectInput(display, window, ExposureMask | StructureNotifyMask | ButtonPressMask);
    XStoreName(display, window, "Darling Metal: CoreAnimation Swapchain Test");
    XMapWindow(display, window);
    XFlush(display);

    printf("[System] Launching hardware backend execution...\n");
    DarlingMTLDevice *device = [[DarlingMTLDevice alloc] init];
    
    CAMetalLayer *metalLayer = [[CAMetalLayer alloc] initWithInstance:[device instance] 
                                                           physDevice:[device physDevice] 
                                                               device:[device device] 
                                                              display:display 
                                                               window:window];

    Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wmDeleteMessage, 1);

    printf("[System] Initialization successful. Loop active.\n");

    XEvent event;
    int running = 1;
    while (running) {
        XNextEvent(display, &event);
        if (event.type == ClientMessage && event.xclient.data.l == (long)wmDeleteMessage) {
            running = 0;
        }
    }

    [metalLayer release];
    [device release];
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    [pool release];
    return 0;
}
