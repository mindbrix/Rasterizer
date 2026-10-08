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

template<typename T, typename S, int kExpiryAge = 10>
struct MetalCache {
    struct Entry {
        Entry(T payload) : payload(payload), timestamp(CACurrentMediaTime()) {}
        
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
            it->second.timestamp = CACurrentMediaTime();
            return it->second.payload;
        }
    }
    
    void flush() {
        double now = CACurrentMediaTime();
        std::vector<size_t> expired;
        for (const auto& entry: map)
            if (now - entry.second.timestamp > kExpiryAge)
                expired.emplace_back(entry.first);
        for (auto key: expired)
            map.erase(key);
    }
    
    std::map<size_t, Entry> map;
};

#pragma clang diagnostic ignored "-Wextra"

struct TextureCache : MetalCache<id <MTLTexture>, const Ra::Paint &> {
    __strong id <MTLTexture> createPayload(const Ra::Paint & image, id <MTLDevice> device) override {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                         width:image.bitmap->w
                                        height:image.bitmap->h
                                     mipmapped:NO];
        desc.storageMode = MTLStorageModeShared;
        desc.usage = MTLTextureUsageShaderRead;
        
        auto texture = [device newTextureWithDescriptor:desc];
        [texture replaceRegion:MTLRegionMake2D(0, 0, image.bitmap->w, image.bitmap->h)
                        mipmapLevel:0
                          withBytes:& image.bitmap->colors[0]
                        bytesPerRow:image.bitmap->w * sizeof(Ra::Color)];
        return texture;
    }
};


@interface RasterizerLayer ()
{
    RenderBuffer _buffer0, _buffer1;
    TextureCache _textureCache;
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
@property (nonatomic) id <MTLRenderPipelineState> instancesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> opaquesMaskedPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> instancesMaskedPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> instancesClipPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> clipMaskPipelineState;
@property (nonatomic) id <MTLDepthStencilState> instancesDepthState;
@property (nonatomic) id <MTLDepthStencilState> opaquesDepthState;
@property (nonatomic) id <MTLDepthStencilState> clipMaskDepthState;
@property (nonatomic) id <MTLTexture> depthTexture;
@property (nonatomic) id <MTLTexture> accumulationTexture;
@property (nonatomic) id <MTLTexture> clipMaskTexture;

@end


@implementation RasterizerLayer

- (id)init {
    self = [super init];
    if (!self)
        return nil;
    
    self.device = MTLCreateSystemDefaultDevice();
    _buffer0.device =  self.device;
    _buffer1.device =  self.device;
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
    
    MTLDepthStencilDescriptor *depthStencilDescriptor = [MTLDepthStencilDescriptor new];
    depthStencilDescriptor.depthWriteEnabled = YES;
    depthStencilDescriptor.depthCompareFunction = MTLCompareFunctionGreater;
    self.opaquesDepthState = [self.device newDepthStencilStateWithDescriptor:depthStencilDescriptor];

    depthStencilDescriptor.depthWriteEnabled = NO;
    self.instancesDepthState = [self.device newDepthStencilStateWithDescriptor:depthStencilDescriptor];
    
    depthStencilDescriptor.depthCompareFunction = MTLCompareFunctionAlways;    // Clip mask cells are always written, & never write depth
    self.clipMaskDepthState = [self.device newDepthStencilStateWithDescriptor:depthStencilDescriptor];
    
    // A frame with clip masks renders them in its drawable passes, into the clip mask as color attachment 1, so its pipelines have
    // that attachment, which only clip mask instances write. Clipped instances read it with framebuffer fetch
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.colorAttachments[0].pixelFormat = self.pixelFormat;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    descriptor.colorAttachments[1].writeMask = MTLColorWriteMaskNone;
    for (int masked = 0; masked < 2; masked++) {
        descriptor.colorAttachments[1].pixelFormat = masked ? MTLPixelFormatR8Unorm : MTLPixelFormatInvalid;
        descriptor.colorAttachments[0].blendingEnabled = NO;
        descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"opaques_vertex_main"];
        descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"opaques_fragment_main"];
        descriptor.label = masked ? @"opaques masked" : @"opaques";
        id <MTLRenderPipelineState> opaques = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
        
        descriptor.colorAttachments[0].blendingEnabled = YES;
        descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
        descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"instances_vertex_main"];
        descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"instances_fragment_main"];
        descriptor.label = masked ? @"instances masked" : @"instances";
        id <MTLRenderPipelineState> instances = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
        if (masked)
            self.opaquesMaskedPipelineState = opaques, self.instancesMaskedPipelineState = instances;
        else
            self.opaquesPipelineState = opaques, self.instancesPipelineState = instances;
    }
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"instances_clip_fragment_main"];
    descriptor.label = @"instances clip";
    self.instancesClipPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    // A clip path's cells don't overlap, so each mask pixel is written once, without blending, after its kClipClear cell zeroes it
    MTLRenderPipelineDescriptor *maskDescriptor = [MTLRenderPipelineDescriptor new];
    maskDescriptor.colorAttachments[0].pixelFormat = self.pixelFormat;
    maskDescriptor.colorAttachments[0].writeMask = MTLColorWriteMaskNone;
    maskDescriptor.colorAttachments[1].pixelFormat = MTLPixelFormatR8Unorm;
    maskDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    maskDescriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"instances_vertex_main"];
    maskDescriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"clip_mask_fragment_main"];
    maskDescriptor.label = @"clip mask";
    self.clipMaskPipelineState = [self.device newRenderPipelineStateWithDescriptor:maskDescriptor error:nil];
    
    descriptor.colorAttachments[1].pixelFormat = MTLPixelFormatInvalid;
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatR32Float;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatInvalid;
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
    RenderBuffer *renderBuffer = odd ? & _buffer1 : & _buffer0;
    Ra::Buffer *buffer = & renderBuffer->buffer;

    if ([self.layerDelegate respondsToSelector:@selector(writeBuffer:forLayer:)])
        [self.layerDelegate writeBuffer:renderBuffer forLayer:self];
    
    id <MTLBuffer> mtlBuffer = renderBuffer->mtlBuffer;
    
    id <CAMetalDrawable> drawable = [self nextDrawable];
    
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                     width:self.drawableSize.width
                                    height:self.drawableSize.height
                                 mipmapped:NO];
    
    if (self.drawableSize.width != self.depthTexture.width || self.drawableSize.height != self.depthTexture.height) {
        desc.storageMode = MTLStorageModePrivate;
        desc.usage = MTLTextureUsageRenderTarget;
        self.depthTexture = [self.device newTextureWithDescriptor:desc];
        [self.depthTexture setLabel:@"depthTexture"];
        
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        desc.pixelFormat = MTLPixelFormatR32Float;
        self.accumulationTexture = [self.device newTextureWithDescriptor:desc];
        [self.accumulationTexture setLabel:@"accumulationTexture"];
        
        desc.usage = MTLTextureUsageRenderTarget;
        desc.pixelFormat = MTLPixelFormatR8Unorm;
        self.clipMaskTexture = [self.device newTextureWithDescriptor:desc];
        [self.clipMaskTexture setLabel:@"clipMaskTexture"];
    }
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageShaderRead;
    desc.pixelFormat = MTLPixelFormatBGRA8Unorm;
    size_t w = kColorTextureWidth, h = (buffer->pathsCount + w - 0) / w, th = buffer->texCount;
    desc.width = w;
    desc.height = h + th;
    id <MTLTexture> colorTexture = [self.device newTextureWithDescriptor:desc];
    if (buffer->base) {
        [colorTexture replaceRegion:MTLRegionMake2D(0, 0, w, h)
                        mipmapLevel:0
                          withBytes:buffer->base + buffer->colors
                        bytesPerRow:w * sizeof(Ra::Color)];
        if (buffer->texCount) {
            [colorTexture replaceRegion:MTLRegionMake2D(0, h, w, th)
                            mipmapLevel:0
                              withBytes:buffer->base + buffer->texStrips
                            bytesPerRow:w * sizeof(Ra::Color)];
        }
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
    
    // The clip mask holds the current clip path's coverage within its bounds, which its kClipClear cell zeroes beneath it. Clipped
    // instances treat the mask as zero outside them, so it's never cleared. Frames without clip masks don't attach it. Without
    // framebuffer fetch, e.g. in the iOS Simulator, the clipped instances pipeline is nil, so clip paths are ignored
    bool canClip = _instancesClipPipelineState != nil, hasMasks = false;
    for (size_t i = 0; i < buffer->entries.end && canClip && !hasMasks; i++)
        hasMasks = buffer->entries.base[i].type == Ra::Buffer::kClipMask;
    if (hasMasks) {
        drawableDescriptor.colorAttachments[1].texture = _clipMaskTexture;
        drawableDescriptor.colorAttachments[1].loadAction = MTLLoadActionDontCare;
        drawableDescriptor.colorAttachments[1].storeAction = MTLStoreActionStore;    // Edges passes interrupt the drawable passes
    }
    id <MTLRenderPipelineState> opaquesPipelineState = hasMasks ? _opaquesMaskedPipelineState : _opaquesPipelineState;
    id <MTLRenderPipelineState> instancesPipelineState = hasMasks ? _instancesMaskedPipelineState : _instancesPipelineState;
    
    id <MTLRenderCommandEncoder> commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:drawableDescriptor];
    
    drawableDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
    drawableDescriptor.colorAttachments[1].loadAction = MTLLoadActionLoad;
    drawableDescriptor.depthAttachment.loadAction = MTLLoadActionLoad;
    
    MTLRenderPassDescriptor *edgesDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    edgesDescriptor.colorAttachments[0].texture = _accumulationTexture;
    edgesDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    edgesDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    edgesDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    
    MTLScissorRect maskBounds = { 0, 0, 0, 0 };
    size_t maskID = 0;    // The identity of the mask the clip mask holds, as contexts beginning with the last one's clip repeat it
    bool skipMask = false;
    
    bool useClip = false, useImage = false, inMask = false;
    // Render passes switch only when their kind changes, as each switch stores & reloads its attachments. Edges always begin a pass
    enum PassKind { kDrawablePass, kEdgesPass } passKind = kDrawablePass;
    auto beginPass = [&](PassKind kind) {
        if (passKind == kind && kind != kEdgesPass)
            return;
        [commandEncoder endEncoding];
        commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:kind == kEdgesPass ? edgesDescriptor : drawableDescriptor];
        passKind = kind;
    };
    uint32_t reverse, pathsCount = uint32_t(buffer->pathsCount), texCount = uint32_t(th);
    float width = drawable.texture.width, height = drawable.texture.height;
    
    NSUInteger imgIndex = 0;
    id <MTLTexture> imageTexture = nil;
    
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
            case Ra::Buffer::kEnableClip:
                inMask = skipMask = false, useClip = canClip && entry.type == Ra::Buffer::kEnableClip;
                break;
            case Ra::Buffer::kClipMask: {
                inMask = true;
                if ((skipMask = !canClip || entry.end == maskID))
                    break;
                maskID = entry.end;
                NSUInteger lx = entry.begin & 0xFFFF, ly = (entry.begin >> 16) & 0xFFFF, ux = (entry.begin >> 32) & 0xFFFF, uy = (entry.begin >> 48) & 0xFFFF;
                ux = MIN(ux, NSUInteger(width)), uy = MIN(uy, NSUInteger(height)), lx = MIN(lx, ux), ly = MIN(ly, uy);
                maskBounds = { lx, NSUInteger(height) - uy, ux - lx, uy - ly };    // Device space is y up
                break;
            }
            case Ra::Buffer::kDisableImage:
                useImage = false;
                break;
            case Ra::Buffer::kNextImage:
                imageTexture = _textureCache.entryFor(buffer->images[imgIndex++], self.device);
                useImage = true;
                break;
            case Ra::Buffer::kOpaques:
                beginPass(kDrawablePass);
                [commandEncoder setDepthStencilState:_opaquesDepthState];
                [commandEncoder setRenderPipelineState:opaquesPipelineState];
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
                    beginPass(kEdgesPass);
                    [commandEncoder setRenderPipelineState:_quadEdgesPipelineState];
                } else if (entry.type == Ra::Buffer::kFastEdges)
                    [commandEncoder setRenderPipelineState:_fastEdgesPipelineState];
                else if (entry.type == Ra::Buffer::kFastMolecules)
                    [commandEncoder setRenderPipelineState:_fastMoleculesPipelineState];
                else
                    [commandEncoder setRenderPipelineState:_quadMoleculesPipelineState];
                if (entry.end - entry.begin) {
                    [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:segbase atIndex:2];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->ctms atIndex:4];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:instbase atIndex:5];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->bounds atIndex:7];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->p16s atIndex:8];
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
                if (skipMask)
                    break;
                beginPass(kDrawablePass);
                if (inMask) {
                    [commandEncoder setDepthStencilState:_clipMaskDepthState];
                    [commandEncoder setRenderPipelineState:_clipMaskPipelineState];
                } else {
                    [commandEncoder setDepthStencilState:_instancesDepthState];
                    [commandEncoder setRenderPipelineState:useClip ? _instancesClipPipelineState : instancesPipelineState];
                    uint32_t bounds[4] = { uint32_t(maskBounds.x), uint32_t(maskBounds.y), uint32_t(maskBounds.width), uint32_t(maskBounds.height) };
                    [commandEncoder setFragmentBytes:bounds length:sizeof(bounds) atIndex:0];
                }
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
                [commandEncoder setFragmentTexture:_accumulationTexture atIndex:0];
                [commandEncoder setFragmentTexture:useImage ? imageTexture : colorTexture atIndex:1];
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
}
@end
