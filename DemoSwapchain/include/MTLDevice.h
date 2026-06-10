#ifndef MTLDevice_h
#define MTLDevice_h

#import <Metal/Metal.h>

@interface DarlingMTLCommandQueue : NSObject <MTLCommandQueue>
@end

@interface DarlingMTLBuffer : NSObject <MTLBuffer>
- (instancetype)initWithLength:(NSUInteger)length;
@end

@interface DarlingMTLLibrary : NSObject <MTLLibrary>
@end

@interface DarlingMTLRenderPipelineState : NSObject <MTLRenderPipelineState>
@end

@interface DarlingMTLComputePipelineState : NSObject <MTLComputePipelineState>
@end

@interface DarlingMTLTexture : NSObject <MTLTexture>
@end

@interface DarlingMTLSamplerState : NSObject <MTLSamplerState>
@end

@interface DarlingMTLDepthStencilState : NSObject <MTLDepthStencilState>
@end

@interface DarlingMTLSharedEvent : NSObject <MTLSharedEvent>
@end

@interface DarlingMTLEvent : NSObject <MTLEvent>
@end

@interface DarlingMTLHeap : NSObject <MTLHeap>
@end

@interface DarlingMTLArgumentEncoder : NSObject <MTLArgumentEncoder>
@end

@interface DarlingMTLRasterizationRateMap : NSObject <MTLRasterizationRateMap>
@end

@interface DarlingMTLIndirectCommandBuffer : NSObject <MTLIndirectCommandBuffer>
@end

@interface DarlingMTLAccelerationStructure : NSObject <MTLAccelerationStructure>
@end

@interface DarlingMTLCounterSampleBuffer : NSObject <MTLCounterSampleBuffer>
@end

@interface DarlingMTLDynamicLibrary : NSObject <MTLDynamicLibrary>
@end

@interface DarlingMTLDevice : NSObject <MTLDevice>
@end

#endif
