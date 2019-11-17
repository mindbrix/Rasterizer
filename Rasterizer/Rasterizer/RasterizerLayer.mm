//
//  MetalLayer.mm
//  Rasterizer
//
//  Created by Nigel Barber on 13/12/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "RasterizerLayer.h"
#import <Metal/Metal.h>

struct CacheEntry {
    size_t hitCount = 0;
    id <MTLBuffer> mtlScene;
};
@interface RasterizerLayer ()
{
    Ra::Buffer _buffer0, _buffer1;
    std::unordered_map<size_t, CacheEntry> cache;
}
@property (nonatomic) dispatch_semaphore_t inflight_semaphore;
@property (nonatomic) id <MTLCommandQueue> commandQueue;
@property (nonatomic) id <MTLLibrary> defaultLibrary;
@property (nonatomic) size_t tick;
@property (nonatomic) id <MTLBuffer> mtlBuffer0;
@property (nonatomic) id <MTLBuffer> mtlBuffer1;
@property (nonatomic) id <MTLRenderPipelineState> edgesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> fastEdgesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> opaquesPipelineState;
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
    
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatR16Float;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatInvalid;
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"edges_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"edges_fragment_main"];
    descriptor.label = @"edges";
    self.edgesPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"fast_edges_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"fast_edges_fragment_main"];
    descriptor.label = @"fast edges";
    self.fastEdgesPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
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
    buffer->tick = _tick, buffer->entries.empty();
    if ([self.layerDelegate respondsToSelector:@selector(writeBuffer:forLayer:)])
        [self.layerDelegate writeBuffer:buffer forLayer:self];
    
    
    Ra::SceneBuffer *scene = buffer->sceneBuffers;
    for (int i = 0; i < buffer->sceneCount; i++, scene++) {
        auto it = cache.find(scene->hash);
        if (it == cache.end()) {
            CacheEntry entry;
            entry.hitCount = 2;
            entry.mtlScene = [self.device newBufferWithBytesNoCopy:scene->base
                 length:scene->size
                options:MTLResourceStorageModeShared
            deallocator:nil];
            cache.emplace(scene->hash, entry);
        } else
            it->second.hitCount = 2;
    }
    for (auto it = cache.begin(); it != cache.end(); ) {
        if (it->second.hitCount == 0)
            it = cache.erase(it);
        else
            it->second.hitCount--, ++it;
    }

    id <MTLBuffer> mtlBuffer = odd ? _mtlBuffer1 : _mtlBuffer0;
    if (mtlBuffer.contents != buffer->base || mtlBuffer.length != buffer->size) {
        mtlBuffer = [self.device newBufferWithBytesNoCopy:buffer->base
                                                   length:buffer->size
                                                  options:MTLResourceStorageModeShared
                                              deallocator:nil];
        if (odd)
            _mtlBuffer1 = mtlBuffer;
        else
            _mtlBuffer0 = mtlBuffer;
    }
    
    id <CAMetalDrawable> drawable = [self nextDrawable];
    if (self.drawableSize.width != self.depthTexture.width || self.drawableSize.height != self.depthTexture.height) {
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
        desc.pixelFormat = MTLPixelFormatR16Float;
        self.accumulationTexture = [self.device newTextureWithDescriptor:desc];
        [self.accumulationTexture setLabel:@"accumulationTexture"];
    }
    id <MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    
    MTLRenderPassDescriptor *drawableDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    drawableDescriptor.colorAttachments[0].texture = drawable.texture;
    drawableDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    drawableDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    MTLClearColor clear = MTLClearColorMake(buffer->clearColor.src2 / 255.0, buffer->clearColor.src1 / 255.0, buffer->clearColor.src0 / 255.0, buffer->clearColor.src3 / 255.0);
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
    id <MTLBuffer> mtlScene = nil;
    
    for (size_t i = 0; i < buffer->entries.end; i++) {
        Ra::Buffer::Entry& entry = buffer->entries.base[i];
        switch (entry.type) {
            case Ra::Buffer::kSceneBuffer: {
                size_t hash = *((size_t *)(buffer->base + entry.begin));
                auto it = cache.find(hash);
                assert(it != cache.end());
                mtlScene = it->second.mtlScene;
                break;
            }
            case Ra::Buffer::kOpaques:
                [commandEncoder setDepthStencilState:_opaquesDepthState];
                [commandEncoder setRenderPipelineState:_opaquesPipelineState];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->colors atIndex:0];
                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                reverse = uint32_t((entry.end - entry.begin) / sizeof(Ra::GPU::Instance));
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
            case Ra::Buffer::kEdges:
            case Ra::Buffer::kFastEdges:
                if (entry.type == Ra::Buffer::kEdges) {
                    [commandEncoder endEncoding];
                    commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:edgesDescriptor];
                    [commandEncoder setRenderPipelineState:_edgesPipelineState];
                } else
                    [commandEncoder setRenderPipelineState:_fastEdgesPipelineState];
                if (entry.end - entry.begin) {
                    [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:entry.segments atIndex:2];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:entry.cells atIndex:3];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->transforms atIndex:4];
                    [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                    [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                    [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                       vertexStart:0
                                       vertexCount:4
                                     instanceCount:(entry.end - entry.begin) / sizeof(Ra::GPU::Edge)
                                      baseInstance:0];
                }
                if (entry.type == Ra::Buffer::kFastEdges) {
                    [commandEncoder endEncoding];
                    commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:drawableDescriptor];
                }
                break;
            case Ra::Buffer::kInstances:
                [commandEncoder setDepthStencilState:_instancesDepthState];
                [commandEncoder setRenderPipelineState:_instancesPipelineState];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->colors atIndex:0];
                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->transforms atIndex:4];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->clips atIndex:5];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->widths atIndex:6];
                [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                [commandEncoder setVertexBytes:& pathsCount length:sizeof(pathsCount) atIndex:13];
                [commandEncoder setVertexBytes:& buffer->useCurves length:sizeof(bool) atIndex:14];
                [commandEncoder setFragmentTexture:_accumulationTexture atIndex:0];
                [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                   vertexStart:0
                                   vertexCount:4
                                 instanceCount:(entry.end - entry.begin) / sizeof(Ra::GPU::Instance)
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
