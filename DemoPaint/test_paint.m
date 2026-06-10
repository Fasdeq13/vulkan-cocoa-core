#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <vulkan/vulkan.h>

@interface DarlingMTLPaintWindow : NSObject {
    VkInstance _instance;
    VkPhysicalDevice _physDevice;
    VkDevice _device;
    Display* _display;
    Window _window;
    GC _gc;
    int _isDrawing;
    int _lastX;
    int _lastY;
    unsigned long _brushColor;
    int _winWidth;
    int _winHeight;
}
- (id)initWithDisplay:(Display*)display window:(Window)window;
- (void)dealloc;
- (void)drawUI;
- (void)handleButtonPressAtX:(int)x y:(int)y;
- (void)handleMouseMoveToX:(int)x y:(int)y;
- (void)handleMouseUp;
- (void)clearCanvas;
- (void)updateSizeWithWidth:(int)width height:(int)height;
@end

@implementation DarlingMTLPaintWindow

- (id)initWithDisplay:(Display*)display window:(Window)window {
    self = [super init];
    if (self) {
        _display = display;
        _window = window;
        _gc = XCreateGC(display, window, 0, NULL);
        XSetLineAttributes(display, _gc, 5, LineSolid, CapRound, JoinRound);
        _isDrawing = 0;
        _brushColor = 0x00FF00;
        _winWidth = 800;
        _winHeight = 600;

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
                    printf("[DarlingPaint] SUCCESS: Vulkan 1.4 active on: %s\n", props.deviceName);
                }
            }
        }
    }
    return self;
}

- (void)dealloc {
    XFreeGC(_display, _gc);
    if (_device) vkDestroyDevice(_device, NULL);
    if (_instance) vkDestroyInstance(_instance, NULL);
    [super dealloc];
}

- (void)updateSizeWithWidth:(int)width height:(int)height {
    _winWidth = width;
    _winHeight = height;
}

- (void)drawUI {
    int panelY = _winHeight - 70;
    XSetForeground(_display, _gc, 0x333333);
    XFillRectangle(_display, _window, _gc, 0, panelY, _winWidth, 70);
    XSetForeground(_display, _gc, 0x00FF00);
    XFillRectangle(_display, _window, _gc, 10, panelY + 15, 100, 40);
    XSetForeground(_display, _gc, 0x000000);
    XDrawString(_display, _window, _gc, 40, panelY + 38, "GREEN", 5);
    XSetForeground(_display, _gc, 0xFF0000);
    XFillRectangle(_display, _window, _gc, 120, panelY + 15, 100, 40);
    XSetForeground(_display, _gc, 0xFFFFFF);
    XDrawString(_display, _window, _gc, 155, panelY + 38, "RED", 3);
    XSetForeground(_display, _gc, 0x555555);
    XFillRectangle(_display, _window, _gc, _winWidth - 110, panelY + 15, 100, 40);
    XSetForeground(_display, _gc, 0xFFFFFF);
    XDrawString(_display, _window, _gc, _winWidth - 78, panelY + 38, "CLEAR", 5);
    XFlush(_display);
}

- (void)handleButtonPressAtX:(int)x y:(int)y {
    int panelY = _winHeight - 70;
    if (y >= panelY + 15 && y <= panelY + 55) {
        if (x >= 10 && x <= 110) {
            _brushColor = 0x00FF00;
            printf("[Paint] Active brush color: GREEN\n");
        } else if (x >= 120 && x <= 220) {
            _brushColor = 0xFF0000;
            printf("[Paint] Active brush color: RED\n");
        } else if (x >= _winWidth - 110 && x <= _winWidth - 10) {
            [self clearCanvas];
            printf("[Paint] Event: Canvas cleared via UI\n");
        }
        return;
    }
    if (y < panelY) {
        _isDrawing = 1;
        _lastX = x;
        _lastY = y;
    }
}

- (void)handleMouseMoveToX:(int)x y:(int)y {
    int panelY = _winHeight - 70;
    if (_isDrawing && y < panelY) {
        XSetForeground(_display, _gc, _brushColor);
        XDrawLine(_display, _window, _gc, _lastX, _lastY, x, y);
        XFlush(_display);
        _lastX = x;
        _lastY = y;
    }
}

- (void)handleMouseUp {
    _isDrawing = 0;
}

- (void)clearCanvas {
    XSetForeground(_display, _gc, 0x000000);
    XFillRectangle(_display, _window, _gc, 0, 0, _winWidth, _winHeight - 70);
    [self drawUI];
}
@end

int main() {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Failed to open X11 display context!\n");
        [pool release];
        return 1;
    }
    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    Window window = XCreateSimpleWindow(display, root, 10, 10, 800, 600, 1, 0x000000, 0x000000);
    XStoreName(display, window, "Darling Metal Paint: Dynamic UI Demo");
    XSelectInput(display, window, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask);
    XMapWindow(display, window);
    XFlush(display);
    DarlingMTLPaintWindow *paintWindow = [[DarlingMTLPaintWindow alloc] initWithDisplay:display window:window];
    Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wmDeleteMessage, 1);
    printf("[System] Metal Paint engine initialized successfully.\n");
    XEvent event;
    int running = 1;
    while (running) {
        XNextEvent(display, &event);
        switch (event.type) {
            case Expose:
                [paintWindow drawUI];
                break;
            case ConfigureNotify:
                [paintWindow updateSizeWithWidth:event.xconfigure.width height:event.xconfigure.height];
                [paintWindow drawUI];
                break;
            case ButtonPress:
                if (event.xbutton.button == Button1) {
                    [paintWindow handleButtonPressAtX:event.xbutton.x y:event.xbutton.y];
                }
                break;
            case MotionNotify:
                [paintWindow handleMouseMoveToX:event.xmotion.x y:event.xmotion.y];
                break;
            case ButtonRelease:
                if (event.xbutton.button == Button1) {
                    [paintWindow handleMouseUp];
                }
                break;
            case ClientMessage:
                if (event.xclient.data.l[0] == (long)wmDeleteMessage) {
                    running = 0;
                }
                break;
        }
    }
    [paintWindow release];
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    [pool release];
    return 0;
}
