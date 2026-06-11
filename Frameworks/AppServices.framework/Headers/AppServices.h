#ifndef APPSERVICES_H
#define APPSERVICES_H

#import <Foundation/Foundation.h>

typedef struct CGPoint CGPoint;
typedef struct CGSize CGSize;
typedef struct CGRect CGRect;

typedef uint32_t CGGlyph;
typedef struct CGColor* CGColorRef;
typedef struct CGContext* CGContextRef;
typedef struct CGFont* CGFontRef;
typedef struct CGImage* CGImageRef;
typedef struct CGColorSpace* CGColorSpaceRef;

typedef const struct __CTFont* CTFontRef;
typedef const struct __CTLine* CTLineRef;
typedef const struct __CTRun* CTRunRef;

@interface NSColorSpace : NSObject <NSSecureCoding, NSCopying>
@property(readonly, copy) NSString* localizedName;
@property(readonly) NSColorSpaceModel colorSpaceModel;
@property(readonly, nullable) CGColorSpaceRef CGColorSpace;
+(NSColorSpace*)sRGBColorSpace;
+(NSColorSpace*)adobeRGB1998ColorSpace;
+(NSColorSpace*)displayP3ColorSpace;
@end

@interface NSFont : NSObject <NSCopying, NSSecureCoding>
@property(readonly, copy) NSString* fontName;
@property(readonly) CGFloat pointSize;
@property(readonly, copy) NSString* displayName;
@property(readonly) CTFontRef CTFont;
+(nullable NSFont*)fontWithName:(NSString*)fontName size : (CGFloat)fontSize;
+(NSFont*)systemFontOfSize:(CGFloat)fontSize;
@end

#ifdef __cplusplus
extern "C" {
#endif

	CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void);
	CGColorSpaceRef CGColorSpaceCreateDeviceGray(void);
	CGColorSpaceRef CGColorSpaceCreateWithName(CFStringRef name);
	void CGColorSpaceRelease(CGColorSpaceRef space);

	CGContextRef CGBitmapContextCreate(void* data, size_t width, size_t height, size_t bitsPerComponent, size_t bytesPerRow, CGColorSpaceRef space, uint32_t bitmapInfo);
	void CGContextRelease(CGContextRef c);
	void CGContextClearRect(CGContextRef c, CGRect rect);
	void CGContextSetRGBFillColor(CGContextRef c, CGFloat red, CGFloat green, CGFloat blue, CGFloat alpha);
	void CGContextFillRect(CGContextRef c, CGRect rect);

	CGFontRef CGFontCreateWithDataProvider(id provider);
	CGFontRef CGFontCreateWithFontName(CFStringRef name);
	void CGFontRelease(CGFontRef font);

#ifdef __cplusplus
}
#endif

#endif // APPSERVICES_H
