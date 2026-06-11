#ifndef QUARTZCORE_H
#define QUARTZCORE_H

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

@class CALayer;
@class CAAnimation;
@class CAMetalLayer;
@protocol CAMetalDrawable;

typedef struct CATransform3D {
    CGFloat m11, m12, m13, m14;
    CGFloat m21, m22, m23, m24;
    CGFloat m31, m32, m33, m34;
    CGFloat m41, m42, m43, m44;
} CATransform3D;

@protocol CAAction
- (void)runActionForKey:(NSString*)event object : (id)anObject arguments : (NSDictionary*)dict;
@end

@interface CALayer : NSObject <NSSecureCoding, CAMediaTiming>
@property(retain) id contents;
@property CGRect bounds;
@property CGPoint position;
@property CGFloat zPosition;
@property CGPoint anchorPoint;
@property CGFloat anchorPointZ;
@property CATransform3D transform;
@property CGRect frame;
@property(getter = isHidden) BOOL hidden;
@property(getter = isDoubleSided) BOOL doubleSided;
@property CGFloat cornerRadius;
@property CGFloat borderWidth;
@property(nullable) CGColorRef borderColor;
@property float opacity;
@property(nullable, copy) NSString* compositingFilter;
@property(nullable, copy) NSArray* filters;
@property(nullable, copy) NSArray* backgroundFilters;
@property BOOL masksToBounds;
@property(retain) id mask;
@property(getter = isOpaque) BOOL opaque;
@property(nullable, assign) id delegate;
@property(nullable, copy) NSDictionary* actions;
@property(nullable, readonly) CALayer* superlayer;
@property(nullable, copy) NSArray<CALayer*>* sublayers;

+(instancetype)layer;
-(instancetype)init;
-(instancetype)initWithLayer:(id)layer;
-(id)presentationLayer;
-(id)modelLayer;
-(void)addSublayer:(CALayer*)layer;
-(void)insertSublayer:(CALayer*)layer atIndex : (unsigned)idx;
-(void)insertSublayer:(CALayer*)layer below : (nullable CALayer*)sibling;
-(void)insertSublayer:(CALayer*)layer above : (nullable CALayer*)sibling;
-(void)removeFromSuperlayer;
-(void)replaceSublayer:(CALayer*)oldLayer with : (CALayer*)newLayer;
-(void)setNeedsDisplay;
-(void)setNeedsDisplayInRect:(CGRect)r;
-(BOOL)needsDisplay;
-(void)displayIfNeeded;
-(void)display;
-(void)drawInContext:(CGContextRef)ctx;
-(void)addAnimation:(CAAnimation*)anim forKey : (nullable NSString*)key;
-(void)removeAllAnimations;
-(void)removeAnimationForKey:(NSString*)key;
-(nullable NSArray<NSString*> *)animationKeys;
-(nullable CAAnimation*)animationForKey:(NSString*)key;
-(CGPoint)convertPoint:(CGPoint)p fromLayer : (nullable CALayer*)l;
-(CGPoint)convertPoint:(CGPoint)p toLayer : (nullable CALayer*)l;
-(CGRect)convertRect:(CGRect)r fromLayer : (nullable CALayer*)l;
-(CGRect)convertRect:(CGRect)r toLayer : (nullable CALayer*)l;
-(CFTimeInterval)convertTime:(CFTimeInterval)t fromLayer : (nullable CALayer*)l;
-(CFTimeInterval)convertTime:(CFTimeInterval)t toLayer : (nullable CALayer*)l;
-(nullable id<CAAction>)actionForKey:(NSString*)event;
@end

@interface CAMetalLayer : CALayer
@property(retain, nullable) id<MTLDevice> device;
@property(readonly, nullable) id<MTLCommandQueue> serverCommandQueue;
@property MTLPixelFormat pixelFormat;
@property BOOL framebufferOnly;
@property CGSize drawableSize;
@property NSUInteger maximumDrawableCount;
@property BOOL presentsWithTransaction;
@property(nullable) CGColorSpaceRef colorspace;
@property BOOL wantsExtendedDynamicRangeContent;
@property CAEdgeAntialiasingMask edgeAntialiasingMask;
@property BOOL allowsNextDrawableTimeout;

-(nullable id<CAMetalDrawable>)nextDrawable;
@end

@protocol CAMetalDrawable <MTLDrawable>
@property(readonly) id<MTLTexture> texture;
@property(readonly) CAMetalLayer* layer;
@end

@interface CAAnimation : NSObject <NSSecureCoding, NSCopying, CAMediaTiming>
@property(retain, nullable) CAMediaTimingFunction* timingFunction;
@property(retain, nullable) id delegate;
@property(getter = isRemovedOnCompletion) BOOL removedOnCompletion;
+(instancetype)animation;
@end

@interface CABasicAnimation : CAAnimation
@property(copy, nullable) NSString* keyPath;
@property(retain, nullable) id fromValue;
@property(retain, nullable) id toValue;
@property(retain, nullable) id byValue;
@end

#endif // QUARTZCORE_H
