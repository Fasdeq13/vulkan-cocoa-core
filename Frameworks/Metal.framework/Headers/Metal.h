#ifndef METAL_H
#define METAL_H

#import <Foundation/Foundation.h>

@protocol MTLDevice;
@protocol MTLCommandQueue;
@protocol MTLCommandBuffer;
@protocol MTLCommandBufferEncoderInfo;
@protocol MTLCommandEncoder;
@protocol MTLRenderCommandEncoder;
@protocol MTLComputeCommandEncoder;
@protocol MTLBlitCommandEncoder;
@protocol MTLResource;
@protocol MTLBuffer;
@protocol MTLTexture;
@protocol MTLLibrary;
@protocol MTLFunction;
@protocol MTLRenderPipelineState;
@protocol MTLComputePipelineState;
@protocol MTLDepthStencilState;
@protocol MTLSamplerState;
@protocol MTLEvent;
@protocol MTLSharedEvent;
@protocol MTLHeap;
@protocol MTLArgumentEncoder;
@protocol MTLIndirectCommandBuffer;
@protocol MTLIntersectionFunctionTable;
@protocol MTLVisibleFunctionTable;
@protocol MTLAccelerationStructure;

typedef NS_ENUM(NSUInteger, MTLResourceOptions) {
    MTLResourceCPUCacheModeDefaultCache = 0,
        MTLResourceCPUCacheModeWriteCombined = 1,
        MTLResourceStorageModeShared = 0 << 4,
        MTLResourceStorageModeManaged = 1 << 4,
        MTLResourceStorageModePrivate = 2 << 4,
        MTLResourceStorageModeMemoryless = 3 << 4,
        MTLResourceHazardTrackingModeTracked = 1 << 8,
        MTLResourceHazardTrackingModeUntracked = 2 << 8
};

typedef NS_ENUM(NSUInteger, MTLPixelFormat) {
    MTLPixelFormatInvalid = 0,
        MTLPixelFormatA8Unorm = 1,
        MTLPixelFormatR8Unorm = 10,
        MTLPixelFormatR8Snorm = 12,
        MTLPixelFormatR16Unorm = 20,
        MTLPixelFormatR16Float = 25,
        MTLPixelFormatRG8Unorm = 30,
        MTLPixelFormatR32Float = 55,
        MTLPixelFormatRG16Float = 65,
        MTLPixelFormatRGBA8Unorm = 70,
        MTLPixelFormatRGBA8Unorm_sRGB = 71,
        MTLPixelFormatBGRA8Unorm = 80,
        MTLPixelFormatBGRA8Unorm_sRGB = 81,
        MTLPixelFormatRGB10A2Unorm = 90,
        MTLPixelFormatRG32Float = 105,
        MTLPixelFormatRGBA16Float = 115,
        MTLPixelFormatRGBA32Float = 125,
        MTLPixelFormatBC1_RGBA = 130,
        MTLPixelFormatBC1_RGBA_sRGB = 131,
        MTLPixelFormatBC2_RGBA = 132,
        MTLPixelFormatBC2_RGBA_sRGB = 133,
        MTLPixelFormatBC3_RGBA = 134,
        MTLPixelFormatBC3_RGBA_sRGB = 135,
        MTLPixelFormatBC4_RUnorm = 140,
        MTLPixelFormatBC5_RGUnorm = 142,
        MTLPixelFormatBC6H_RGBFloat = 145,
        MTLPixelFormatBC7_RGBAUnorm = 148,
        MTLPixelFormatBC7_RGBAUnorm_sRGB = 149,
        MTLPixelFormatDepth16Unorm = 250,
        MTLPixelFormatDepth32Float = 252,
        MTLPixelFormatStencil8 = 253,
        MTLPixelFormatDepth24Unorm_Stencil8 = 255,
        MTLPixelFormatDepth32Float_Stencil8 = 260
};

typedef NS_ENUM(NSUInteger, MTLTextureType) {
    MTLTextureType1D = 0,
        MTLTextureType1DArray = 1,
        MTLTextureType2D = 2,
        MTLTextureType2DArray = 3,
        MTLTextureType2DMultisample = 4,
        MTLTextureTypeCube = 5,
        MTLTextureTypeCubeArray = 6,
        MTLTextureType3D = 7,
        MTLTextureType2DMultisampleArray = 8,
        MTLTextureTypeTextureBuffer = 9
};

typedef NS_ENUM(NSUInteger, MTLLoadAction) {
    MTLLoadActionDontCare = 0,
        MTLLoadActionLoad = 1,
        MTLLoadActionClear = 2,
};

typedef NS_ENUM(NSUInteger, MTLStoreAction) {
    MTLStoreActionDontCare = 0,
        MTLStoreActionStore = 1,
        MTLStoreActionMultisampleResolve = 2,
        MTLStoreActionStoreAndMultisampleResolve = 3,
        MTLStoreActionUnknown = 4,
        MTLStoreActionCustomSampleDepthStore = 5,
};

typedef NS_ENUM(NSUInteger, MTLPrimitiveType) {
    MTLPrimitiveTypePoint = 0,
        MTLPrimitiveTypeLine = 1,
        MTLPrimitiveTypeLineStrip = 2,
        MTLPrimitiveTypeTriangle = 3,
        MTLPrimitiveTypeTriangleStrip = 4,
};

typedef NS_ENUM(NSUInteger, MTLIndexType) {
    MTLIndexTypeUInt16 = 0,
        MTLIndexTypeUInt32 = 1,
};

typedef NS_ENUM(NSUInteger, MTLVisibilityResultMode) {
    MTLVisibilityResultModeDisabled = 0,
        MTLVisibilityResultModeBoolean = 1,
        MTLVisibilityResultModeCounting = 2,
};

typedef NS_ENUM(NSUInteger, MTLCullMode) {
    MTLCullModeNone = 0,
        MTLCullModeFront = 1,
        MTLCullModeBack = 2,
};

typedef NS_ENUM(NSUInteger, MTLWinding) {
    MTLWindingClockwise = 0,
        MTLWindingCounterClockwise = 1,
};

typedef NS_ENUM(NSUInteger, MTLDepthClipMode) {
    MTLDepthClipModeClip = 0,
        MTLDepthClipModeClamp = 1,
};

typedef NS_ENUM(NSUInteger, MTLTriangleFillMode) {
    MTLTriangleFillModeFill = 0,
        MTLTriangleFillModeLines = 1,
};

typedef NS_ENUM(NSUInteger, MTLCompareFunction) {
    MTLCompareFunctionNever = 0,
        MTLCompareFunctionLess = 1,
        MTLCompareFunctionEqual = 2,
        MTLCompareFunctionLessEqual = 3,
        MTLCompareFunctionGreater = 4,
        MTLCompareFunctionNotEqual = 5,
        MTLCompareFunctionGreaterEqual = 6,
        MTLCompareFunctionAlways = 7,
};

typedef NS_ENUM(NSUInteger, MTLStencilOperation) {
    MTLStencilOperationKeep = 0,
        MTLStencilOperationZero = 1,
        MTLStencilOperationReplace = 2,
        MTLStencilOperationIncrementClamp = 3,
        MTLStencilOperationDecrementClamp = 4,
        MTLStencilOperationInvert = 5,
        MTLStencilOperationIncrementWrap = 6,
        MTLStencilOperationDecrementWrap = 7,
};

typedef NS_ENUM(NSUInteger, MTLBlendFactor) {
    MTLBlendFactorZero = 0,
        MTLBlendFactorOne = 1,
        MTLBlendFactorSourceColor = 2,
        MTLBlendFactorOneMinusSourceColor = 3,
        MTLBlendFactorSourceAlpha = 4,
        MTLBlendFactorOneMinusSourceAlpha = 5,
        MTLBlendFactorDestinationColor = 6,
        MTLBlendFactorOneMinusDestinationColor = 7,
        MTLBlendFactorDestinationAlpha = 8,
        MTLBlendFactorOneMinusDestinationAlpha = 9,
        MTLBlendFactorSourceAlphaSaturated = 10,
        MTLBlendFactorBlendColor = 11,
        MTLBlendFactorOneMinusBlendColor = 12,
        MTLBlendFactorBlendAlpha = 13,
        MTLBlendFactorOneMinusBlendAlpha = 14,
        MTLBlendFactorSource1Color = 15,
        MTLBlendFactorOneMinusSource1Color = 16,
        MTLBlendFactorSource1Alpha = 17,
        MTLBlendFactorOneMinusSource1Alpha = 18,
};

typedef NS_ENUM(NSUInteger, MTLBlendOperation) {
    MTLBlendOperationAdd = 0,
        MTLBlendOperationSubtract = 1,
        MTLBlendOperationReverseSubtract = 2,
        MTLBlendOperationMin = 3,
        MTLBlendOperationMax = 4,
};

typedef NS_OPTIONS(NSUInteger, MTLColorWriteMask) {
    MTLColorWriteMaskNone = 0,
        MTLColorWriteMaskRed = 1 << 3,
        MTLColorWriteMaskGreen = 1 << 2,
        MTLColorWriteMaskBlue = 1 << 1,
        MTLColorWriteMaskAlpha = 1 << 0,
        MTLColorWriteMaskAll = 0xf
};

typedef NS_ENUM(NSUInteger, MTLLanguageVersion) {
    MTLLanguageVersion1_0 = 1 << 16,
        MTLLanguageVersion1_1 = (1 << 16) + 1,
        MTLLanguageVersion1_2 = (1 << 16) + 2,
        MTLLanguageVersion2_0 = 2 << 16,
        MTLLanguageVersion2_1 = (2 << 16) + 1,
        MTLLanguageVersion2_2 = (2 << 16) + 2,
        MTLLanguageVersion2_3 = (2 << 16) + 3,
        MTLLanguageVersion2_4 = (2 << 16) + 4,
        MTLLanguageVersion3_0 = 3 << 16,
};

typedef NS_ENUM(NSInteger, MTLFunctionType) {
    MTLFunctionTypeVertex = 1,
        MTLFunctionTypeFragment = 2,
        MTLFunctionTypeKernel = 3,
        MTLFunctionTypeVisible = 5,
        MTLFunctionTypeIntersection = 6,
};

typedef NS_ENUM(NSUInteger, MTLArgumentToBuffersTopology) {
    MTLArgumentToBuffersTopologyInvalid = 0,
};

typedef NS_ENUM(NSUInteger, MTLDeviceLocation) {
    MTLDeviceLocationBuiltIn = 0,
        MTLDeviceLocationSlot = 1,
        MTLDeviceLocationExternal = 2,
        MTLDeviceLocationUnspecified = NSUIntegerMax
};

typedef NS_ENUM(NSUInteger, MTLCPUCacheMode) {
    MTLCPUCacheModeDefaultCache = 0,
        MTLCPUCacheModeWriteCombined = 1,
};

typedef NS_ENUM(NSUInteger, MTLStorageMode) {
    MTLStorageModeShared = 0,
        MTLStorageModeManaged = 1,
        MTLStorageModePrivate = 2,
        MTLStorageModeMemoryless = 3,
};

typedef NS_ENUM(NSUInteger, MTLHazardTrackingMode) {
    MTLHazardTrackingModeDefault = 0,
        MTLHazardTrackingModeUntracked = 1,
        MTLHazardTrackingModeTracked = 2,
};

typedef struct {
    NSUInteger x, y, z;
} MTLOrigin;

typedef struct {
    NSUInteger width, height, depth;
} MTLSize;

typedef struct {
    MTLOrigin origin;
    MTLSize size;
} MTLRegion;

typedef struct {
    double red, green, blue, alpha;
} MTLClearColor;

NS_INLINE MTLOrigin MTLOriginMake(NSUInteger x, NSUInteger y, NSUInteger z) {
    MTLOrigin o = { x, y, z };
    return o;
}

NS_INLINE MTLSize MTLSizeMake(NSUInteger width, NSUInteger height, NSUInteger depth) {
    MTLSize s = { width, height, depth };
    return s;
}

NS_INLINE MTLRegion MTLRegionMake2D(NSUInteger x, NSUInteger y, NSUInteger width, NSUInteger height) {
    MTLRegion r = { {x, y, 0}, {width, height, 1} };
    return r;
}

@protocol MTLResource <NSObject>
@property(copy, atomic) NSString* label;
@property(readonly) id<MTLDevice> device;
@property(readonly) MTLCPUCacheMode cpuCacheMode;
@property(readonly) MTLStorageMode storageMode;
@property(readonly) MTLHazardTrackingMode hazardTrackingMode;
@property(readonly) MTLResourceOptions resourceOptions;
-(void)makeAliasable;
-(BOOL)isAliasable;
@end

@protocol MTLBuffer <MTLResource>
@property(readonly) NSUInteger length;
-(void*)contents;
-(void)didModifyRange:(NSRange)range;
-(id<MTLTexture>)newTextureWithDescriptor:(id)descriptor offset : (NSUInteger)offset bytesPerRow : (NSUInteger)bytesPerRow;
-(void)removeAllDebugMarkers;
-(void)addDebugMarker:(NSString*)marker range : (NSRange)range;
@end

@protocol MTLTexture <MTLResource>
@property(readonly, nullable) id<MTLResource> rootResource;
@property(readonly) id<MTLTexture> parentTexture;
@property(readonly) NSUInteger parentRelativeLevel;
@property(readonly) NSUInteger parentRelativeSlice;
@property(readonly, nullable) id<MTLBuffer> buffer;
@property(readonly) NSUInteger bufferOffset;
@property(readonly) NSUInteger bufferBytesPerRow;
@property(readonly) MTLTextureType textureType;
@property(readonly) MTLPixelFormat pixelFormat;
@property(readonly) NSUInteger width;
@property(readonly) NSUInteger height;
@property(readonly) NSUInteger depth;
@property(readonly) NSUInteger mipmapLevelCount;
@property(readonly) NSUInteger sampleCount;
@property(readonly) NSUInteger arrayLength;
@property(readonly, getter = isFramebufferOnly) BOOL framebufferOnly;
@property(readonly) NSUInteger firstMipmapInTail;
@property(readonly) NSUInteger tailSizeInBytes;
@property(readonly) BOOL isSparse;
@property(readonly) BOOL allowGPUOptimizedContents;
@property(readonly) MTLTextureSwizzleChannels swizzle;
@property(readonly) BOOL isShareable;
@property(readonly) NSUInteger gpuAddress;
@property(assign) MTLTextureUsage usage;
@property(readonly) NSUInteger allocatedSize;
@property(readonly) NSUInteger compressionFootprint;
@property(readonly) MTLHazardTrackingMode hazardTrackingMode;
@property(readonly) MTLResourceOptions resourceOptions;

-(void)getBytes:(void*)pixelBytes bytesPerRow : (NSUInteger)bytesPerRow fromRegion : (MTLRegion)region mipmapLevel : (NSUInteger)level;
-(void)replaceRegion:(MTLRegion)region mipmapLevel : (NSUInteger)level withBytes : (const void*)pixelBytes bytesPerRow : (NSUInteger)bytesPerRow;
-(void)getBytes:(void*)pixelBytes bytesPerRow : (NSUInteger)bytesPerRow bytesPerImage : (NSUInteger)bytesPerImage fromRegion : (MTLRegion)region mipmapLevel : (NSUInteger)level slice : (NSUInteger)slice;
-(void)replaceRegion:(MTLRegion)region mipmapLevel : (NSUInteger)level slice : (NSUInteger)slice withBytes : (const void*)pixelBytes bytesPerRow : (NSUInteger)bytesPerRow bytesPerImage : (NSUInteger)bytesPerImage;
-(id<MTLTexture>)newTextureViewWithPixelFormat:(MTLPixelFormat)pixelFormat;
-(id<MTLTexture>)newTextureViewWithPixelFormat:(MTLPixelFormat)pixelFormat textureType : (MTLTextureType)textureType traits : (NSRange)levelRange slices : (NSRange)sliceRange;
-(id<MTLTexture>)newTextureViewWithPixelFormat:(MTLPixelFormat)pixelFormat textureType : (MTLTextureType)textureType levels : (NSRange)levelRange slices : (NSRange)sliceRange swizzle : (MTLTextureSwizzleChannels)swizzle;
-(void)generateMipmapsForCommandBuffer:(id<MTLCommandBuffer>)commandBuffer;
@end

@protocol MTLFunction <NSObject>
@property(copy, atomic) NSString* label;
@property(readonly) id<MTLDevice> device;
@property(readonly) MTLFunctionType functionType;
@property(readonly) NSArray<MTLVertexAttribute*>* vertexAttributes;
@property(readonly) NSArray<MTLAttribute*>* stageInputAttributes;
@property(readonly) NSString* name;
@property(readonly) NSDictionary<NSString*, MTLFunctionConstant*>* functionConstantsDictionary;
@property(readonly) MTLPatchType patchType;
@property(readonly) NSInteger patchControlPointCount;
@property(readonly) NSArray<MTLFunctionArgument*>* arguments;
@property(readonly) MTLFunctionOptions options;

-(id<MTLArgumentEncoder>)newArgumentEncoderWithBufferIndex:(NSUInteger)bufferIndex;
-(id<MTLArgumentEncoder>)newArgumentEncoderWithBufferIndex:(NSUInteger)bufferIndex reflection : (MTLAutoreleasedArgument**)reflection;
@end

@protocol MTLLibrary <NSObject>
@property(copy, atomic) NSString* label;
@property(readonly) id<MTLDevice> device;
@property(readonly) NSArray<NSString*>* functionNames;
@property(readonly) MTLLibraryType type;
@property(readonly, nullable) NSString* installName;

-(id<MTLFunction>)newFunctionWithName:(NSString*)functionName;
-(void)newFunctionWithName:(NSString*)functionName constantValues : (MTLFunctionConstantValues*)constantValues completionHandler : (void(^)(id<MTLFunction> __nullable function, NSError * __nullable error))completionHandler;
-(id<MTLFunction>)newFunctionWithName:(NSString*)functionName constantValues : (MTLFunctionConstantValues*)constantValues error : (NSError**)error;
-(void)newFunctionWithName:(NSString*)functionName constantValues : (MTLFunctionConstantValues*)constantValues options : (MTLFunctionOptions)options completionHandler : (void(^)(id<MTLFunction> __nullable function, NSError * __nullable error))completionHandler;
-(id<MTLFunction>)newFunctionWithName:(NSString*)functionName constantValues : (MTLFunctionConstantValues*)constantValues options : (MTLFunctionOptions)options error : (NSError**)error;
-(void)newIntersectionFunctionWithDescriptor:(MTLIntersectionFunctionDescriptor*)descriptor completionHandler : (void(^)(id<MTLFunction> __nullable function, NSError * __nullable error))completionHandler;
-(id<MTLFunction>)newIntersectionFunctionWithDescriptor:(MTLIntersectionFunctionDescriptor*)descriptor error : (NSError**)error;
@end

@interface MTLRenderPipelineDescriptor : NSObject <NSCopying>
@property(copy, atomic, nullable) NSString* label;
@property(retain, atomic, nullable) id<MTLFunction> vertexFunction;
@property(retain, atomic, nullable) id<MTLFunction> fragmentFunction;
@property(copy, atomic, null_resettable) MTLVertexDescriptor* vertexDescriptor;
@property(readonly) MTLRenderPipelineColorAttachmentDescriptorArray* colorAttachments;
@property(assign, atomic) MTLPixelFormat depthAttachmentPixelFormat;
@property(assign, atomic) MTLPixelFormat stencilAttachmentPixelFormat;
@property(assign, atomic) NSUInteger sampleCount;
@property(assign, atomic) NSUInteger rasterSampleCount;
@property(assign, atomic, getter = isAlphaToCoverageEnabled) BOOL alphaToCoverageEnabled;
@property(assign, atomic, getter = isAlphaToOneEnabled) BOOL alphaToOneEnabled;
@property(assign, atomic, getter = isRasterizationEnabled) BOOL rasterizationEnabled;
@property(assign, atomic) NSUInteger maxVertexAmplificationCount;
@property(copy, atomic) MTLMeshRenderPipelineDescriptor* meshDescriptor;
@property(retain, atomic, nullable) id<MTLFunction> objectFunction;
@property(retain, atomic, nullable) id<MTLFunction> meshFunction;
@property(assign, atomic) NSUInteger maxTotalThreadsPerObjectThreadgroup;
@property(assign, atomic) NSUInteger maxTotalThreadsPerMeshThreadgroup;
@property(assign, atomic, getter = isObjectThreadgroupSizeMatchesMeshThreadgroupSize) BOOL objectThreadgroupSizeMatchesMeshThreadgroupSize;

-(void)reset;
@end

@protocol MTLRenderPipelineState <NSObject>
@property(readonly) NSString* label;
@property(readonly) id<MTLDevice> device;
@property(readonly) NSUInteger maxFocusedSampleCount;
@property(readonly) BOOL threadgroupSizeMatchesTileSize;
@property(readonly) NSUInteger imageblockSampleLength;
@property(readonly) BOOL supportIndirectCommandBuffers;
@property(readonly) NSUInteger gpuAddress;

-(NSUInteger)imageblockMemoryLengthForDimensions:(MTLSize)imageblockDimensions;
-(id<MTLArgumentEncoder>)newVertexArgumentEncoderWithBufferIndex:(NSUInteger)bufferIndex;
-(id<MTLArgumentEncoder>)newFragmentArgumentEncoderWithBufferIndex:(NSUInteger)bufferIndex;
-(id<MTLArgumentEncoder>)newTileArgumentEncoderWithBufferIndex:(NSUInteger)bufferIndex;
@end

@protocol MTLRenderCommandEncoder <NSObject>
@property(copy, atomic) NSString* label;
@property(readonly) id<MTLDevice> device;

-(void)setRenderPipelineState:(id<MTLRenderPipelineState>)pipelineState;
-(void)setVertexBytes:(const void*)bytes length : (NSUInteger)length atIndex : (NSUInteger)index;
-(void)setVertexBuffer:(id<MTLBuffer>)buffer offset : (NSUInteger)offset atIndex : (NSUInteger)index;
-(void)setVertexBufferOffset:(NSUInteger)offset atIndex : (NSUInteger)index;
-(void)setVertexBuffers:(const id<MTLBuffer> __nullable[__nonnull])buffers offsets : (const NSUInteger[__nonnull])offsets withRange : (NSRange)range;
-(void)setVertexTexture:(id<MTLTexture>)texture atIndex : (NSUInteger)index;
-(void)setVertexTextures:(const id<MTLTexture> __nullable[__nonnull])textures withRange : (NSRange)range;
-(void)setVertexSamplerState:(id<MTLSamplerState>)sampler atIndex : (NSUInteger)index;
-(void)setVertexSamplerStates:(const id<MTLSamplerState> __nullable[__nonnull])samplers withRange : (NSRange)range;
-(void)setVertexSamplerState:(id<MTLSamplerState>)sampler lodMinClamp : (float)lodMinClamp lodMaxClamp : (float)lodMaxClamp atIndex : (NSUInteger)index;
-(void)setVertexSamplerStates:(const id<MTLSamplerState> __nullable[__nonnull])samplers lodMinClamps : (const float[__nonnull])lodMinClamps lodMaxClamps : (const float[__nonnull])lodMaxClamps withRange : (NSRange)range;
-(void)setViewport:(MTLViewport)viewport;
-(void)setViewports:(const MTLViewport[__nonnull])viewports count : (NSUInteger)count;
-(void)setFrontFacingWinding:(MTLWinding)frontFacingWinding;
-(void)setCullMode:(MTLCullMode)cullMode;
-(void)setDepthClipMode:(MTLDepthClipMode)depthClipMode;
-(void)setDepthBias:(float)depthBias slopeScale : (float)slopeScale clamp : (float)clamp;
-(void)setScissorRect:(MTLScissorRect)rect;
-(void)setScissorRects:(const MTLScissorRect[__nonnull])rects count : (NSUInteger)count;
-(void)setTriangleFillMode:(MTLTriangleFillMode)fillMode;
-(void)setFragmentBytes:(const void*)bytes length : (NSUInteger)length atIndex : (NSUInteger)index;
-(void)setFragmentBuffer:(id<MTLBuffer>)buffer offset : (NSUInteger)offset atIndex : (NSUInteger)index;
-(void)setFragmentBufferOffset:(NSUInteger)offset atIndex : (NSUInteger)index;
-(void)setFragmentBuffers:(const id<MTLBuffer> __nullable[__nonnull])buffers offsets : (const NSUInteger[__nonnull])offsets withRange : (NSRange)range;
-(void)setFragmentTexture:(id<MTLTexture>)texture atIndex : (NSUInteger)index;
-(void)setFragmentTextures:(const id<MTLTexture> __nullable[__nonnull])textures withRange : (NSRange)range;
-(void)setFragmentSamplerState:(id<MTLSamplerState>)sampler atIndex : (NSUInteger)index;
-(void)setFragmentSamplerStates:(const id<MTLSamplerState> __nullable[__nonnull])samplers withRange : (NSRange)range;
-(void)setFragmentSamplerState:(id<MTLSamplerState>)sampler lodMinClamp : (float)lodMinClamp lodMaxClamp : (float)lodMaxClamp atIndex : (NSUInteger)index;
-(void)setFragmentSamplerStates:(const id<MTLSamplerState> __nullable[__nonnull])samplers lodMinClamps : (const float[__nonnull])lodMinClamps lodMaxClamps : (const float[__nonnull])lodMaxClamps withRange : (NSRange)range;
-(void)setBlendColorRed:(float)red green : (float)green blue : (float)blue alpha : (float)alpha;
-(void)setDepthStencilState:(id<MTLDepthStencilState>)depthStencilState;
-(void)setStencilReferenceValue:(uint32_t)referenceValue;
-(void)setStencilFrontReferenceValue:(uint32_t)frontReferenceValue backReferenceValue : (uint32_t)backReferenceValue;
-(void)setVisibilityResultMode:(MTLVisibilityResultMode)mode offset : (NSUInteger)offset;
-(void)setColorStoreAction:(MTLStoreAction)storeAction atIndex : (NSUInteger)index;
-(void)setDepthStoreAction:(MTLStoreAction)storeAction;
-(void)setStencilStoreAction:(MTLStoreAction)storeAction;
-(void)setColorStoreActionOptions:(MTLStoreActionOptions)storeActionOptions atIndex : (NSUInteger)index;
-(void)setDepthStoreActionOptions:(MTLStoreActionOptions)storeActionOptions;
-(void)setStencilStoreActionOptions:(MTLStoreActionOptions)storeActionOptions;
-(void)drawPrimitives:(MTLPrimitiveType)primitiveType vertexStart : (NSUInteger)vertexStart vertexCount : (NSUInteger)vertexCount instanceCount : (NSUInteger)instanceCount baseInstance : (NSUInteger)baseInstance;
-(void)drawPrimitives:(MTLPrimitiveType)primitiveType vertexStart : (NSUInteger)vertexStart vertexCount : (NSUInteger)vertexCount instanceCount : (NSUInteger)instanceCount;
-(void)drawPrimitives:(MTLPrimitiveType)primitiveType vertexStart : (NSUInteger)vertexStart vertexCount : (NSUInteger)vertexCount;
-(void)drawIndexedPrimitives:(MTLPrimitiveType)primitiveType indexCount : (NSUInteger)indexCount indexType : (MTLIndexType)indexType indexBuffer : (id<MTLBuffer>)indexBuffer indexBufferOffset : (NSUInteger)indexBufferOffset instanceCount : (NSUInteger)instanceCount baseVertex : (NSInteger)baseVertex baseInstance : (NSUInteger)baseInstance;
-(void)drawIndexedPrimitives:(MTLPrimitiveType)primitiveType indexCount : (NSUInteger)indexCount indexType : (MTLIndexType)indexType indexBuffer : (id<MTLBuffer>)indexBuffer indexBufferOffset : (NSUInteger)indexBufferOffset instanceCount : (NSUInteger)instanceCount;
-(void)drawIndexedPrimitives:(MTLPrimitiveType)primitiveType indexCount : (NSUInteger)indexCount indexType : (MTLIndexType)indexType indexBuffer : (id<MTLBuffer>)indexBuffer indexBufferOffset : (NSUInteger)indexBufferOffset;
-(void)drawPrimitives:(MTLPrimitiveType)primitiveType indirectBuffer : (id<MTLBuffer>)indirectBuffer indirectBufferOffset : (NSUInteger)indirectBufferOffset;
-(void)drawIndexedPrimitives:(MTLPrimitiveType)primitiveType indexType : (MTLIndexType)indexType indexBuffer : (id<MTLBuffer>)indexBuffer indexBufferOffset : (NSUInteger)indexBufferOffset indirectBuffer : (id<MTLBuffer>)indirectBuffer indirectBufferOffset : (NSUInteger)indirectBufferOffset;
-(void)textureBarrier;
-(void)updateFence:(id<MTLFence>)fence afterStages : (MTLRenderStages)stages;
-(void)waitForFence:(id<MTLFence>)fence beforeStages : (MTLRenderStages)stages;
-(void)setThreadgroupMemoryLength:(NSUInteger)length atIndex : (NSUInteger)index;
-(void)setTileBytes:(const void*)bytes length : (NSUInteger)length atIndex : (NSUInteger)index;
-(void)setTileBuffer:(id<MTLBuffer>)buffer offset : (NSUInteger)offset atIndex : (NSUInteger)index;
-(void)setTileBufferOffset:(NSUInteger)offset atIndex : (NSUInteger)index;
-(void)setTileBuffers:(const id<MTLBuffer> __nullable[__nonnull])buffers offsets : (const NSUInteger[__nonnull])offsets withRange : (NSRange)range;
-(void)setTileTexture:(id<MTLTexture>)texture atIndex : (NSUInteger)index;
-(void)setTileTextures:(const id<MTLTexture> __nullable[__nonnull])textures withRange : (NSRange)range;
-(void)setTileSamplerState:(id<MTLSamplerState>)sampler atIndex : (NSUInteger)index;
-(void)setTileSamplerStates:(const id<MTLSamplerState> __nullable[__nonnull])samplers withRange : (NSRange)range;
-(void)dispatchThreadsPerTile:(MTLSize)threadsPerTile;
-(void)useResource:(id<MTLResource>)resource usage : (MTLResourceUsage)usage;
-(void)useResources:(const id<MTLResource>[__nonnull])resources count : (NSUInteger)count usage : (MTLResourceUsage)usage;
-(void)useResource:(id<MTLResource>)resource usage : (MTLResourceUsage)usage stages : (MTLRenderStages)stages;
-(void)useResources:(const id<MTLResource>[__nonnull])resources count : (NSUInteger)count usage : (MTLResourceUsage)usage stages : (MTLRenderStages)stages;
-(void)useHeap:(id<MTLHeap>)heap;
-(void)useHeaps:(const id<MTLHeap>[__nonnull])heaps count : (NSUInteger)count;
-(void)useHeap:(id<MTLHeap>)heap stages : (MTLRenderStages)stages;
-(void)useHeaps:(const id<MTLHeap>[__nonnull])heaps count : (NSUInteger)count stages : (MTLRenderStages)stages;
-(void)executeCommandsInBuffer:(id<MTLIndirectCommandBuffer>)indirectCommandBuffer withRange : (NSRange)range;
-(void)executeCommandsInBuffer:(id<MTLIndirectCommandBuffer>)indirectCommandBuffer indirectBuffer : (id<MTLBuffer>)indirectBuffer indirectBufferOffset : (NSUInteger)indirectBufferOffset;
-(void)memoryBarrierWithScope:(MTLBarrierScope)scope afterStages : (MTLRenderStages)afterStages beforeStages : (MTLRenderStages)beforeStages;
-(void)memoryBarrierWithResources:(const id<MTLResource>[__nonnull])resources count : (NSUInteger)count afterStages : (MTLRenderStages)afterStages beforeStages : (MTLRenderStages)beforeStages;
-(void)drawMeshPrimitives:(MTLSize)threadgroupsPerGrid threadsPerObjectThreadgroup : (MTLSize)threadsPerObjectThreadgroup threadsPerMeshThreadgroup : (MTLSize)threadsPerMeshThreadgroup;
-(void)drawMeshThreadgroups:(MTLSize)threadgroupsPerGrid threadsPerObjectThreadgroup : (MTLSize)threadsPerObjectThreadgroup threadsPerMeshThreadgroup : (MTLSize)threadsPerMeshThreadgroup;
-(void)drawMeshThreads:(MTLSize)threadsPerGrid threadsPerObjectThreadgroup : (MTLSize)threadsPerObjectThreadgroup threadsPerMeshThreadgroup : (MTLSize)threadsPerMeshThreadgroup;
-(void)drawMeshPrimitivesWithIndirectBuffer:(id<MTLBuffer>)indirectBuffer indirectBufferOffset : (NSUInteger)indirectBufferOffset executionBuffer : (id<MTLBuffer>)executionBuffer executionBufferOffset : (NSUInteger)executionBufferOffset;
-(void)endEncoding;
@end

@interface MTLRenderPassDescriptor : NSObject <NSCopying>
@property(readonly) MTLRenderPassColorAttachmentDescriptorArray* colorAttachments;
@property(copy, atomic) MTLRenderPassDepthAttachmentDescriptor* depthAttachment;
@property(copy, atomic) MTLRenderPassStencilAttachmentDescriptor* stencilAttachment;
@property(retain, atomic, nullable) id<MTLBuffer> visibilityResultBuffer;
@property(assign, atomic) NSUInteger renderTargetArrayLength;
@property(assign, atomic) NSUInteger imageblockSampleLength;
@property(assign, atomic) NSUInteger threadgroupMemoryLength;
@property(assign, atomic) NSUInteger tileWidth;
@property(assign, atomic) NSUInteger tileHeight;
@property(assign, atomic) NSUInteger defaultRasterSampleCount;
@property(assign, atomic) NSUInteger renderTargetWidth;
@property(assign, atomic) NSUInteger renderTargetHeight;
@property(retain, atomic, nullable) id<MTLBuffer> sampleBufferAttachments;

+(MTLRenderPassDescriptor*)renderPassDescriptor;
-(void)reset;
@end

@protocol MTLCommandBuffer <NSObject>
@property(readonly) id<MTLCommandQueue> commandQueue;
@property(readonly) NSString* label;
@property(readonly) MTLCommandBufferStatus status;
@property(readonly) NSError* error;
@property(readonly) CFAbsoluteTime kernelStartTime;
@property(readonly) CFAbsoluteTime kernelEndTime;
@property(readonly) CFAbsoluteTime GPUStartTime;
@property(readonly) CFAbsoluteTime GPUEndTime;
@property(readonly) BOOL retainedReferences;

-(void)enqueue;
-(void)commit;
-(void)addScheduledHandler:(MTLCommandBufferHandler)block;
-(void)addCompletedHandler:(MTLCommandBufferHandler)block;
-(void)waitUntilScheduled;
-(void)waitUntilCompleted;
-(id<MTLRenderCommandEncoder>)renderCommandEncoderWithDescriptor:(MTLRenderPassDescriptor*)renderPassDescriptor;
-(id<MTLComputeCommandEncoder>)computeCommandEncoderWithDescriptor:(MTLComputePassDescriptor*)computePassDescriptor;
-(id<MTLBlitCommandEncoder>)blitCommandEncoderWithDescriptor:(MTLBlitPassDescriptor*)blitPassDescriptor;
-(id<MTLComputeCommandEncoder>)computeCommandEncoder;
-(id<MTLBlitCommandEncoder>)blitCommandEncoder;
-(id<MTLResourceStateCommandEncoder>)resourceStateCommandEncoder;
-(id<MTLResourceStateCommandEncoder>)resourceStateCommandEncoderWithDescriptor:(MTLResourceStatePassDescriptor*)resourceStatePassDescriptor;
-(id<MTLAccelerationStructureCommandEncoder>)accelerationStructureCommandEncoder;
-(id<MTLAccelerationStructureCommandEncoder>)accelerationStructureCommandEncoderWithDescriptor:(MTLAccelerationStructurePassDescriptor*)accelerationStructurePassDescriptor;
-(void)pushDebugGroup:(NSString*)string;
-(void)popDebugGroup;
@end

@protocol MTLCommandQueue <NSObject>
@property(copy, atomic) NSString* label;
@property(readonly) id<MTLDevice> device;

-(id<MTLCommandBuffer>)commandBuffer;
-(id<MTLCommandBuffer>)commandBufferWithDescriptor:(MTLCommandBufferDescriptor*)descriptor;
-(id<MTLCommandBuffer>)commandBufferWithUnretainedReferences;
-(void)insertDebugCaptureBoundary;
@end

@protocol MTLDevice <NSObject>
@property(readonly) NSString* name;
@property(readonly) uint64_t registryID;
@property(readonly) MTLSize maxThreadsPerThreadgroup;
@property(readonly, getter = isLowPower) BOOL lowPower;
@property(readonly, getter = isHeadless) BOOL headless;
@property(readonly, getter = isRemovable) BOOL removable;
@property(readonly) BOOL hasUnifiedMemory;
@property(readonly) NSUInteger peerGroupSize;
@property(readonly) uint32_t peerIndex;
@property(readonly) uint64_t peerMask;
@property(readonly) NSUInteger maxBufferLength;
@property(readonly) NSUInteger currentAllocatedSize;
@property(readonly) NSUInteger recommendedMaxWorkingSetSize;
@property(readonly) BOOL depth24Stencil8PixelFormatSupported;
@property(readonly) BOOL readWriteTextureSupport;
@property(readonly) MTLArgumentBuffersTier argumentBuffersTier;
@property(readonly) BOOL programmableSamplePositionsSupported;
@property(readonly) BOOL supportIndirectCommandBuffers;
@property(readonly) NSUInteger maxArgumentBufferSamplerCount;

-(id<MTLCommandQueue>)newCommandQueue;
-(id<MTLCommandQueue>)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)maxCommandBufferCount;
-(id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options : (MTLResourceOptions)options;
-(id<MTLBuffer>)newBufferWithBytes:(const void*)pointer length : (NSUInteger)length options : (MTLResourceOptions)options;
-(id<MTLBuffer>)newBufferWithBytesNoCopy:(void*)pointer length : (NSUInteger)length options : (MTLResourceOptions)options deallocator : (void(^ __nullable)(void* pointer, NSUInteger length))deallocator;
-(id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor*)descriptor;
-(id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor*)descriptor iosurface : (IOSurfaceRef)iosurface plane : (NSUInteger)plane;
-(id<MTLSamplerState>)newSamplerStateWithDescriptor:(MTLSamplerDescriptor*)descriptor;
-(id<MTLLibrary>)newLibraryWithSource:(NSString*)source options : (nullable MTLCompileOptions*)options error : (NSError**)error;
-(void)newLibraryWithSource:(NSString*)source options : (nullable MTLCompileOptions*)options completionHandler : (void(^)(id<MTLLibrary> __nullable library, NSError * __nullable error))completionHandler;
-(id<MTLLibrary>)newLibraryWithData:(dispatch_data_t)data error : (NSError**)error;
-(id<MTLLibrary>)newLibraryWithURL:(NSURL*)url error : (NSError**)error;
-(id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)descriptor error : (NSError**)error;
-(void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)descriptor completionHandler : (void(^)(id<MTLRenderPipelineState> __nullable pipelineState, NSError * __nullable error))completionHandler;
-(id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)descriptor options : (MTLPipelineOption)options reflection : (MTLAutoreleasedRenderPipelineReflection * __nullable)reflection error : (NSError**)error;
-(void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)descriptor options : (MTLPipelineOption)options completionHandler : (void(^)(id<MTLRenderPipelineState> __nullable pipelineState, MTLRenderPipelineReflection * __nullable reflection, NSError * __nullable error))completionHandler;
-(id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction error : (NSError**)error;
-(void)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction completionHandler : (void(^)(id<MTLComputePipelineState> __nullable pipelineState, NSError * __nullable error))completionHandler;
-(id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction options : (MTLPipelineOption)options reflection : (MTLAutoreleasedComputePipelineReflection * __nullable)reflection error : (NSError**)error;
-(void)newComputePipelineStateWithFunction:(id<MTLFunction>)computeFunction options : (MTLPipelineOption)options completionHandler : (void(^)(id<MTLComputePipelineState> __nullable pipelineState, MTLComputePipelineReflection * __nullable reflection, NSError * __nullable error))completionHandler;
-(id<MTLComputePipelineState>)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor*)descriptor options : (MTLPipelineOption)options reflection : (MTLAutoreleasedComputePipelineReflection * __nullable)reflection error : (NSError**)error;
-(void)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor*)descriptor options : (MTLPipelineOption)options completionHandler : (void(^)(id<MTLComputePipelineState> __nullable pipelineState, MTLComputePipelineReflection * __nullable reflection, NSError * __nullable error))completionHandler;
-(id<MTLDepthStencilState>)newDepthStencilStateWithDescriptor:(MTLDepthStencilDescriptor*)descriptor;
-(id<MTLHeap>)newHeapWithDescriptor:(MTLHeapDescriptor*)descriptor;
-(id<MTLFence>)newFence;
-(BOOL)supportsFeatureSet:(MTLFeatureSet)featureSet;
-(BOOL)supportsFamily:(MTLGPUFamily)gpuFamily;
-(BOOL)supportsTextureSampleCount:(NSUInteger)sampleCount;
-(id<MTLArgumentEncoder>)newArgumentEncoderWithArguments:(NSArray<MTLArgumentDescriptor*> *)arguments;
-(id<MTLIndirectCommandBuffer>)newIndirectCommandBufferWithDescriptor:(MTLIndirectCommandBufferDescriptor*)descriptor maxCommandCount : (NSUInteger)maxCount options : (MTLResourceOptions)options;
-(id<MTLEvent>)newEvent;
-(id<MTLSharedEvent>)newSharedEvent;
-(id<MTLSharedEvent>)newSharedEventWithHandle:(MTLSharedEventHandle*)sharedEventHandle;
-(id<MTLAccelerationStructure>)newAccelerationStructureWithDescriptor:(MTLAccelerationStructureDescriptor*)descriptor;
-(id<MTLAccelerationStructure>)newAccelerationStructureWithLength:(NSUInteger)length;
-(MTLSizeAndAlign)heapBufferSizeAndAlignWithLength:(NSUInteger)length options : (MTLResourceOptions)options;
-(MTLSizeAndAlign)heapTextureSizeAndAlignWithDescriptor:(MTLTextureDescriptor*)desc;
-(id<MTLLibrary>)newDefaultLibrary;
-(id<MTLLibrary>)newDefaultLibraryWithBundle:(NSBundle*)bundle error : (NSError**)error;
-(void)getDefaultSamplePositions:(MTLSamplePosition*)positions count : (NSUInteger)count;
@end

#ifdef __cplusplus
extern "C" {
#endif
    id<MTLDevice> __nullable MTLCreateSystemDefaultDevice(void);
    NSArray<id<MTLDevice>>* __nonnull MTLCopyAllDevices(void);
#ifdef __cplusplus
}
#endif

#endif // METAL_H


