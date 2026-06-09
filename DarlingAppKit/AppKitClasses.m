#import <Foundation/Foundation.h>

typedef struct {
    double x;
    double y;
    double width;
    double height;
} DarlingNSRect;

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
    }
    return self;
}
- (void)dealloc {
    if (_contentView) [_contentView release];
    [super dealloc];
}
- (void)makeKeyAndOrderFront:(id)sender {}
- (void)setTitle:(NSString *)title {}
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
- (void)run {}
- (void)terminate:(id)sender { exit(0); }
@end
