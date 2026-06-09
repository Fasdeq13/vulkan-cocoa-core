#import <Foundation/Foundation.h>

@interface CALayer : NSObject {
    id _sublayers;
    id _superlayer;
    id _delegate;
    unsigned char _hidden;
    float _opacity;
}
@property (copy) NSArray *sublayers;
@property (readonly) CALayer *superlayer;
@property (assign) id delegate;
@property (getter=isHidden) unsigned char hidden;
@property float opacity;
- (void)addSublayer:(CALayer *)layer;
- (void)removeFromSuperlayer;
- (void)setNeedsLayout;
- (void)layoutIfNeeded;
@end

@implementation CALayer
@synthesize sublayers = _sublayers;
@synthesize superlayer = _superlayer;
@synthesize delegate = _delegate;
@synthesize hidden = _hidden;
@synthesize opacity = _opacity;

- (id)init {
    self = [super init];
    if (self) {
        _hidden = 0;
        _opacity = 1.0f;
    }
    return self;
}

- (void)dealloc {
    if (_sublayers) [_sublayers release];
    [super dealloc];
}

- (void)addSublayer:(CALayer *)layer {}
- (void)removeFromSuperlayer {}
- (void)setNeedsLayout {}
- (void)layoutIfNeeded {}
@end

@interface CATransaction : NSObject
+ (void)begin;
+ (void)commit;
+ (void)flush;
+ (void)setValue:(id)anObject forKey:(NSString *)key;
+ (id)valueForKey:(NSString *)key;
@end

@implementation CATransaction
+ (void)begin {}
+ (void)commit {}
+ (void)flush {}
+ (void)setValue:(id)anObject forKey:(NSString *)key {}
+ (id)valueForKey:(NSString *)key { return nil; }
@end
