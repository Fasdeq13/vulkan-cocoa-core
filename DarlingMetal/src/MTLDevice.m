#import <Metal/Metal.h>
#include <vulkan/vulkan.h>
#include <stdlib.h>
#include <string.h>

@interface DarlingMTLCommandQueue : NSObject <MTLCommandQueue>
@end
@implementation DarlingMTLCommandQueue
- (id<MTLCommandBuffer>)commandBuffer { return nil; }
- (id<MTLCommandBuffer>)commandBufferWithDescriptor:(MTLCommandBufferDescriptor *)descriptor { return nil; }
- (id<MTLCommandBuffer>)commandBufferWithUnretainedReferences { return nil; }
- (void)insertDebugCaptureBoundary {}
- (NSString *)label { return @"DarlingQueue"; }
- (void)setLabel:(NSString *)label {}
- (id<MTLDevice>)device { return nil; }
@end

@interface DarlingMTLBuffer : NSObject <MTLBuffer> {
    void* _storage;
    NSUInteger _length;
}
@end
@implementation DarlingMTLBuffer
- (instancetype)initWithLength:(NSUInteger)length {
    self = [super init];
    if (self) {
        _length = length;
        _storage = calloc(1, length);
    }
    return self;
}
- (void)dealloc {
    free(_storage);
    [super dealloc];
}
- (NSUInteger)length { return _length; }
- (void*)contents { return _storage; }
- (void)didModifyRange:(NSRange)range {}
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor offset:(NSUInteger)offset bytesPerRow:(NSUInteger)bytesPerRow { return nil; }
- (void)removeAllDebugMarkers {}
- (void)addDebugMarker:(NSString *)marker range:(NSRange)range {}
- (NSString *)label { return @"DarlingBuffer"; }
- (void)setLabel:(NSString *)label {}
- (id<MTLDevice>)device { return nil; }
- (NSUInteger)allocatedSize { return _length; }
- (NSUInteger)cpuCacheMode { return 0; }
- (NSUInteger)storageMode { return 0; }
- (NSUInteger)hazardTrackingMode { return 0; }
- (NSUInteger)resourceOptions { return 0; }
- (MTLPurgeableState)setPurgeableState:(MTLPurgeableState)state { return MTLPurgeableStateKeepCurrent; }
- (id<MTLHeap>)heap { return nil; }
- (NSUInteger)heapOffset { return 0; }
- (BOOL)isAliasable { return NO; }
- (void)makeAliasable {}
@end

@interface DarlingMTLLibrary : NSObject <MTLLibrary>
@end
@implementation DarlingMTLLibrary
- (NSString *)label { return @"DarlingLibrary"; }
- (void)setLabel:(NSString *)label {}
- (id<MTLDevice>)device { return nil; }
- (NSArray<NSString *> *)functionNames { return @[]; }
- (id<MTLFunction>)newFunctionWithName:(NSString *)functionName { return nil; }
- (void)newFunctionWithName:(NSString *)functionName constantValues:(MTLFunctionConstantValues *)constantValues completionHandler:(void (^)(id<MTLFunction>, NSError *))completionHandler {}
- (id<MTLFunction>)newFunctionWithName:(NSString *)functionName constantValues:(MTLFunctionConstantValues *)constantValues error:(NSError **)error { return nil; }
- (void)newFunctionWithDescriptor:(MTLFunctionDescriptor *)descriptor completionHandler:(void (^)(id<MTLFunction>, NSError *))completionHandler {}
- (id<MTLFunction>)newFunctionWithDescriptor:(MTLFunctionDescriptor *)descriptor error:(NSError **)error { return nil; }
- (id<MTLFunction>)newIntersectionFunctionWithDescriptor:(MTLIntersectionFunctionDescriptor *)descriptor error:(NSError **)error { return nil; }
- (void)newIntersectionFunctionWithDescriptor:(MTLIntersectionFunctionDescriptor *)descriptor completionHandler:(void (^)(id<MTLFunction>, NSError *))completionHandler {}
- (MTLLibraryType)type { return MTLLibraryTypeExecutable; }
- (NSString *)installName { return nil; }
@end

@interface DarlingMTLDevice : NSObject <MTLDevice> {
    VkInstance _instance;
    VkPhysicalDevice _physDevice;
    VkDevice _device;
}
@end

@implementation DarlingMTLDevice

- (instancetype)init {
    self = [super init];
    if (self) {
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.apiVersion = VK_API_VERSION_1_4;
        
        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        
        if (vkCreateInstance(&createInfo, NULL, &_instance) == VK_SUCCESS) {
            uint32_t count = 0;
            vkEnumeratePhysicalDevices(_instance, &count, NULL);
            if (count > 0) {
                VkPhysicalDevice* devices = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * count);
                vkEnumeratePhysicalDevices(_instance, &count, devices);
                _physDevice = devices[0];
                free(devices);
                
                float priority = 1.0f;
                VkDeviceQueueCreateInfo queueInfo = {};
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &priority;
                
                VkDeviceCreateInfo devInfo = {};
                devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                devInfo.queueCreateInfoCount = 1;
                devInfo.pQueueCreateInfos = &queueInfo;
                
                vkCreateDevice(_physDevice, &devInfo, NULL, &_device);
            }
        }
    }
    return self;
}

- (void)dealloc {
    if (_device) vkDestroyDevice(_device, NULL);
    if (_instance) vkDestroyInstance(_instance, NULL);
    [super dealloc];
}

- (NSString *)name {
    if (_physDevice) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(_physDevice, &props);
        return [NSString stringWithUTF8String:props.deviceName];
    }
    return @"NVIDIA GeForce RTX 4090";
}

- (uint64_t)registryID { return 424242; }
- (MTLSize)maxThreadsPerThreadgroup { return MTLSizeMake(1024, 1024, 64); }
- (BOOL)isLowPower { return NO; }
- (BOOL)isHeadless { return NO; }
- (BOOL)isRemovable { return NO; }
- (BOOL)hasUnifiedMemory { return NO; }
- (uint64_t)recommendedMaxWorkingSetSize { return 24ULL * 1024 * 1024 * 1024; }
- (uint64_t)locationId { return 0; }
- (MTLDeviceLocation)location { return MTLDeviceLocationBuiltIn; }
- (uint64_t)maxTransferRate { return 32ULL * 1024 * 1024 * 1024; }
- (BOOL)isDepth24Stencil8PixelFormatSupported { return YES; }
- (MTLReadWriteTextureTier)readWriteTextureSupport { return MTLReadWriteTextureTier2; }
- (MTLArgumentBuffersTier)argumentBuffersSupport { return MTLArgumentBuffersTier2; }
- (BOOL)areRasterOrderGroupsSupported { return YES; }
- (BOOL)supports32BitFloatFiltering { return YES; }
- (BOOL)supports32BitMSAA { return YES; }
- (BOOL)supportsQueryTextureSystemType { return YES; }
- (MTLTimestamp)currentTimestamp { return 0; }

- (BOOL)supportsFamily:(MTLGPUFamily)gpuFamily { return YES; }
- (BOOL)supportsFeatureSet:(MTLFeatureSet)featureSet { return YES; }

- (id<MTLCommandQueue>)newCommandQueue {
    return [[[NSClassFromString(@"DarlingMTLCommandQueue") alloc] init] autorelease];
}

- (id<MTLCommandQueue>)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)maxCount {
    return [self newCommandQueue];
}

- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options {
    return [[NSClassFromString(@"DarlingMTLBuffer") alloc] initWithLength:length];
}

- (id<MTLBuffer>)newBufferWithBytes:(const void *)pointer length:(NSUInteger)length options:(MTLResourceOptions)options {
    id buf = [[NSClassFromString(@"DarlingMTLBuffer") alloc] initWithLength:length];
    if (pointer && [buf respondsToSelector:@selector(contents)]) {
        void* dest = (void*)[buf performSelector:@selector(contents)];
        if (dest) memcpy(dest, pointer, length);
    }
    return buf;
}

- (id<MTLBuffer>)newBufferWithBytesNoCopy:(void *)pointer length:(NSUInteger)length options:(MTLResourceOptions)options deallocator:(void (^)(void *, NSUInteger))deallocator {
    return [[NSClassFromString(@"DarlingMTLBuffer") alloc] initWithLength:length];
}

- (id<MTLLibrary>)newDefaultLibrary {
    return [[[NSClassFromString(@"DarlingMTLLibrary") alloc] init] autorelease];
}

- (id<MTLLibrary>)newDefaultLibraryWithBundle:(NSBundle *)bundle error:(NSError **)error {
    return [self newDefaultLibrary];
}

- (id<MTLLibrary>)newLibraryWithFile:(NSString *)filepath error:(NSError **)error {
    return [self newDefaultLibrary];
}

- (id<MTLLibrary>)newLibraryWithURL:(NSURL *)url error:(NSError **)error {
    return [self newDefaultLibrary];
}

- (id<MTLLibrary>)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options error:(NSError **)error {
    return [self newDefaultLibrary];
}

- (void)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options completionHandler:(void (^)(id<MTLLibrary> _Nullable, NSError * _Nullable))completionHandler {
    completionHandler([self newDefaultLibrary], NULL);
}

- (id<MTLLibrary>)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error {
    return [self newDefaultLibrary];
}

- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor error:(NSError **)error {
    return [[[NSClassFromString(@"DarlingMTLRenderPipelineState") alloc] init] autorelease];
}

- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor completionHandler:(void (^)(id<MTLRenderPipelineState> _Nullable, NSError * _Nullable))completionHandler {
    completionHandler([self newRenderPipelineStateWithDescriptor:descriptor error:NULL], NULL);
}

- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction error:(NSError **)error {
    return [[[NSClassFromString(@"DarlingMTLComputePipelineState") alloc] init] autorelease];
}

- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction options:(MTLPipelineOption)options reflection:(MTLAutoreleasedComputePipelineReflection *)reflection error:(NSError **)error {
    return [self newComputePipelineStateWithFunction:computeFunction error:error];
}

- (void)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction completionHandler:(void (^)(id<MTLComputePipelineState>, NSError *))completionHandler {
    completionHandler([self newComputePipelineStateWithFunction:computeFunction error:NULL], NULL);
}

- (void)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction options:(MTLPipelineOption)options completionHandler:(void (^)(id<MTLComputePipelineState>, MTLComputePipelineReflection *, NSError *))completionHandler {
    completionHandler([self newComputePipelineStateWithFunction:computeFunction error:NULL], NULL, NULL);
}

- (id<MTLComputePipelineState>)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)descriptor options:(MTLPipelineOption)options reflection:(MTLAutoreleasedComputePipelineReflection *)reflection error:(NSError **)error {
    return [[[NSClassFromString(@"DarlingMTLComputePipelineState") alloc] init] autorelease];
}

- (void)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)descriptor options:(MTLPipelineOption)options completionHandler:(void (^)(id<MTLComputePipelineState>, MTLComputePipelineReflection *, NSError *))completionHandler {
    completionHandler([[[NSClassFromString(@"DarlingMTLComputePipelineState") alloc] init] autorelease], NULL, NULL);
}

- (id<MTLCommandQueue>)newCommandQueueWithDescriptor:(MTLCommandQueueDescriptor *)descriptor {
    return [self newCommandQueue];
}

- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor {
    return [[[NSClassFromString(@"DarlingMTLTexture") alloc] init] autorelease];
}

- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor iosurface:(IOSurfaceRef)iosurface plane:(NSUInteger)plane {
    return [self newTextureWithDescriptor:descriptor];
}

- (id<MTLSamplerState>)newSamplerStateWithDescriptor:(MTLSamplerDescriptor *)descriptor {
    return [[[NSClassFromString(@"DarlingMTLSamplerState") alloc] init] autorelease];
}

- (id<MTLDepthStencilState>)newDepthStencilStateWithDescriptor:(MTLDepthStencilDescriptor *)descriptor {
    return [[[NSClassFromString(@"DarlingMTLDepthStencilState") alloc] init] autorelease];
}

- (id<MTLTexture>)newSharedTextureWithDescriptor:(MTLTextureDescriptor *)descriptor {
    return [self newTextureWithDescriptor:descriptor];
}

- (id<MTLTexture>)newSharedTextureWithHandle:(MTLSharedTextureHandle *)sharedHandle {
    return [[[NSClassFromString(@"DarlingMTLTexture") alloc] init] autorelease];
}

- (id<MTLSharedEvent>)newSharedEvent {
    return [[[NSClassFromString(@"DarlingMTLSharedEvent") alloc] init] autorelease];
}

- (id<MTLSharedEvent>)newSharedEventWithHandle:(MTLSharedEventHandle *)sharedHandle {
    return [[[NSClassFromString(@"DarlingMTLSharedEvent") alloc] init] autorelease];
}

- (id<MTLEvent>)newEvent {
    return [[[NSClassFromString(@"DarlingMTLEvent") alloc] init] autorelease];
}

- (id<MTLHeap>)newHeapWithDescriptor:(MTLHeapDescriptor *)descriptor {
    return [[[NSClassFromString(@"DarlingMTLHeap") alloc] init] autorelease];
}

- (void)getDefaultSamplePositions:(MTLSamplePosition *)positions count:(NSUInteger)count {
    if (count > 0 && positions) {
        positions->x = 0.5f;
        positions->y = 0.5f;
    }
}

- (id<MTLArgumentEncoder>)newArgumentEncoderWithArguments:(NSArray<MTLArgumentDescriptor *> *)arguments {
    return [[[NSClassFromString(@"DarlingMTLArgumentEncoder") alloc] init] autorelease];
}

- (BOOL)supportsRasterizationRateMapWithLayerCount:(NSUInteger)layerCount {
    return YES;
}

- (id<MTLRasterizationRateMap>)newRasterizationRateMapWithDescriptor:(MTLRasterizationRateMapDescriptor *)descriptor {
    return [[[NSClassFromString(@"DarlingMTLRasterizationRateMap") alloc] init] autorelease];
}

- (id<MTLIndirectCommandBuffer>)newIndirectCommandBufferWithDescriptor:(MTLIndirectCommandBufferDescriptor *)descriptor maxCommandCount:(NSUInteger)maxCount options:(MTLResourceOptions)options {
    return [[[NSClassFromString(@"DarlingMTLIndirectCommandBuffer") alloc] init] autorelease];
}

- (MTLAccelerationStructureSizes)accelerationStructureSizesWithDescriptor:(MTLAccelerationStructureDescriptor *)descriptor {
    MTLAccelerationStructureSizes s = {1024, 1024, 1024};
    return s;
}

- (id<MTLAccelerationStructure>)newAccelerationStructureWithDescriptor:(MTLAccelerationStructureDescriptor *)descriptor {
    return [[[NSClassFromString(@"DarlingMTLAccelerationStructure") alloc] init] autorelease];
}

- (id<MTLAccelerationStructure>)newAccelerationStructureWithLength:(NSUInteger)length {
    return [[[NSClassFromString(@"DarlingMTLAccelerationStructure") alloc] init] autorelease];
}

- (BOOL)supportsVertexAmplificationCount:(NSUInteger)count {
    return YES;
}

- (id<MTLCounterSampleBuffer>)newCounterSampleBufferWithDescriptor:(MTLCounterSampleBufferDescriptor *)descriptor error:(NSError **)error {
    return [[[NSClassFromString(@"DarlingMTLCounterSampleBuffer") alloc] init] autorelease];
}

- (void)sampleTimestamps:(MTLTimestamp *)cpuTimestamp gpuTimestamp:(MTLTimestamp *)gpuTimestamp {
    if (cpuTimestamp) *cpuTimestamp = 1000;
    if (gpuTimestamp) *gpuTimestamp = 1000;
}

- (BOOL)supportsDynamicLibraries {
    return YES;
}

- (id<MTLDynamicLibrary>)newDynamicLibraryWithURL:(NSURL *)url error:(NSError **)error {
    return [[[NSClassFromString(@"DarlingMTLDynamicLibrary") alloc] init] autorelease];
}

- (id<MTLDynamicLibrary>)newDynamicLibrary:(id<MTLLibrary>)library error:(NSError **)error {
    return [[[NSClassFromString(@"DarlingMTLDynamicLibrary") alloc] init] autorelease];
}

- (void)newLibraryWithStitchedDescriptor:(MTLStitchedLibraryDescriptor *)descriptor completionHandler:(void (^)(id<MTLLibrary> library, NSError *error))completionHandler {
    completionHandler([[[NSClassFromString(@"DarlingMTLLibrary") alloc] init] autorelease], NULL);
}

- (id<MTLLibrary>)newLibraryWithStitchedDescriptor:(MTLStitchedLibraryDescriptor *)descriptor error:(NSError **)error {
    return [[[NSClassFromString(@"DarlingMTLLibrary") alloc] init] autorelease];
}

@end

id<MTLDevice> MTLCreateSystemDefaultDevice(void) {
    static DarlingMTLDevice* globalDevice = nil;
    if (!globalDevice) {
        globalDevice = [[DarlingMTLDevice alloc] init];
    }
    return globalDevice;
}
