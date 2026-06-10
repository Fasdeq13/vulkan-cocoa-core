#import <Foundation/Foundation.h>

#if !defined(__APPLE__) && !defined(__MACH__)
#include <X11/Xlib.h>
#endif

typedef struct {
    double x;
    double y;
    double width;
    double height;
} DarlingNSRect;

@protocol MySwapchainBridgeExport <NSObject>
- (void)setNativeWindowHandle:(void*)window;
- (void)setNativeDisplayHandle:(void*)display;
- (void)setDevice:(id)device;
- (BOOL)initializeVulkanSwapchain;
@end

@interface NSView : NSObject {
    DarlingNSRect _frame;
    id _layer;
    unsigned char _wantsLayer;
}
@property DarlingNSRect frame;
@property (retain) id layer;
@property unsigned char wantsLayer;
- (void)addSubview:(NSView *)view;
- (void)setNeedsDisplay:(unsigned char)flag;
@end

@implementation NSView
@synthesize frame = _frame;
@synthesize layer = _layer;
@synthesize wantsLayer = _wantsLayer;
- (id)init {
    self = [super init];
    if (self) {
        _frame.x = 0; _frame.y = 0; _frame.width = 1920; _frame.height = 1080;
        _wantsLayer = 1;
    }
    return self;
}
- (void)dealloc {
    if (_layer) [_layer release];
    [super dealloc];
}
- (void)addSubview:(NSView *)view {}
- (void)setNeedsDisplay:(unsigned char)flag {}
@end

@interface NSWindow : NSObject {
    NSView* _contentView;
    unsigned long _styleMask;
    void* _xDisplay;
    unsigned long _xWindow;
}
@property (retain) NSView *contentView;
@property unsigned long styleMask;
- (id)initWithContentRect:(DarlingNSRect)contentRect styleMask:(unsigned long)windowStyle backing:(unsigned long)bufferingType defer:(unsigned char)flag;
- (void)makeKeyAndOrderFront:(id)sender;
- (void)setTitle:(NSString *)title;
@end

@implementation NSWindow
@synthesize contentView = _contentView;
@synthesize styleMask = _styleMask;

- (id)initWithContentRect:(DarlingNSRect)contentRect styleMask:(unsigned long)windowStyle backing:(unsigned long)bufferingType defer:(unsigned char)flag {
    self = [super init];
    if (self) {
        _contentView = [[NSView alloc] init];
        _styleMask = windowStyle;
        _xDisplay = NULL;
        _xWindow = 0;

#if !defined(__APPLE__) && !defined(__MACH__)
        _xDisplay = XOpenDisplay(NULL);
        if (_xDisplay) {
            int screen = DefaultScreen(_xDisplay);
            _xWindow = XCreateSimpleWindow((Display*)_xDisplay, RootWindow((Display*)_xDisplay, screen), (int)contentRect.x, (int)contentRect.y, (unsigned int)contentRect.width, (unsigned int)contentRect.height, 1, BlackPixel(_xDisplay, screen), WhitePixel(_xDisplay, screen));
            XSelectInput((Display*)_xDisplay, _xWindow, ExposureMask | KeyPressMask);
            XMapWindow((Display*)_xDisplay, _xWindow);
        }
#endif
    }
    return self;
}

- (void)makeKeyAndOrderFront:(id)sender {
    id metalDevice = [NSClassFromString(@"MyMTLDevice") alloc];
    if ([metalDevice respondsToSelector:@selector(init)]) {
        metalDevice = [metalDevice init];
    }

    id metalLayer = [NSClassFromString(@"MyCAMetalLayer") alloc];
    if ([metalLayer respondsToSelector:@selector(init)]) {
        metalLayer = [metalLayer init];
    }

    if (metalLayer && metalDevice) {
        if ([metalLayer respondsToSelector:@selector(setDevice:)]) {
            [metalLayer performSelector:@selector(setDevice:) withObject:metalDevice];
        }
        
        if (_xDisplay && _xWindow) {
            if ([metalLayer respondsToSelector:@selector(setNativeDisplayHandle:)]) {
                [metalLayer performSelector:@selector(setNativeDisplayHandle:) withObject:_xDisplay];
            }
            if ([metalLayer respondsToSelector:@selector(setNativeWindowHandle:)]) {
                [metalLayer performSelector:@selector(setNativeWindowHandle:) withObject:(void*)_xWindow];
            }
        }

        if ([metalLayer respondsToSelector:@selector(initializeVulkanSwapchain)]) {
            [metalLayer performSelector:@selector(initializeVulkanSwapchain)];
        }
    }

    if (_contentView) {
        _contentView.wantsLayer = 1;
        _contentView.layer = metalLayer;
    }

    if (metalDevice) [metalDevice release];
}

- (void)setTitle:(NSString *)title {
#if !defined(__APPLE__) && !defined(__MACH__)
    if (_xDisplay && _xWindow && title) {
        XStoreName((Display*)_xDisplay, _xWindow, [title UTF8String]);
    }
#endif
}

- (void)dealloc {
    if (_contentView) [_contentView release];
#if !defined(__APPLE__) && !defined(__MACH__)
    if (_xDisplay) {
        XDestroyWindow((Display*)_xDisplay, _xWindow);
        XCloseDisplay((Display*)_xDisplay);
    }
#endif
    [super dealloc];
}
@end

@interface NSScreen : NSObject
+ (NSArray *)screens;
- (DarlingNSRect)frame;
@end

@implementation NSScreen
+ (NSArray *)screens {
    return [NSArray arrayWithObject:[[[NSScreen alloc] init] autorelease]];
}
- (DarlingNSRect)frame {
    DarlingNSRect r = {0, 0, 1920, 1080};
    return r;
}
@end

@interface NSApplication : NSObject {
    id _delegate;
}
@property (assign) id delegate;
+ (NSApplication *)sharedApplication;
- (void)run;
- (void)terminate:(id)sender;
@end

@implementation NSApplication
@synthesize delegate = _delegate;
+ (NSApplication *)sharedApplication {
    static NSApplication* shared = nil;
    if (!shared) { shared = [[NSApplication alloc] init]; }
    return shared;
}
- (void)run {
    while (YES) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.016]];
    }
}
- (void)terminate:(id)sender { exit(0); }
@end
