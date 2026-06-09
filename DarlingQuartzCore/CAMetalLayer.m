#import <Foundation/Foundation.h>

typedef struct {
    double width;
    double height;
} DarlingCGSize;

@interface CAMetalLayer : NSObject {
    id _device;
    unsigned long _pixelFormat;
    unsigned char _framebufferOnly;
    DarlingCGSize _drawableSize;
    unsigned char _presentsWithTransaction;
    id _colorspace;
    double _contentsScale;
}
@property (retain) id device;
@property unsigned long pixelFormat;
@property unsigned char framebufferOnly;
@property DarlingCGSize drawableSize;
@property unsigned char presentsWithTransaction;
@property (retain) id colorspace;
@property double contentsScale;
- (id)nextDrawable;
- (void)display;
- (void)setNeedsDisplay;
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

- (id)init {
    self = [super init];
    if (self) {
        _pixelFormat = 80;
        _framebufferOnly = 1;
        _drawableSize.width = 1920.0;
        _drawableSize.height = 1080.0;
        _contentsScale = 1.0;
        _presentsWithTransaction = 0;
    }
    return self;
}

- (void)dealloc {
    if (_device) {
        [_device release];
    }
    if (_colorspace) {
        [_colorspace release];
    }
    [super dealloc];
}

- (id)nextDrawable {
    return [[[DarlingCAMetalDrawable alloc] initWithLayer:self] autorelease];
}

- (void)display {}
- (void)setNeedsDisplay {}

@end
