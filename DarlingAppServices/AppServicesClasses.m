#import <Foundation/Foundation.h>

typedef struct { double x; double y; } DarlingCGPoint;
typedef struct { double width; double height; } DarlingCGSize;
typedef struct { DarlingCGPoint origin; DarlingCGSize size; } DarlingCGRect;

typedef const void* CTFontRef;
typedef const void* CTFontDescriptorRef;
typedef const void* CGContextRef;
typedef const void* CGColorRef;
typedef const void* CGPathRef;

@interface CTFontManager : NSObject
+ (unsigned char)registerFontsForURL:(NSURL *)fontURL scope:(unsigned int)scope error:(NSError **)error;
@end

@implementation CTFontManager
+ (unsigned char)registerFontsForURL:(NSURL *)fontURL scope:(unsigned int)scope error:(NSError **)error {
    return 1;
}
@end

@interface DarlingCTFont : NSObject {
    NSString *_name;
    double _size;
}
- (instancetype)initWithName:(NSString *)name size:(double)size;
- (NSString *)fontName;
- (double)fontSize;
@end

@implementation DarlingCTFont
- (instancetype)initWithName:(NSString *)name size:(double)size {
    self = [super init];
    if (self) {
        _name = [name copy];
        _size = size;
    }
    return self;
}
- (NSString *)fontName { return _name; }
- (double)fontSize { return _size; }
- (void)dealloc {
    [_name release];
    [super dealloc];
}
@end

@interface MyCGColor : NSObject {
    @public double r, g, b, a;
}
@end
@implementation MyCGColor
@end

@interface MyCGContext : NSObject {
    @public uint8_t* _pixelBuffer;
    int _width, _height;
    double _currentR, _currentG, _currentB, _currentA;
    double _strokeR, _strokeG, _strokeB, _strokeA;
    double _lineWidth;
}
@end

@implementation MyCGContext
- (instancetype)init {
    self = [super init];
    if (self) {
        _width = 1920;
        _height = 1080;
        _pixelBuffer = calloc(_width * _height * 4, 1);
        _currentR = 0.0; _currentG = 0.0; _currentB = 0.0; _currentA = 1.0;
        _strokeR = 0.0; _strokeG = 0.0; _strokeB = 0.0; _strokeA = 1.0;
        _lineWidth = 1.0;
    }
    return self;
}
- (void)dealloc {
    if (_pixelBuffer) free(_pixelBuffer);
    [super dealloc];
}
@end

CTFontRef CTFontCreateWithName(NSString *name, double size, const void *matrix) {
    return (CTFontRef)[[DarlingCTFont alloc] initWithName:name size:size];
}

CTFontDescriptorRef CTFontDescriptorCreateWithNameAndSize(NSString *name, double size) {
    return (CTFontDescriptorRef)[[DarlingCTFont alloc] initWithName:name size:size];
}

CGContextRef NSGraphicsContextCurrentContext(void) {
    static MyCGContext* c = nil;
    if (!c) c = [[MyCGContext alloc] init];
    return (CGContextRef)c;
}

void CGContextSetRGBFillColor(CGContextRef c, double red, double green, double blue, double alpha) {
    MyCGContext* ctx = (MyCGContext*)c;
    ctx->_currentR = red; ctx->_currentG = green; ctx->_currentB = blue; ctx->_currentA = alpha;
}

void CGContextSetRGBStrokeColor(CGContextRef c, double red, double green, double blue, double alpha) {
    MyCGContext* ctx = (MyCGContext*)c;
    ctx->_strokeR = red; ctx->_strokeG = green; ctx->_strokeB = blue; ctx->_strokeA = alpha;
}

void CGContextSetLineWidth(CGContextRef c, double width) {
    MyCGContext* ctx = (MyCGContext*)c;
    ctx->_lineWidth = width;
}

void CGContextFillRect(CGContextRef c, DarlingCGRect rect) {
    MyCGContext* ctx = (MyCGContext*)c;
    int x1 = MAX(0, (int)rect.origin.x);
    int y1 = MAX(0, (int)rect.origin.y);
    int x2 = MIN(ctx->_width, x1 + (int)rect.size.width);
    int y2 = MIN(ctx->_height, y1 + (int)rect.size.height);
    uint8_t r = (uint8_t)(ctx->_currentR * 255.0);
    uint8_t g = (uint8_t)(ctx->_currentG * 255.0);
    uint8_t b = (uint8_t)(ctx->_currentB * 255.0);
    uint8_t a = (uint8_t)(ctx->_currentA * 255.0);
    for (int y = y1; y < y2; y++) {
        for (int x = x1; x < x2; x++) {
            int idx = (y * ctx->_width + x) * 4;
            ctx->_pixelBuffer[idx] = r;
            ctx->_pixelBuffer[idx+1] = g;
            ctx->_pixelBuffer[idx+2] = b;
            ctx->_pixelBuffer[idx+3] = a;
        }
    }
}

void CGContextStrokeRect(CGContextRef c, DarlingCGRect rect) {
    MyCGContext* ctx = (MyCGContext*)c;
    int x1 = MAX(0, (int)rect.origin.x);
    int y1 = MAX(0, (int)rect.origin.y);
    int x2 = MIN(ctx->_width, x1 + (int)rect.size.width);
    int y2 = MIN(ctx->_height, y1 + (int)rect.size.height);
    uint8_t r = (uint8_t)(ctx->_strokeR * 255.0);
    uint8_t g = (uint8_t)(ctx->_strokeG * 255.0);
    uint8_t b = (uint8_t)(ctx->_strokeB * 255.0);
    uint8_t a = (uint8_t)(ctx->_strokeA * 255.0);
    int thickness = MAX(1, (int)ctx->_lineWidth);
    for (int t = 0; t < thickness; t++) {
        for (int x = x1; x < x2; x++) {
            if (y1 + t < ctx->_height) {
                int idx1 = ((y1 + t) * ctx->_width + x) * 4;
                ctx->_pixelBuffer[idx1] = r; ctx->_pixelBuffer[idx1+1] = g; ctx->_pixelBuffer[idx1+2] = b; ctx->_pixelBuffer[idx1+3] = a;
            }
            if (y2 - 1 - t >= 0) {
                int idx2 = ((y2 - 1 - t) * ctx->_width + x) * 4;
                ctx->_pixelBuffer[idx2] = r; ctx->_pixelBuffer[idx2+1] = g; ctx->_pixelBuffer[idx2+2] = b; ctx->_pixelBuffer[idx2+3] = a;
            }
        }
        for (int y = y1; y < y2; y++) {
            if (x1 + t < ctx->_width) {
                int idx1 = (y * ctx->_width + (x1 + t)) * 4;
                ctx->_pixelBuffer[idx1] = r; ctx->_pixelBuffer[idx1+1] = g; ctx->_pixelBuffer[idx1+2] = b; ctx->_pixelBuffer[idx1+3] = a;
            }
            if (x2 - 1 - t >= 0) {
                int idx2 = (y * ctx->_width + (x2 - 1 - t)) * 4;
                ctx->_pixelBuffer[idx2] = r; ctx->_pixelBuffer[idx2+1] = g; ctx->_pixelBuffer[idx2+2] = b; ctx->_pixelBuffer[idx2+3] = a;
            }
        }
    }
}

void CGContextClearRect(CGContextRef c, DarlingCGRect rect) {
    MyCGContext* ctx = (MyCGContext*)c;
    int x1 = MAX(0, (int)rect.origin.x);
    int y1 = MAX(0, (int)rect.origin.y);
    int x2 = MIN(ctx->_width, x1 + (int)rect.size.width);
    int y2 = MIN(ctx->_height, y1 + (int)rect.size.height);
    for (int y = y1; y < y2; y++) {
        for (int x = x1; x < x2; x++) {
            int idx = (y * ctx->_width + x) * 4;
            ctx->_pixelBuffer[idx] = 0;
            ctx->_pixelBuffer[idx+1] = 0;
            ctx->_pixelBuffer[idx+2] = 0;
            ctx->_pixelBuffer[idx+3] = 0;
        }
    }
}

void CGContextFillEllipseInRect(CGContextRef c, DarlingCGRect rect) {
    MyCGContext* ctx = (MyCGContext*)c;
    double cx = rect.origin.x + rect.size.width / 2.0;
    double cy = rect.origin.y + rect.size.height / 2.0;
    double rx = rect.size.width / 2.0;
    double ry = rect.size.height / 2.0;
    if (rx <= 0.0 || ry <= 0.0) return;
    int x1 = MAX(0, (int)rect.origin.x);
    int y1 = MAX(0, (int)rect.origin.y);
    int x2 = MIN(ctx->_width, x1 + (int)rect.size.width);
    int y2 = MIN(ctx->_height, y1 + (int)rect.size.height);
    uint8_t r = (uint8_t)(ctx->_currentR * 255.0);
    uint8_t g = (uint8_t)(ctx->_currentG * 255.0);
    uint8_t b = (uint8_t)(ctx->_currentB * 255.0);
    uint8_t a = (uint8_t)(ctx->_currentA * 255.0);
    for (int y = y1; y < y2; y++) {
        for (int x = x1; x < x2; x++) {
            double dx = (x - cx) / rx;
            double dy = (y - cy) / ry;
            if ((dx * dx + dy * dy) <= 1.0) {
                int idx = (y * ctx->_width + x) * 4;
                ctx->_pixelBuffer[idx] = r;
                ctx->_pixelBuffer[idx+1] = g;
                ctx->_pixelBuffer[idx+2] = b;
                ctx->_pixelBuffer[idx+3] = a;
            }
        }
    }
}

CGColorRef CGColorCreateGenericRGB(double red, double green, double blue, double alpha) {
    MyCGColor *color = [[MyCGColor alloc] init];
    if (color) {
        color->r = red; color->g = green; color->b = blue; color->a = alpha;
    }
    return (CGColorRef)color;
}

void CGColorRelease(CGColorRef color) {
    if (color) {
        [(id)color release];
    }
}
