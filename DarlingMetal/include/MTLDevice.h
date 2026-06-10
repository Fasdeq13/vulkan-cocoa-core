#import <Foundation/Foundation.h>

@protocol MTLCommandQueue;
@protocol MTLBuffer;
@protocol MTLTexture;

@protocol MTLDevice <NSObject>

@property (readonly) NSString *name;

- (id<MTLCommandQueue>)newCommandQueue;
- (id<MTLCommandQueue>)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)maxCount;

- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(NSUInteger)options;
- (id<MTLBuffer>)newBufferWithBytes:(const void *)pointer length:(NSUInteger)length options:(NSUInteger)options;

- (id<MTLTexture>)newTextureWithDescriptor:(id)descriptor;

- (id)newLibraryWithSource:(NSString *)source options:(id)options error:(NSError **)error;
- (id)newDefaultLibrary;
- (id)newRenderPipelineStateWithDescriptor:(id)descriptor error:(NSError **)error;
- (id)newComputePipelineStateWithDescriptor:(id)descriptor error:(NSError **)error;

- (BOOL)supportsFamily:(NSInteger)family;
- (BOOL)supportsFeatureSet:(NSUInteger)featureSet;

@end

id<MTLDevice> MTLCreateSystemDefaultDevice(void);
