//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//
//  This software is provided 'as-is', without any express or implied
//  warranty. In no event will the authors be held liable for any damages
//  arising from the use of this software.
//
//  Permission is granted to anyone to use this software for personal use
//  (for a commercial licence please contact the author), and to alter it and
//  redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
//

#import "RasterizerLayer.h"
#import <Metal/Metal.h>
#import <map>
#import <time.h>

template<typename T, typename S, int kExpiryAge = 10>
struct MetalCache {
    struct Entry {
        Entry(T payload) : payload(payload), timestamp(getTime()) {}
        
        T payload;
        double timestamp;
    };
    
    virtual T createPayload(S src, id <MTLDevice> device) = 0;
    
    T entryFor(S src, id <MTLDevice> device) {
        flush();
        auto key = src.hash();
        auto it = map.find(key);
        if (it == map.end()) {
            T payload = createPayload(src, device);
            map.emplace(key, Entry(payload));
            return payload;
        } else {
            it->second.timestamp = getTime();
            return it->second.payload;
        }
    }
    
    void flush() {
        double now = getTime();
        std::vector<size_t> expired;
        for (const auto& entry: map)
            if (now - entry.second.timestamp > kExpiryAge)
                expired.emplace_back(entry.first);
        for (auto key: expired)
            map.erase(key);
    }
    
    static inline double getTime() {
        struct timeval tv;  gettimeofday(& tv, NULL);
        return tv.tv_sec + tv.tv_usec * 1e-6;
    }
    
    std::map<size_t, Entry> map;
};

#pragma clang diagnostic ignored "-Wextra"
struct GeometryCache : MetalCache<id <MTLBuffer>, const Ra::Scene &> {
    __strong id <MTLBuffer> createPayload(const Ra::Scene & scene, id <MTLDevice> device) override {
        size_t length = (1 + scene.p16total) * sizeof(Ra::Point16);
        id <MTLBuffer> buffer = [device newBufferWithLength:length
                                                    options:MTLResourceStorageModeShared];
        auto dst = (Ra::Point16 *)buffer.contents;
        for (auto& entry: scene.p16map)
            memcpy(dst + entry.second.idx, entry.second.path->p16s.base, entry.second.path->p16s.end * sizeof(*dst));
        return buffer;
    }
};

struct StrokeCache : MetalCache<id <MTLBuffer>, const Ra::Scene &> {
    __strong id <MTLBuffer> createPayload(const Ra::Scene & scene, id <MTLDevice> device) override {
        size_t length = (1 + scene.stroketotal) * sizeof(Ra::Point16);
        id <MTLBuffer> buffer = [device newBufferWithLength:length
                                                    options:MTLResourceStorageModeShared];
        auto dst = (Ra::Point16 *)buffer.contents;
        for (auto& entry: scene.strokemap)
            memcpy(dst + entry.second.idx, entry.second.path->outlines.base, entry.second.path->outlines.end * sizeof(*dst));
        return buffer;
    }
};

struct TangentCache : MetalCache<id <MTLBuffer>, const Ra::Scene &> {
    __strong id <MTLBuffer> createPayload(const Ra::Scene & scene, id <MTLDevice> device) override {
        size_t length = (1 + scene.stroketotal) * sizeof(Ra::Vector16);
        id <MTLBuffer> buffer = [device newBufferWithLength:length
                                                    options:MTLResourceStorageModeShared];
        auto dst = (Ra::Vector16 *)buffer.contents;
        for (auto& entry: scene.strokemap)
            memcpy(dst + entry.second.idx, entry.second.path->tangents.base, entry.second.path->tangents.end * sizeof(*dst));
        return buffer;
    }
};

struct TextureCache : MetalCache<id <MTLTexture>, const Ra::Paint &> {
    __strong id <MTLTexture> createPayload(const Ra::Paint & image, id <MTLDevice> device) override {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                         width:image.w
                                        height:image.h
                                     mipmapped:NO];
        desc.storageMode = MTLStorageModeShared;
        desc.usage = MTLTextureUsageShaderRead;
        
        auto texture = [device newTextureWithDescriptor:desc];
        [texture replaceRegion:MTLRegionMake2D(0, 0, image.w, image.h)
                        mipmapLevel:0
                          withBytes:& image.matched[0]
                        bytesPerRow:image.w * sizeof(Ra::Color)];
        return texture;
    }
};


@interface RasterizerLayer ()
{
    Ra::Buffer _buffer0, _buffer1;
    TextureCache _textureCache;
    GeometryCache _geometryCache;
    StrokeCache _strokeCache;
    TangentCache _tangentCache;
}

@property (nonatomic) dispatch_semaphore_t inflight_semaphore;
@property (nonatomic) id <MTLCommandQueue> commandQueue;
@property (nonatomic) id <MTLLibrary> defaultLibrary;
@property (nonatomic) size_t tick;
@property (nonatomic) id <MTLRenderPipelineState> quadEdgesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> fastEdgesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> fastMoleculesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> quadMoleculesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> opaquesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> stencilPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> instancesPipelineState;
@property (nonatomic) id <MTLDepthStencilState> stencilDepthState;
@property (nonatomic) id <MTLDepthStencilState> instancesDepthState;
@property (nonatomic) id <MTLDepthStencilState> opaquesDepthState;
@property (nonatomic) id <MTLDepthStencilState> instancesClipDepthState;
@property (nonatomic) id <MTLDepthStencilState> opaquesClipDepthState;
@property (nonatomic) id <MTLTexture> depthTexture;
@property (nonatomic) id <MTLTexture> accumulationTexture;

@end


@implementation RasterizerLayer

- (id)init {
    self = [super init];
    if (!self)
        return nil;
    
    self.device = MTLCreateSystemDefaultDevice();
    self.pixelFormat = MTLPixelFormatBGRA8Unorm;
    self.magnificationFilter = kCAFilterNearest;
    self.colorspace = nil;
    self.commandQueue = [self.device newCommandQueue];
    
#if SWIFT_PACKAGE
    self.defaultLibrary = [self.device newDefaultLibraryWithBundle:SWIFTPM_MODULE_BUNDLE error:nil];
#else
    self.defaultLibrary = [self.device newDefaultLibrary];
#endif

    self.inflight_semaphore = dispatch_semaphore_create(2);
    
    MTLDepthStencilDescriptor *stencilStateDescriptor = [MTLDepthStencilDescriptor new];
    stencilStateDescriptor.depthWriteEnabled = NO;
    stencilStateDescriptor.depthCompareFunction = MTLCompareFunctionAlways;
    stencilStateDescriptor.frontFaceStencil.stencilCompareFunction = MTLCompareFunctionAlways;
    stencilStateDescriptor.frontFaceStencil.depthStencilPassOperation = MTLStencilOperationIncrementWrap;
    stencilStateDescriptor.backFaceStencil.stencilCompareFunction = MTLCompareFunctionAlways;
    stencilStateDescriptor.backFaceStencil.depthStencilPassOperation = MTLStencilOperationDecrementWrap;
    self.stencilDepthState = [self.device newDepthStencilStateWithDescriptor:stencilStateDescriptor];
    
    MTLDepthStencilDescriptor *depthStencilDescriptor = [MTLDepthStencilDescriptor new];
    depthStencilDescriptor.depthWriteEnabled = YES;
    depthStencilDescriptor.depthCompareFunction = MTLCompareFunctionGreater;
    self.opaquesDepthState = [self.device newDepthStencilStateWithDescriptor:depthStencilDescriptor];

    depthStencilDescriptor.depthWriteEnabled = NO;
    self.instancesDepthState = [self.device newDepthStencilStateWithDescriptor:depthStencilDescriptor];
    
    depthStencilDescriptor.frontFaceStencil.stencilCompareFunction = MTLCompareFunctionNotEqual;
    depthStencilDescriptor.frontFaceStencil.depthStencilPassOperation = MTLStencilOperationKeep;
    depthStencilDescriptor.frontFaceStencil.readMask = 0x01;
    depthStencilDescriptor.backFaceStencil = depthStencilDescriptor.frontFaceStencil;
    self.instancesClipDepthState = [self.device newDepthStencilStateWithDescriptor:depthStencilDescriptor];
    
    depthStencilDescriptor.depthWriteEnabled = YES;
    self.opaquesClipDepthState = [self.device newDepthStencilStateWithDescriptor:depthStencilDescriptor];
    
    MTLRenderPipelineDescriptor *stencilDescriptor = [MTLRenderPipelineDescriptor new];
    stencilDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatInvalid;
    stencilDescriptor.depthAttachmentPixelFormat = MTLPixelFormatInvalid;
    stencilDescriptor.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    stencilDescriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"stencil_vertex_main"];
    stencilDescriptor.fragmentFunction = nil;
    stencilDescriptor.label = @"stencil";
    self.stencilPipelineState = [self.device newRenderPipelineStateWithDescriptor:stencilDescriptor error:nil];
    
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.colorAttachments[0].pixelFormat = self.pixelFormat;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    descriptor.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    descriptor.colorAttachments[0].blendingEnabled = NO;
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"opaques_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"opaques_fragment_main"];
    descriptor.label = @"opaques";
    self.opaquesPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.colorAttachments[0].blendingEnabled = YES;
    descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"instances_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"instances_fragment_main"];
    descriptor.label = @"instances";
    self.instancesPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatR32Float;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatInvalid;
    descriptor.stencilAttachmentPixelFormat = MTLPixelFormatInvalid;
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"edges_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"quad_edges_fragment_main"];
    descriptor.label = @"quad edges";
    self.quadEdgesPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"fast_edges_fragment_main"];
    descriptor.label = @"fast edges";
    self.fastEdgesPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"fast_molecules_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"fast_molecules_fragment_main"];
    descriptor.label = @"fast molecules";
    self.fastMoleculesPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"quad_molecules_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"quad_molecules_fragment_main"];
    descriptor.label = @"quad molecules";
    self.quadMoleculesPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    return self;
}

- (void)display {
    @autoreleasepool {
        self.drawableSize = CGSizeMake(ceil(self.bounds.size.width * self.contentsScale), ceil(self.bounds.size.height * self.contentsScale));
        if (dispatch_semaphore_wait(_inflight_semaphore, DISPATCH_TIME_NOW) == 0)
            [self draw];
    }
}

- (void)draw {
    BOOL odd = ++_tick & 1;
    Ra::Buffer *buffer = odd ? & _buffer1 : & _buffer0;
    if ([self.layerDelegate respondsToSelector:@selector(writeBuffer:forLayer:)])
        [self.layerDelegate writeBuffer:buffer forLayer:self];
    
    id <MTLBuffer> mtlBuffer = buffer->size == 0 ? nil : [self.device newBufferWithBytesNoCopy:buffer->base
                                               length:buffer->size
                                              options:MTLResourceStorageModeShared
                                          deallocator:nil];
    id <CAMetalDrawable> drawable = [self nextDrawable];
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                                                    width:self.drawableSize.width
                                                                                   height:self.drawableSize.height
                                                                                mipmapped:NO];
    desc.storageMode = MTLStorageModePrivate;
    desc.usage = MTLTextureUsageRenderTarget;
    self.depthTexture = [self.device newTextureWithDescriptor:desc];
    [self.depthTexture setLabel:@"depthTexture"];
    
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.pixelFormat = MTLPixelFormatR32Float;
    self.accumulationTexture = [self.device newTextureWithDescriptor:desc];
    [self.accumulationTexture setLabel:@"accumulationTexture"];
    
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageShaderRead;
    desc.pixelFormat = MTLPixelFormatBGRA8Unorm;
    size_t w = kColorTextureWidth, h = (buffer->pathsCount + w - 0) / w, th = buffer->texCount;
    desc.width = w;
    desc.height = h + th;
    id <MTLTexture> colorTexture = [self.device newTextureWithDescriptor:desc];
    [colorTexture replaceRegion:MTLRegionMake2D(0, 0, w, h)
                    mipmapLevel:0
                      withBytes:buffer->base + buffer->colors
                    bytesPerRow:w * sizeof(Ra::Color)];
    if (th) {
        [colorTexture replaceRegion:MTLRegionMake2D(0, h, w, th)
                        mipmapLevel:0
                          withBytes:buffer->base + buffer->texStrips
                        bytesPerRow:w * sizeof(Ra::Color)];
    }
    
    id <MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    
    MTLRenderPassDescriptor *drawableDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    drawableDescriptor.colorAttachments[0].texture = drawable.texture;
    drawableDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    drawableDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    drawableDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(
        buffer->params.clearColor.r / 255.0,
        buffer->params.clearColor.g / 255.0,
        buffer->params.clearColor.b / 255.0,
        buffer->params.clearColor.a / 255.0
    );
    drawableDescriptor.depthAttachment.texture = _depthTexture;
    drawableDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
    drawableDescriptor.depthAttachment.storeAction = MTLStoreActionStore;
    drawableDescriptor.depthAttachment.clearDepth = 0;
    
    drawableDescriptor.stencilAttachment.texture = _depthTexture;
    drawableDescriptor.stencilAttachment.loadAction = MTLLoadActionLoad;
    drawableDescriptor.stencilAttachment.storeAction = MTLStoreActionStore;
    
    MTLRenderPassDescriptor *clipDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    clipDescriptor.stencilAttachment.texture = _depthTexture;
    clipDescriptor.stencilAttachment.loadAction = MTLLoadActionClear;
    clipDescriptor.stencilAttachment.storeAction = MTLStoreActionStore;
    clipDescriptor.stencilAttachment.clearStencil = 0;
    
    id <MTLRenderCommandEncoder> commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:drawableDescriptor];
    
    drawableDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
    drawableDescriptor.depthAttachment.loadAction = MTLLoadActionLoad;
    
    MTLRenderPassDescriptor *edgesDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    edgesDescriptor.colorAttachments[0].texture = _accumulationTexture;
    edgesDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    edgesDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    edgesDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    
    bool useClip = false, useImage = false;
    uint32_t reverse, pathsCount = uint32_t(buffer->pathsCount), texCount = uint32_t(th);
    float width = drawable.texture.width, height = drawable.texture.height;
    
    NSUInteger imgIndex = 0, sceneIndex = 0;
    id <MTLTexture> imageTexture = nil;
    id <MTLBuffer> p16buffer = nil, strokebuffer = nil, tangentbuffer = nil;
    
    for (size_t segbase = 0, instbase = 0, i = 0; i < buffer->entries.end; i++) {
        Ra::Buffer::Entry& entry = buffer->entries.base[i];
        switch (entry.type) {
            case Ra::Buffer::kSegmentsBase:
                segbase = entry.begin;
                break;
            case Ra::Buffer::kInstancesBase:
                instbase = entry.begin;
                break;
            case Ra::Buffer::kDisableClip:
                useClip = false;
                break;
            case Ra::Buffer::kEnableClip:
                useClip = true;
                break;
            case Ra::Buffer::kDisableImage:
                useImage = false;
                break;
            case Ra::Buffer::kNextImage:
                imageTexture = _textureCache.entryFor(buffer->images[imgIndex++], self.device);
                useImage = true;
                break;
            case Ra::Buffer::kNextScene:
                assert(sceneIndex < buffer->scenes.end());
                p16buffer = _geometryCache.entryFor(*buffer->scenes[sceneIndex].ptr, self.device);
                strokebuffer = _strokeCache.entryFor(*buffer->scenes[sceneIndex].ptr, self.device);
                tangentbuffer = _tangentCache.entryFor(*buffer->scenes[sceneIndex].ptr, self.device);
                sceneIndex++;
                break;
            case Ra::Buffer::kStencils:
                [commandEncoder endEncoding];
                commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:clipDescriptor];
                [commandEncoder setDepthStencilState:_stencilDepthState];
                [commandEncoder setRenderPipelineState:_stencilPipelineState];
                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                                   vertexStart:0
                                   vertexCount:3
                                 instanceCount:(entry.end - entry.begin) / sizeof(Ra::Opaque)
                                  baseInstance:0];
                
                [commandEncoder endEncoding];
                commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:drawableDescriptor];
                break;
            case Ra::Buffer::kOpaques:
                [commandEncoder setDepthStencilState:useClip ? _opaquesClipDepthState : _opaquesDepthState];
                [commandEncoder setStencilReferenceValue:0];
                [commandEncoder setRenderPipelineState:_opaquesPipelineState];
                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->widths atIndex:6];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->texCtms atIndex:8];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->texIdxs atIndex:9];
                reverse = uint32_t((entry.end - entry.begin) / sizeof(Ra::Opaque));
                [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                [commandEncoder setVertexBytes:& reverse length:sizeof(reverse) atIndex:12];
                [commandEncoder setVertexBytes:& pathsCount length:sizeof(pathsCount) atIndex:13];
                [commandEncoder setVertexBytes:& texCount length:sizeof(texCount) atIndex:14];
                [commandEncoder setVertexBytes:& buffer->params length:sizeof(Ra::Params) atIndex:15];
                [commandEncoder setFragmentTexture:useImage ? imageTexture : colorTexture atIndex:1];
                [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                   vertexStart:0
                                   vertexCount:4
                                 instanceCount:reverse
                                  baseInstance:0];
                break;
            case Ra::Buffer::kQuadEdges:
            case Ra::Buffer::kFastEdges:
            case Ra::Buffer::kFastMolecules:
            case Ra::Buffer::kQuadMolecules:
                if (entry.type == Ra::Buffer::kQuadEdges) {
                    [commandEncoder endEncoding];
                    commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:edgesDescriptor];
                    [commandEncoder setRenderPipelineState:_quadEdgesPipelineState];
                } else if (entry.type == Ra::Buffer::kFastEdges)
                    [commandEncoder setRenderPipelineState:_fastEdgesPipelineState];
                else if (entry.type == Ra::Buffer::kFastMolecules)
                    [commandEncoder setRenderPipelineState:_fastMoleculesPipelineState];
                else
                    [commandEncoder setRenderPipelineState:_quadMoleculesPipelineState];
                if (entry.end - entry.begin) {
                    assert(p16buffer != nil);
                    [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:segbase atIndex:2];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->ctms atIndex:4];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:instbase atIndex:5];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->bounds atIndex:7];
                    [commandEncoder setVertexBuffer:p16buffer offset:0 atIndex:8];
                    [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                    [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                    [commandEncoder setVertexBytes:& buffer->params length:sizeof(Ra::Params) atIndex:14];
                    [commandEncoder setFragmentBuffer:mtlBuffer offset:segbase atIndex:2];
                    [commandEncoder setFragmentBytes:& buffer->params length:sizeof(Ra::Params) atIndex:14];
                    [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                       vertexStart:0
                                       vertexCount:4
                                     instanceCount:(entry.end - entry.begin) / sizeof(Ra::Edge)
                                      baseInstance:0];
                }
                break;
            case Ra::Buffer::kInstances:
                [commandEncoder endEncoding];
                commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:drawableDescriptor];
                [commandEncoder setDepthStencilState:useClip ? _instancesClipDepthState : _instancesDepthState];
                [commandEncoder setStencilReferenceValue:0];
                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->ctms atIndex:4];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->clips atIndex:5];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->widths atIndex:6];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->bounds atIndex:7];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->texCtms atIndex:8];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->texIdxs atIndex:9];
                [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                [commandEncoder setVertexBytes:& pathsCount length:sizeof(pathsCount) atIndex:13];
                [commandEncoder setVertexBytes:& texCount length:sizeof(texCount) atIndex:14];
                [commandEncoder setVertexBytes:& buffer->params length:sizeof(Ra::Params) atIndex:15];
                [commandEncoder setVertexBuffer:strokebuffer offset:0 atIndex:20];
                [commandEncoder setVertexBuffer:tangentbuffer offset:0 atIndex:21];
                [commandEncoder setFragmentTexture:_accumulationTexture atIndex:0];
                [commandEncoder setFragmentTexture:useImage ? imageTexture : colorTexture atIndex:1];
                [commandEncoder setRenderPipelineState:_instancesPipelineState];
                [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                   vertexStart:0
                                   vertexCount:4
                                 instanceCount:(entry.end - entry.begin) / sizeof(Ra::Instance)
                                  baseInstance:0];
                break;
        }
    }
    [commandEncoder endEncoding];
    __block dispatch_semaphore_t block_sema = _inflight_semaphore;
    [commandBuffer addCompletedHandler:^(id <MTLCommandBuffer> buffer) {
        dispatch_semaphore_signal(block_sema);
    }];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
    self.accumulationTexture = nil, self.depthTexture = nil;
}
@end
