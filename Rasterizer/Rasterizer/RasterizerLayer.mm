//
//  RasterizerLayer.mm
//  Rasterizer
//
//  Created by Nigel Barber on 13/12/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "RasterizerLayer.h"
#import <Metal/Metal.h>

struct TextureCache {
    struct Entry {
        Entry(size_t hash, id <MTLTexture> texture) : hash(hash), texture(texture) {}
        ~Entry() {  texture = nil;  }
        size_t hash = 0;
        id <MTLTexture> texture = nil;
        inline bool operator< (const Entry& other) const { return hash < other.hash; }
    };

    std::vector<Entry> update(Ra::Buffer *buffer, CGColorSpaceRef colorSpace, id<MTLDevice> device, RaCG::Converter& converter) {
        std::vector<Entry> mipmaps;
        if (buffer->images.end) {
            size_t textureSize = textures.size(), hash, i = 0, j = 0;
            Ra::Image *images = buffer->images.base, *img;
            Ra::Image::Index *indices = buffer->indices.base;
            do {
                hash = indices[i].hash;
                while (j < textureSize && textures[j].hash < hash)
                    textures[j].hash = ~0, j++;
                if (j == textureSize || textures[j].hash != hash) {
                    img = images + indices[i].i;
                    Ra::Image tex;  tex.init(img->memory->addr, img->width, img->height, img->width);
                    converter.matchColors((Ra::Colorant *)tex.memory->addr, img->width * img->height, colorSpace);
                    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat: MTLPixelFormatBGRA8Unorm
                                                                                                    width: tex.width
                                                                                                   height: tex.height
                                                                                                mipmapped: YES];
                    desc.storageMode = MTLStorageModeShared;
                    textures.emplace_back(tex.hash, [device newTextureWithDescriptor:desc]);
                    auto& entry = textures.back();
                    [entry.texture replaceRegion: MTLRegionMake2D(0, 0, tex.width, tex.height)
                                   mipmapLevel: 0
                                     withBytes: tex.memory->addr
                                   bytesPerRow: tex.memory->size / tex.height];
                    if (entry.texture.mipmapLevelCount > 1)
                        mipmaps.emplace_back(entry);
                } else
                    j++;
                while (i < buffer->indices.end && hash == indices[i].hash)
                    i++;
            } while (i < buffer->indices.end);
            
            while (j < textureSize)
                textures[j].hash = ~0, j++;
            
            std::sort(textures.begin(), textures.end());
            while (textures.size() && textures.back().hash == ~0)
                textures.pop_back();
        }
        return mipmaps;
    }
    std::vector<Entry> textures;
};


@interface RasterizerLayer ()
{
    Ra::Buffer _buffer0, _buffer1;
    RaCG::Converter _converter;
    TextureCache _textureCache;
}

@property (nonatomic) dispatch_semaphore_t inflight_semaphore;
@property (nonatomic) id <MTLCommandQueue> commandQueue;
@property (nonatomic) id <MTLLibrary> defaultLibrary;
@property (nonatomic) size_t tick;
@property (nonatomic) id <MTLRenderPipelineState> quadEdgesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> fastEdgesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> fastOutlinesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> quadOutlinesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> fastMoleculesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> quadMoleculesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> opaquesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> instancesTransformState;
@property (nonatomic) id <MTLRenderPipelineState> instancesPipelineState;
@property (nonatomic) id <MTLDepthStencilState> instancesDepthState;
@property (nonatomic) id <MTLDepthStencilState> opaquesDepthState;
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
    self.defaultLibrary = [self.device newDefaultLibrary];
    self.inflight_semaphore = dispatch_semaphore_create(2);
    
    MTLDepthStencilDescriptor *depthStencilDescriptor = [MTLDepthStencilDescriptor new];
    depthStencilDescriptor.depthWriteEnabled = YES;
    depthStencilDescriptor.depthCompareFunction = MTLCompareFunctionGreaterEqual;
    self.opaquesDepthState = [self.device newDepthStencilStateWithDescriptor:depthStencilDescriptor];
    depthStencilDescriptor.depthWriteEnabled = NO;
    self.instancesDepthState = [self.device newDepthStencilStateWithDescriptor:depthStencilDescriptor];
    
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.colorAttachments[0].pixelFormat = self.pixelFormat;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
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
    
//    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"instances_transform_main"];
//    descriptor.fragmentFunction = nil;
//    descriptor.rasterizationEnabled = NO;
//    descriptor.label = @"instances transform";
//    self.instancesTransformState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
//    descriptor.rasterizationEnabled = YES;
    
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
    descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationMax;
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"fast_molecules_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"fast_outlines_fragment_main"];
    descriptor.label = @"fast outlines";
    self.fastOutlinesPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"quad_molecules_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"quad_outlines_fragment_main"];
    descriptor.label = @"quad outlines";
    self.quadOutlinesPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
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
    BOOL odd = ++_tick & 1;//, isM1 = [self.device.name hasPrefix:@"Apple M1"];
    Ra::Buffer *buffer = odd ? & _buffer1 : & _buffer0;
    CGColorSpaceRef colorSpace = nil;
    if ([self.layerDelegate respondsToSelector:@selector(writeBuffer:forLayer:)])
        colorSpace = [self.layerDelegate writeBuffer:buffer forLayer:self];
    
    if (buffer->size == 0) {
        dispatch_semaphore_signal(_inflight_semaphore);
        return;
    }
    id <MTLBuffer> mtlBuffer = [self.device newBufferWithBytesNoCopy:buffer->base
                                               length:buffer->size
                                              options:MTLResourceStorageModeShared
                                          deallocator:nil];

    _converter.matchColors(buffer->_colors, buffer->pathsCount, colorSpace);
    auto mipmaps = _textureCache.update(buffer, colorSpace, self.device, _converter);
    
    id <CAMetalDrawable> drawable = [self nextDrawable];
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                    width:self.drawableSize.width
                                                                                   height:self.drawableSize.height
                                                                                mipmapped:NO];
    desc.storageMode = MTLStorageModePrivate;
    desc.usage = MTLTextureUsageRenderTarget;
    self.depthTexture = [self.device newTextureWithDescriptor:desc];
    [self.depthTexture setLabel:@"depthTexture"];
    
    desc.width = self.drawableSize.width;
    desc.height = self.drawableSize.height;
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.pixelFormat = MTLPixelFormatR32Float;
    self.accumulationTexture = [self.device newTextureWithDescriptor:desc];
    [self.accumulationTexture setLabel:@"accumulationTexture"];
    
    id <MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    
    if (mipmaps.size()) {
        id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
        for (auto& mipmap: mipmaps)
            [blitEncoder generateMipmapsForTexture: mipmap.texture];
        [blitEncoder endEncoding];
    }
    MTLRenderPassDescriptor *drawableDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    drawableDescriptor.colorAttachments[0].texture = drawable.texture;
    drawableDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    drawableDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    MTLClearColor clear = MTLClearColorMake(buffer->clearColor.r / 255.0, buffer->clearColor.g / 255.0, buffer->clearColor.b / 255.0, buffer->clearColor.a / 255.0);
    drawableDescriptor.colorAttachments[0].clearColor = clear;
    drawableDescriptor.depthAttachment.texture = _depthTexture;
    drawableDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
    drawableDescriptor.depthAttachment.storeAction = MTLStoreActionStore;
    drawableDescriptor.depthAttachment.clearDepth = 0;
    id <MTLRenderCommandEncoder> commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:drawableDescriptor];
    drawableDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
    drawableDescriptor.depthAttachment.loadAction = MTLLoadActionLoad;
    MTLRenderPassDescriptor *edgesDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    edgesDescriptor.colorAttachments[0].texture = _accumulationTexture;
    edgesDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    edgesDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    edgesDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    
    uint32_t reverse, pathsCount = uint32_t(buffer->pathsCount);
    float width = drawable.texture.width, height = drawable.texture.height;
    
    for (size_t segbase = 0, ptsbase = 0, instbase = 0, i = 0; i < buffer->entries.end; i++) {
        Ra::Buffer::Entry& entry = buffer->entries.base[i];
        switch (entry.type) {
            case Ra::Buffer::kSegmentsBase:
                segbase = entry.begin;
                break;
            case Ra::Buffer::kPointsBase:
                ptsbase = entry.begin;
                break;
            case Ra::Buffer::kInstancesBase:
                instbase = entry.begin;
                break;
            case Ra::Buffer::kOpaques:
                [commandEncoder setDepthStencilState:_opaquesDepthState];
                [commandEncoder setRenderPipelineState:_opaquesPipelineState];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->colors atIndex:0];
                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                reverse = uint32_t((entry.end - entry.begin) / sizeof(Ra::Instance));
                [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                [commandEncoder setVertexBytes:& reverse length:sizeof(reverse) atIndex:12];
                [commandEncoder setVertexBytes:& pathsCount length:sizeof(pathsCount) atIndex:13];
                [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                   vertexStart:0
                                   vertexCount:4
                                 instanceCount:reverse
                                  baseInstance:0];
                break;
            case Ra::Buffer::kQuadEdges:
            case Ra::Buffer::kFastEdges:
            case Ra::Buffer::kFastOutlines:
            case Ra::Buffer::kQuadOutlines:
            case Ra::Buffer::kFastMolecules:
            case Ra::Buffer::kQuadMolecules:
                if (entry.type == Ra::Buffer::kQuadEdges) {
                    [commandEncoder endEncoding];
                    commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:edgesDescriptor];
                    [commandEncoder setRenderPipelineState:_quadEdgesPipelineState];
                } else if (entry.type == Ra::Buffer::kFastEdges)
                    [commandEncoder setRenderPipelineState:_fastEdgesPipelineState];
                else if (entry.type == Ra::Buffer::kFastOutlines)
                    [commandEncoder setRenderPipelineState:_fastOutlinesPipelineState];
                else if (entry.type == Ra::Buffer::kQuadOutlines)
                    [commandEncoder setRenderPipelineState:_quadOutlinesPipelineState];
                else if (entry.type == Ra::Buffer::kFastMolecules)
                    [commandEncoder setRenderPipelineState:_fastMoleculesPipelineState];
                else
                    [commandEncoder setRenderPipelineState:_quadMoleculesPipelineState];
                if (entry.end - entry.begin) {
                    [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:segbase atIndex:2];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->ctms atIndex:4];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:instbase atIndex:5];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->widths atIndex:6];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->bounds atIndex:7];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:ptsbase atIndex:8];
                    [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                    [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                    [commandEncoder setVertexBytes:& buffer->useCurves length:sizeof(bool) atIndex:14];
                    [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                       vertexStart:0
                                       vertexCount:4
                                     instanceCount:(entry.end - entry.begin) / sizeof(Ra::Edge)
                                      baseInstance:0];
                }
                if (entry.type == Ra::Buffer::kQuadMolecules) {
                    [commandEncoder endEncoding];
                    commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:drawableDescriptor];
                }
                break;
            case Ra::Buffer::kInstances:
//                [commandEncoder endEncoding];
//                commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:drawableDescriptor];
//                [commandEncoder setRenderPipelineState:_instancesTransformState];
//                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
//                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.end atIndex:20];
//                [commandEncoder setVertexBytes:& buffer->useCurves length:sizeof(bool) atIndex:14];
//                [commandEncoder drawPrimitives:isM1 ? MTLPrimitiveTypeTriangleStrip : MTLPrimitiveTypePoint
//                                   vertexStart:0
//                                   vertexCount:isM1 ? 4 : 1
//                                  instanceCount:(entry.end - entry.begin) / sizeof(Ra::Instance)
//                                  baseInstance:0];
                [commandEncoder endEncoding];
                commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:drawableDescriptor];
                [commandEncoder setDepthStencilState:_instancesDepthState];
                [commandEncoder setRenderPipelineState:_instancesPipelineState];
                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.end atIndex:20];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->clips atIndex:5];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->widths atIndex:6];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->slots atIndex:8];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->texctms atIndex:9];
                [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                [commandEncoder setVertexBytes:& pathsCount length:sizeof(pathsCount) atIndex:13];
                [commandEncoder setVertexBytes:& buffer->useCurves length:sizeof(bool) atIndex:14];
                [commandEncoder setFragmentBuffer:mtlBuffer offset:buffer->colors atIndex:0];
                [commandEncoder setFragmentTexture:_accumulationTexture atIndex:0];
                if (_textureCache.textures.size()) {
                    id <MTLTexture> textures[kTextureSlotsSize];
                    for (int i = 0; i < kTextureSlotsSize; i++)
                        textures[i] = _textureCache.textures[i % _textureCache.textures.size()].texture;
                    [commandEncoder setFragmentTextures:textures withRange:NSMakeRange(2, kTextureSlotsSize)];
                }
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
