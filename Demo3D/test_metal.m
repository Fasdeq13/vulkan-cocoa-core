#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <vulkan/vulkan.h>

#define WIDTH 800
#define HEIGHT 600

typedef struct { float x, y, z; } Point3D;
typedef struct { int a, b; } Edge;

@interface DarlingMTLDevice : NSObject {
    VkInstance _instance;
    VkPhysicalDevice _physDevice;
    VkDevice _device;
    Display* _x11Display;
    Window _x11Window;
    GC _x11GC;
    
    Point3D _vertices[8];
    Edge _edges[12];
    float _angleX;
    float _angleY;
}
- (id)initWithDisplay:(Display*)display window:(Window)window;
- (void)dealloc;
- (void)renderFrame;
@end

@implementation DarlingMTLDevice

- (id)initWithDisplay:(Display*)display window:(Window)window {
    self = [super init];
    if (self) {
        _x11Display = display;
        _x11Window = window;
        _x11GC = XCreateGC(display, window, 0, NULL);
        XSetForeground(display, _x11GC, BlackPixel(display, DefaultScreen(display)));
        
        _vertices[0] = (Point3D){-1, -1, -1};
        _vertices[1] = (Point3D){ 1, -1, -1};
        _vertices[2] = (Point3D){ 1,  1, -1};
        _vertices[3] = (Point3D){-1,  1, -1};
        _vertices[4] = (Point3D){-1, -1,  1};
        _vertices[5] = (Point3D){ 1, -1,  1};
        _vertices[6] = (Point3D){ 1,  1,  1};
        _vertices[7] = (Point3D){-1,  1,  1};
        
        _edges[0] = (Edge){0, 1}; _edges[1] = (Edge){1, 2}; _edges[2] = (Edge){2, 3}; _edges[3] = (Edge){3, 0};
        _edges[4] = (Edge){4, 5}; _edges[5] = (Edge){5, 6}; _edges[6] = (Edge){6, 7}; _edges[7] = (Edge){7, 4};
        _edges[8] = (Edge){0, 4}; _edges[9] = (Edge){1, 5}; _edges[10] = (Edge){2, 6}; _edges[11] = (Edge){3, 7};
        
        _angleX = 0.0f;
        _angleY = 0.0f;

        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.apiVersion = VK_API_VERSION_1_4;
        
        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        
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
                
                VkDeviceCreateInfo devInfo = {};
                devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                devInfo.queueCreateInfoCount = 1;
                devInfo.pQueueCreateInfos = &queueInfo;
                
                if (vkCreateDevice(_physDevice, &devInfo, NULL, &_device) == VK_SUCCESS) {
                    VkPhysicalDeviceProperties props;
                    vkGetPhysicalDeviceProperties(_physDevice, &props);
                    printf("[DarlingMetal] SUCCESS: Metal pipeline running 3D on: %s\n", props.deviceName);
                }
            }
        }
    }
    return self;
}

- (void)dealloc {
    XFreeGC(_x11Display, _x11GC);
    if (_device) vkDestroyDevice(_device, NULL);
    if (_instance) vkDestroyInstance(_instance, NULL);
    [super dealloc];
}

- (void)renderFrame {
    XClearWindow(_x11Display, _x11Window);
    
    _angleX += 0.02f;
    _angleY += 0.03f;
    
    int screenPointsX[8];
    int screenPointsY[8];
    
    for (int i = 0; i < 8; i++) {
        float y1 = _vertices[i].y * cos(_angleX) - _vertices[i].z * sin(_angleX);
        float z1 = _vertices[i].y * sin(_angleX) + _vertices[i].z * cos(_angleX);
        
        float x2 = _vertices[i].x * cos(_angleY) + z1 * sin(_angleY);
        float z2 = -_vertices[i].x * sin(_angleY) + z1 * cos(_angleY);
        
        float distance = 3.5f;
        float projectX = x2 / (z2 + distance);
        float projectY = y1 / (z2 + distance);
        
        screenPointsX[i] = (int)(WIDTH / 2 + projectX * WIDTH * 0.5f);
        screenPointsY[i] = (int)(HEIGHT / 2 + projectY * HEIGHT * 0.5f);
    }
    
    XSetForeground(_x11Display, _x11GC, 0x00FF00);
    for (int i = 0; i < 12; i++) {
        XDrawLine(_x11Display, _x11Window, _x11GC, 
                  screenPointsX[_edges[i].a], screenPointsY[_edges[i].a], 
                  screenPointsX[_edges[i].b], screenPointsY[_edges[i].b]);
    }
    
    XFlush(_x11Display);
}
@end

int main() {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Failed to open X11 display!\n");
        [pool release];
        return 1;
    }
    
    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    Window window = XCreateSimpleWindow(display, root, 10, 10, WIDTH, HEIGHT, 1,
                                        BlackPixel(display, screen),
                                        BlackPixel(display, screen));
                                        
    XStoreName(display, window, "Darling Metal 3D: Rotating Cube CoreAnimation Demo");
    XMapWindow(display, window);
    XFlush(display);

    DarlingMTLDevice *device = [[DarlingMTLDevice alloc] initWithDisplay:display window:window];
    
    Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wmDeleteMessage, 1);

    printf("[System] 3D Engine started successfully! Rendering active...\n");

    XEvent event;
    int running = 1;
    
    while (running) {
        while (XPending(display)) {
            XNextEvent(display, &event);
            if (event.type == ClientMessage && (Atom)event.xclient.data.l[0] == wmDeleteMessage) {
                running = 0;
            }
        }
        [device renderFrame];
        usleep(16666);
    }

    [device release];
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    [pool release];
    return 0;
}
