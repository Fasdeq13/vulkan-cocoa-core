#ifndef Metal_h
#define Metal_h
#import <Foundation/Foundation.h>
typedef struct { unsigned long x, y, z; } MTLSize;
static inline MTLSize MTLSizeMake(unsigned long w, unsigned long h, unsigned long d) { MTLSize s = {w, h, d}; return s; }
typedef struct { float x, y; } MTLSamplePosition;
typedef struct { unsigned long a, s, b; } MTLAccelerationStructureSizes;
typedef unsigned long MTLGPUFamily; typedef unsigned long MTLFeatureSet; typedef unsigned long MTLResourceOptions;
typedef unsigned long MTLPipelineOption; typedef unsigned long MTLPurgeableState; typedef unsigned long MTLLibraryType;
typedef unsigned long MTLDeviceLocation; typedef unsigned long MTLReadWriteTextureTier; typedef unsigned long MTLArgumentBuffersTier;
typedef unsigned long long MTLTimestamp; typedef void* IOSurfaceRef;
enum { MTLReadWriteTextureTier2 = 2, MTLArgumentBuffersTier2 = 2, MTLDeviceLocationBuiltIn = 0, MTLPurgeableStateKeepCurrent = 1, MTLLibraryTypeExecutable = 0 };
@class MTLCommandBufferDescriptor, MTLRenderPipelineDescriptor, MTLComputePipelineDescriptor, MTLTextureDescriptor, MTLSamplerDescriptor, MTLDepthStencilDescriptor, MTLSharedTextureHandle, MTLSharedEventHandle, MTLHeapDescriptor, MTLArgumentDescriptor, MTLRasterizationRateMapDescriptor, MTLIndirectCommandBufferDescriptor, MTLAccelerationStructureDescriptor, MTLCounterSampleBufferDescriptor, MTLStitchedLibraryDescriptor, MTLFunctionConstantValues, MTLCompileOptions, MTLComputePipelineReflection, MTLAutoreleasedComputePipelineReflection, MTLCommandQueueDescriptor, MTLFunctionDescriptor, MTLIntersectionFunctionDescriptor;
typedef void* dispatch_data_t;
@protocol MTLDevice, MTLCommandQueue, MTLCommandBuffer, MTLBuffer, MTLTexture, MTLLibrary, MTLFunction, MTLRenderPipelineState, MTLComputePipelineState, MTLHeap, MTLSamplerState, MTLDepthStencilState, MTLSharedEvent, MTLEvent, MTLArgumentEncoder, MTLRasterizationRateMap, MTLIndirectCommandBuffer, MTLAccelerationStructure, MTLCounterSampleBuffer, MTLDynamicLibrary;
@protocol MTLResource <NSObject> @property (readonly) id<MTLDevice> device; @property (copy) NSString *label; @end
@protocol MTLBuffer <MTLResource> @property (readonly) unsigned long length; - (void*)contents; - (void)didModifyRange:(NSRange)r; @end
@protocol MTLTexture <NSObject> @end
@protocol MTLCommandQueue <NSObject> @property (readonly) id<MTLDevice> device; @property (copy) NSString *label; - (id<MTLCommandBuffer>)commandBuffer; @end
@protocol MTLCommandBuffer <NSObject> @end
@protocol MTLLibrary <NSObject> @property (readonly) id<MTLDevice> device; @property (copy) NSString *label; @end
@protocol MTLFunction <NSObject> @end
@protocol MTLRenderPipelineState <NSObject> @property (readonly) id<MTLDevice> device; @property (readonly) NSString *label; @end
@protocol MTLComputePipelineState <NSObject> @property (readonly) id<MTLDevice> device; @property (readonly) NSString *label; @end
@protocol MTLHeap <NSObject> @end
@protocol MTLSamplerState <NSObject> @end
@protocol MTLDepthStencilState <NSObject> @end
@protocol MTLSharedEvent <NSObject> @end
@protocol MTLEvent <NSObject> @end
@protocol MTLArgumentEncoder <NSObject> @end
@protocol MTLRasterizationRateMap <NSObject> @end
@protocol MTLIndirectCommandBuffer <NSObject> @end
@protocol MTLAccelerationStructure <NSObject> @end
@protocol MTLCounterSampleBuffer <NSObject> @end
@protocol MTLDynamicLibrary <NSObject> @end
@protocol MTLDevice <NSObject>
@property (readonly) NSString *name; @property (readonly) unsigned long long registryID; @property (readonly) MTLSize maxThreadsPerThreadgroup;
@property (readonly, getter=isLowPower) BOOL lowPower; @property (readonly, getter=isHeadless) BOOL headless; @property (readonly, getter=isRemovable) BOOL removable;
@property (readonly) BOOL hasUnifiedMemory; @property (readonly) unsigned long long recommendedMaxWorkingSetSize; @property (readonly) unsigned long long locationId;
@property (readonly) MTLDeviceLocation location; @property (readonly) unsigned long long maxTransferRate; @property (readonly, getter=isDepth24Stencil8PixelFormatSupported) BOOL depth24Stencil8PixelFormatSupported;
@property (readonly) MTLReadWriteTextureTier readWriteTextureSupport; @property (readonly) MTLArgumentBuffersTier argumentBuffersSupport; @property (readonly) BOOL areRasterOrderGroupsSupported;
@property (readonly) BOOL supports32BitFloatFiltering; @property (readonly) BOOL supports32BitMSAA; @property (readonly) BOOL supportsQueryTextureSystemType; @property (readonly) MTLTimestamp currentTimestamp;
- (BOOL)supportsFamily:(MTLGPUFamily)g; - (BOOL)supportsFeatureSet:(MTLFeatureSet)f; - (id<MTLCommandQueue>)newCommandQueue; - (id<MTLCommandQueue>)newCommandQueueWithMaxCommandBufferCount:(unsigned long)c;
- (id<MTLBuffer>)newBufferWithLength:(unsigned long)l options:(MTLResourceOptions)o; - (id<MTLBuffer>)newBufferWithBytes:(const void *)p length:(unsigned long)l options:(MTLResourceOptions)o;
- (id<MTLBuffer>)newBufferWithBytesNoCopy:(void *)p length:(unsigned long)l options:(MTLResourceOptions)o deallocator:(void (^)(void *p, unsigned long l))d;
- (id<MTLLibrary>)newDefaultLibrary; - (id<MTLLibrary>)newDefaultLibraryWithBundle:(NSBundle *)b error:(NSError **)e; - (id<MTLLibrary>)newLibraryWithFile:(NSString *)f error:(NSError **)e;
- (id<MTLLibrary>)newLibraryWithURL:(NSURL *)u error:(NSError **)e; - (id<MTLLibrary>)newLibraryWithSource:(NSString *)s options:(MTLCompileOptions *)o error:(NSError **)e;
- (void)newLibraryWithSource:(NSString *)s options:(MTLCompileOptions *)o completionHandler:(void (^)(id<MTLLibrary> l, NSError *e))h; - (id<MTLLibrary>)newLibraryWithData:(dispatch_data_t)d error:(NSError **)e;
- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)d error:(NSError **)e;
- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)d completionHandler:(void (^)(id<MTLRenderPipelineState> s, NSError *e))h;
- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)f error:(NSError **)e;
- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)f options:(MTLPipelineOption)o reflection:(MTLAutoreleasedComputePipelineReflection *)r error:(NSError **)e;
- (void)newComputePipelineStateWithFunction:(id<MTLFunction>)f completionHandler:(void (^)(id<MTLComputePipelineState> s, NSError *e))h;
- (void)newComputePipelineStateWithFunction:(id<MTLFunction>)f options:(MTLPipelineOption)o completionHandler:(void (^)(id<MTLComputePipelineState> s, MTLComputePipelineReflection *r, NSError *e))h;
- (id<MTLComputePipelineState>)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)d options:(MTLPipelineOption)o reflection:(MTLAutoreleasedComputePipelineReflection *)r error:(NSError **)e;
- (void)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)d options:(MTLPipelineOption)o completionHandler:(void (^)(id<MTLComputePipelineState> s, MTLComputePipelineReflection *r, NSError *e))h;
- (id<MTLCommandQueue>)newCommandQueueWithDescriptor:(MTLCommandQueueDescriptor *)d; - (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)d;
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)d iosurface:(IOSurfaceRef)i plane:(unsigned long)p; - (id<MTLSamplerState>)newSamplerStateWithDescriptor:(MTLSamplerDescriptor *)d;
- (id<MTLDepthStencilState>)newDepthStencilStateWithDescriptor:(MTLDepthStencilDescriptor *)d; - (id<MTLTexture>)newSharedTextureWithDescriptor:(MTLTextureDescriptor *)d;
- (id<MTLTexture>)newSharedTextureWithHandle:(id)s; - (id<MTLSharedEvent>)newSharedEvent; - (id<MTLSharedEvent>)newSharedEventWithHandle:(id)s; - (id<MTLEvent>)newEvent;
- (id<MTLHeap>)newHeapWithDescriptor:(MTLHeapDescriptor *)d; - (void)getDefaultSamplePositions:(MTLSamplePosition *)p count:(unsigned long)c; - (id<MTLArgumentEncoder>)newArgumentEncoderWithArguments:(NSArray *)a;
- (BOOL)supportsRasterizationRateMapWithLayerCount:(unsigned long)l; - (id<MTLRasterizationRateMap>)newRasterizationRateMapWithDescriptor:(MTLRasterizationRateMapDescriptor *)d;
- (id<MTLIndirectCommandBuffer>)newIndirectCommandBufferWithDescriptor:(MTLIndirectCommandBufferDescriptor *)d maxCommandCount:(unsigned long)c options:(MTLResourceOptions)o;
- (MTLAccelerationStructureSizes)accelerationStructureSizesWithDescriptor:(id)d; - (id<MTLAccelerationStructure>)newAccelerationStructureWithDescriptor:(id)d;
- (id<MTLAccelerationStructure>)newAccelerationStructureWithLength:(unsigned long)l; - (BOOL)supportsVertexAmplificationCount:(unsigned long)c;
- (id<MTLCounterSampleBuffer>)newCounterSampleBufferWithDescriptor:(MTLCounterSampleBufferDescriptor *)d error:(NSError **)e;
- (void)sampleTimestamps:(MTLTimestamp *)c gpuTimestamp:(MTLTimestamp *)g; - (BOOL)supportsDynamicLibraries;
- (id<MTLDynamicLibrary>)newDynamicLibraryWithURL:(NSURL *)u error:(NSError **)e; - (id<MTLDynamicLibrary>)newDynamicLibrary:(id<MTLLibrary>)l error:(NSError **)e;
- (void)newLibraryWithStitchedDescriptor:(MTLStitchedLibraryDescriptor *)d completionHandler:(void (^)(id<MTLLibrary> l, NSError *e))h;
- (id<MTLLibrary>)newLibraryWithStitchedDescriptor:(MTLStitchedLibraryDescriptor *)d error:(NSError **)e;
@end
id<MTLDevice> MTLCreateSystemDefaultDevice(void);
#endif
