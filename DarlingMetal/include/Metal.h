#ifndef Metal_h
#define Metal_h

#import <Foundation/Foundation.h>

#define MTL_EXPORT __attribute__((visibility("default")))

typedef struct {
    NSUInteger x;
    NSUInteger y;
    NSUInteger z;
} MTLSize;

static inline MTLSize MTLSizeMake(NSUInteger width, NSUInteger height, NSUInteger depth) {
    MTLSize size = {width, height, depth};
    return size;
}

typedef struct {
    float x;
    float y;
} MTLSamplePosition;

typedef struct {
    NSUInteger accelerationStructureSize;
    NSUInteger scratchBufferSize;
    NSUInteger buildScratchBufferSize;
} MTLAccelerationStructureSizes;

typedef NSUInteger MTLGPUFamily;
typedef NSUInteger MTLFeatureSet;
typedef NSUInteger MTLResourceOptions;
typedef NSUInteger MTLPipelineOption;
typedef NSUInteger MTLCommandBufferHandler;
typedef NSUInteger MTLPurgeableState;
typedef NSUInteger MTLLibraryType;
typedef NSUInteger MTLDeviceLocation;
typedef NSUInteger MTLReadWriteTextureTier;
typedef NSUInteger MTLArgumentBuffersTier;
typedef uint64_t MTLTimestamp;
typedef void* IOSurfaceRef;

enum {
    MTLReadWriteTextureTier2 = 2,
    MTLArgumentBuffersTier2 = 2,
    MTLDeviceLocationBuiltIn = 0,
    MTLPurgeableStateKeepCurrent = 1,
    MTLLibraryTypeExecutable = 0
};

@class MTLCommandBufferDescriptor;
@class MTLFunctionDescriptor;
@class MTLIntersectionFunctionDescriptor;
@class MTLRenderPipelineDescriptor;
@class MTLComputePipelineDescriptor;
@class MTLTextureDescriptor;
@class MTLSamplerDescriptor;
@class MTLDepthStencilDescriptor;
@class MTLSharedTextureHandle;
@class MTLSharedEventHandle;
@class MTLHeapDescriptor;
@class MTLArgumentDescriptor;
@class MTLRasterizationRateMapDescriptor;
@class MTLIndirectCommandBufferDescriptor;
@class MTLAccelerationStructureDescriptor;
@class MTLCounterSampleBufferDescriptor;
@class MTLStitchedLibraryDescriptor;
@class MTLFunctionConstantValues;
@class MTLCompileOptions;
@class MTLComputePipelineReflection;
@class MTLAutoreleasedComputePipelineReflection;
@class MTLCommandQueueDescriptor;
typedef void* dispatch_data_t;
@class NSBundle;
@class NSError;
@class NSURL;
@class NSString;
@class NSArray;

@protocol MTLDevice;
@protocol MTLCommandBuffer;

@protocol MTLResource <NSObject>
@property (readonly) id<MTLDevice> device;
@property (copy) NSString *label;
@end

@protocol MTLBuffer <MTLResource>
@property (readonly) NSUInteger length;
- (void*)contents;
- (void)didModifyRange:(NSRange)range;
@end

@protocol MTLTexture <MTLResource>
@end

@protocol MTLCommandQueue <NSObject>
@property (readonly) id<MTLDevice> device;
@property (copy) NSString *label;
- (id<MTLCommandBuffer>)commandBuffer;
@end

@protocol MTLCommandBuffer <NSObject>
@end

@protocol MTLLibrary <NSObject>
@property (readonly) id<MTLDevice> device;
@property (copy) NSString *label;
@end

@protocol MTLFunction <NSObject>
@end

@protocol MTLRenderPipelineState <NSObject>
@property (readonly) id<MTLDevice> device;
@property (readonly) NSString *label;
@end

@protocol MTLComputePipelineState <NSObject>
@property (readonly) id<MTLDevice> device;
@property (readonly) NSString *label;
@end

@protocol MTLSamplerState <NSObject>
@end

@protocol MTLDepthStencilState <NSObject>
@end

@protocol MTLSharedEvent <NSObject>
@end

@protocol MTLEvent <NSObject>
@end

@protocol MTLHeap <NSObject>
@end

@protocol MTLArgumentEncoder <NSObject>
@end

@protocol MTLRasterizationRateMap <NSObject>
@end

@protocol MTLIndirectCommandBuffer <NSObject>
@end

@protocol MTLAccelerationStructure <NSObject>
@end

@protocol MTLCounterSampleBuffer <NSObject>
@end

@protocol MTLDynamicLibrary <NSObject>
@end

@protocol MTLDevice <NSObject>
@property (readonly) NSString *name;
@property (readonly) uint64_t registryID;
@property (readonly) MTLSize maxThreadsPerThreadgroup;
@property (readonly, getter=isLowPower) BOOL lowPower;
@property (readonly, getter=isHeadless) BOOL headless;
@property (readonly, getter=isRemovable) BOOL removable;
@property (readonly) BOOL hasUnifiedMemory;
@property (readonly) uint64_t recommendedMaxWorkingSetSize;
@property (readonly) uint64_t locationId;
@property (readonly) MTLDeviceLocation location;
@property (readonly) uint64_t maxTransferRate;
@property (readonly, getter=isDepth24Stencil8PixelFormatSupported) BOOL depth24Stencil8PixelFormatSupported;
@property (readonly) MTLReadWriteTextureTier readWriteTextureSupport;
@property (readonly) MTLArgumentBuffersTier argumentBuffersSupport;
@property (readonly) BOOL areRasterOrderGroupsSupported;
@property (readonly) BOOL supports32BitFloatFiltering;
@property (readonly) BOOL supports32BitMSAA;
@property (readonly) BOOL supportsQueryTextureSystemType;
@property (readonly) MTLTimestamp currentTimestamp;

- (BOOL)supportsFamily:(MTLGPUFamily)gpuFamily;
- (BOOL)supportsFeatureSet:(MTLFeatureSet)featureSet;
- (id<MTLCommandQueue>)newCommandQueue;
- (id<MTLCommandQueue>)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)maxCount;
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options;
- (id<MTLBuffer>)newBufferWithBytes:(const void *)pointer length:(NSUInteger)length options:(MTLResourceOptions)options;
- (id<MTLBuffer>)newBufferWithBytesNoCopy:(void *)pointer length:(NSUInteger)length options:(MTLResourceOptions)options deallocator:(void (^)(void *pointer, NSUInteger length))deallocator;
- (id<MTLLibrary>)newDefaultLibrary;
- (id<MTLLibrary>)newDefaultLibraryWithBundle:(NSBundle *)bundle error:(NSError **)error;
- (id<MTLLibrary>)newLibraryWithFile:(NSString *)filepath error:(NSError **)error;
- (id<MTLLibrary>)newLibraryWithURL:(NSURL *)url error:(NSError **)error;
- (id<MTLLibrary>)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options error:(NSError **)error;
- (void)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options completionHandler:(void (^)(id<MTLLibrary> library, NSError *error))completionHandler;
- (id<MTLLibrary>)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error;
- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor error:(NSError **)error;
- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor completionHandler:(void (^)(id<MTLRenderPipelineState> renderPipelineState, NSError *error))completionHandler;
- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction error:(NSError **)error;
- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction options:(MTLPipelineOption)options reflection:(MTLAutoreleasedComputePipelineReflection *)reflection error:(NSError **)error;
- (void)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction completionHandler:(void (^)(id<MTLComputePipelineState> computePipelineState, NSError *error))completionHandler;
- (void)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction options:(MTLPipelineOption)options completionHandler:(void (^)(id<MTLComputePipelineState> computePipelineState, MTLComputePipelineReflection *reflection, NSError *error))completionHandler;
- (id<MTLComputePipelineState>)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)descriptor options:(MTLPipelineOption)options reflection:(MTLAutoreleasedComputePipelineReflection *)reflection error:(NSError **)error;
- (void)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)descriptor options:(MTLPipelineOption)options completionHandler:(void (^)(id<MTLComputePipelineState> computePipelineState, MTLComputePipelineReflection *reflection, NSError *error))completionHandler;
- (id<MTLCommandQueue>)newCommandQueueWithDescriptor:(MTLCommandQueueDescriptor *)descriptor;
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor;
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor iosurface:(IOSurfaceRef)iosurface plane:(NSUInteger)plane;
- (id<MTLSamplerState>)newSamplerStateWithDescriptor:(MTLSamplerDescriptor *)descriptor;
- (id<MTLDepthStencilState>)newDepthStencilStateWithDescriptor:(MTLDepthStencilDescriptor *)descriptor;
- (id<MTLTexture>)newSharedTextureWithDescriptor:(MTLTextureDescriptor *)descriptor;
- (id<MTLTexture>)newSharedTextureWithHandle:(MTLSharedTextureHandle *)sharedHandle;
- (id<MTLSharedEvent>)newSharedEvent;
- (id<MTLSharedEvent>)newSharedEventWithHandle:(MTLSharedEventHandle *)sharedHandle;
- (id<MTLEvent>)newEvent;
- (id<MTLHeap>)newHeapWithDescriptor:(MTLHeapDescriptor *)descriptor;
- (void)getDefaultSamplePositions:(MTLSamplePosition *)positions count:(NSUInteger)count;
- (id<MTLArgumentEncoder>)newArgumentEncoderWithArguments:(NSArray<MTLArgumentDescriptor *> *)arguments;
- (BOOL)supportsRasterizationRateMapWithLayerCount:(NSUInteger)layerCount;
- (id<MTLRasterizationRateMap>)newRasterizationRateMapWithDescriptor:(MTLRasterizationRateMapDescriptor *)descriptor;
- (id<MTLIndirectCommandBuffer>)newIndirectCommandBufferWithDescriptor:(MTLIndirectCommandBufferDescriptor *)descriptor maxCommandCount:(NSUInteger)maxCount options:(MTLResourceOptions)options;
- (MTLAccelerationStructureSizes)accelerationStructureSizesWithDescriptor:(MTLAccelerationStructureDescriptor *)descriptor;
- (id<MTLAccelerationStructure>)newAccelerationStructureWithDescriptor:(MTLAccelerationStructureDescriptor *)descriptor;
- (id<MTLAccelerationStructure>)newAccelerationStructureWithLength:(NSUInteger)length;
- (BOOL)supportsVertexAmplificationCount:(NSUInteger)count;
- (id<MTLCounterSampleBuffer>)newCounterSampleBufferWithDescriptor:(MTLCounterSampleBufferDescriptor *)descriptor error:(NSError **)error;
- (void)sampleTimestamps:(MTLTimestamp *)cpuTimestamp gpuTimestamp:(MTLTimestamp *)gpuTimestamp;
- (BOOL)supportsDynamicLibraries;
- (id<MTLDynamicLibrary>)newDynamicLibraryWithURL:(NSURL *)url error:(NSError **)error;
- (id<MTLDynamicLibrary>)newDynamicLibrary:(id<MTLLibrary>)library error:(NSError **)error;
- (void)newLibraryWithStitchedDescriptor:(MTLStitchedLibraryDescriptor *)descriptor completionHandler:(void (^)(id<MTLLibrary> library, NSError *error))completionHandler;
- (id<MTLLibrary>)newLibraryWithStitchedDescriptor:(MTLStitchedLibraryDescriptor *)descriptor error:(NSError **)error;
@end

FOUNDATION_EXPORT id<MTLDevice> _Nullable MTLCreateSystemDefaultDevice(void);

#endif
