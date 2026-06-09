#import <Foundation/Foundation.h>

typedef struct { double x; double y; } DarlingCGPoint;
typedef struct { double width; double height; } DarlingCGSize;
typedef struct { DarlingCGPoint origin; DarlingCGSize size; } DarlingCGRect;

typedef const void* CTFontRef;
typedef const void* CTFontDescriptorRef;
typedef const void* CGContextRef;
typedef const void* CGColorRef;

@interface CTFontManager : NSObject
+ (unsigned char)registerFontsForURL:(NSURL *)fontURL scope:(unsigned int)scope error:(NSError **)error;
@end

@implementation CTFontManager
+ (unsigned char)registerFontsForURL:(NSURL *)fontURL scope:(unsigned int)scope error:(NSError **)error {
    return 1;
}
@end

@interface DarlingAppServicesDummy : NSObject
@end

@implementation DarlingAppServicesDummy
@end

CTFontRef CTFontCreateWithName(NSString *name, double size, const void *matrix) {
    return (CTFontRef)[[[DarlingAppServicesDummy alloc] init] autorelease];
}

CTFontDescriptorRef CTFontDescriptorCreateWithNameAndSize(NSString *name, double size) {
    return (CTFontDescriptorRef)[[[DarlingAppServicesDummy alloc] init] autorelease];
}

CGContextRef NSGraphicsContextCurrentContext(void) {
    return (CGContextRef)[[[DarlingAppServicesDummy alloc] init] autorelease];
}

void CGContextSetRGBFillColor(CGContextRef c, double red, double green, double blue, double alpha) {}
void CGContextFillRect(CGContextRef c, DarlingCGRect rect) {}

CGColorRef CGColorCreateGenericRGB(double red, double green, double blue, double alpha) {
    return (CGColorRef)[[[DarlingAppServicesDummy alloc] init] autorelease];
}

void CGColorRelease(CGColorRef color) {
    if (color) {
        [(id)color release];
    }
}
