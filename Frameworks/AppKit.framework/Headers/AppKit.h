#ifndef APPKIT_H
#define APPKIT_H

#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#import <AppServices/AppServices.h>

@class NSApplication;
@class NSWindow;
@class NSView;
@class NSEvent;
@class NSMenu;

typedef NS_ENUM(NSUInteger, NSApplicationActivationPolicy) {
    NSApplicationActivationPolicyRegular = 0,
        NSApplicationActivationPolicyAccessory = 1,
        NSApplicationActivationPolicyProhibited = 2
};

typedef NS_OPTIONS(NSUInteger, NSWindowStyleMask) {
    NSWindowStyleMaskBorderless = 0,
        NSWindowStyleMaskTitled = 1 << 0,
        NSWindowStyleMaskClosable = 1 << 1,
        NSWindowStyleMaskMiniaturizable = 1 << 2,
        NSWindowStyleMaskResizable = 1 << 3,
        NSWindowStyleMaskFullSizeContentView = 1 << 15
};

typedef NS_ENUM(NSUInteger, NSBackingStoreType) {
    NSBackingStoreBuffered = 2
};

@protocol NSApplicationDelegate <NSObject>
@optional
- (void)applicationDidFinishLaunching:(NSNotification*)notification;
-(BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender;
@end

@interface NSApplication : NSObject
@property(assign, nullable) id<NSApplicationDelegate> delegate;
@property(readonly, copy) NSArray<NSWindow*>* windows;
@property(readonly, getter = isActive) BOOL active;
@property(retain, nullable) NSMenu* mainMenu;

+(NSApplication*)sharedApplication;
-(void)run;
-(void)terminate:(nullable id)sender;
-(BOOL)setActivationPolicy:(NSApplicationActivationPolicy)activationPolicy;
@end

@interface NSView : NSObject
@property CGRect frame;
@property CGRect bounds;
@property(readonly, nullable) NSWindow* window;
@property(readonly, nullable) NSView* superview;
@property(copy) NSArray<NSView*>* subviews;
@property(retain, nullable) CALayer* layer;
@property BOOL wantsLayer;

-(instancetype)initWithFrame:(CGRect)frameRect;
-(void)addSubview:(NSView*)view;
-(void)removeFromSuperview;
-(void)setNeedsDisplay:(BOOL)flag;
-(void)drawRect:(CGRect)dirtyRect;
@end

@interface NSWindow : NSObject
@property CGRect frame;
@property(copy) NSString* title;
@property(retain, nullable) NSView* contentView;
@property(readonly, getter = isVisible) BOOL visible;
@property(readonly) BOOL isKeyWindow;

-(instancetype)initWithContentRect:(CGRect)contentRect styleMask : (NSWindowStyleMask)style backing : (NSBackingStoreType)bufferingType defer : (BOOL)flag;
-(void)makeKeyAndOrderFront:(nullable id)sender;
-(void)orderFront:(nullable id)sender;
-(void)orderOut:(nullable id)sender;
-(void)close;
-(CGPoint)convertPointFromScreen:(CGPoint)point;
-(CGPoint)convertPointToScreen:(CGPoint)point;
@end

@interface NSEvent : NSObject <NSCopying, NSSecureCoding>
@property(readonly) uint32_t type;
@property(readonly) CGPoint locationInWindow;
@property(readonly) NSUInteger modifierFlags;
@property(readonly, timestamp) NSTimeInterval timestamp;
@property(readonly, nullable) NSWindow* window;
@end

#ifdef __cplusplus
extern "C" {
#endif
    int NSApplicationMain(int argc, const char* _Nonnull argv[_Nonnull]);
#ifdef __cplusplus
}
#endif

#endif // APPKIT_H
