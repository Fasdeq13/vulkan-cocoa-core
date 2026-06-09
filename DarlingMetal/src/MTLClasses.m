#import <Metal/Metal.h>

@interface DarlingMTLRenderPipelineState : NSObject <MTLRenderPipelineState>
@end
@implementation DarlingMTLRenderPipelineState
@synthesize device;
@synthesize label;
@end

@interface DarlingMTLComputePipelineState : NSObject <MTLComputePipelineState>
@end
@implementation DarlingMTLComputePipelineState
@synthesize device;
@synthesize label;
@end

@interface DarlingMTLTexture : NSObject <MTLTexture>
@end
@implementation DarlingMTLTexture
@synthesize device;
@synthesize label;
@end

@interface DarlingMTLSamplerState : NSObject <MTLSamplerState>
@end
@implementation DarlingMTLSamplerState
@end

@interface DarlingMTLDepthStencilState : NSObject <MTLDepthStencilState>
@end
@implementation DarlingMTLDepthStencilState
@end

@interface DarlingMTLSharedEvent : NSObject <MTLSharedEvent>
@end
@implementation DarlingMTLSharedEvent
@end

@interface DarlingMTLEvent : NSObject <MTLEvent>
@end
@implementation DarlingMTLEvent
@end

@interface DarlingMTLHeap : NSObject <MTLHeap>
@end
@implementation DarlingMTLHeap
@end

@interface DarlingMTLArgumentEncoder : NSObject <MTLArgumentEncoder>
@end
@implementation DarlingMTLArgumentEncoder
@end

@interface DarlingMTLRasterizationRateMap : NSObject <MTLRasterizationRateMap>
@end
@implementation DarlingMTLRasterizationRateMap
@end

@interface DarlingMTLIndirectCommandBuffer : NSObject <MTLIndirectCommandBuffer>
@end
@implementation DarlingMTLIndirectCommandBuffer
@end

@interface DarlingMTLAccelerationStructure : NSObject <MTLAccelerationStructure>
@end
@implementation DarlingMTLAccelerationStructure
@end

@interface DarlingMTLCounterSampleBuffer : NSObject <MTLCounterSampleBuffer>
@end
@implementation DarlingMTLCounterSampleBuffer
@end

@interface DarlingMTLDynamicLibrary : NSObject <MTLDynamicLibrary>
@end
@implementation DarlingMTLDynamicLibrary
@end
