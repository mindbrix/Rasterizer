//
//  MetalLayer.mm
//  Rasterizer
//
//  Created by Nigel Barber on 13/12/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "MetalLayer.h"
#import <Metal/Metal.h>

@interface MetalLayer ()
{
    Rasterizer::Buffer _buffer0;
    Rasterizer::Buffer _buffer1;
}
@property (nonatomic) dispatch_semaphore_t inflight_semaphore;
@property (nonatomic) id <MTLCommandQueue> commandQueue;
@property (nonatomic) id <MTLLibrary> defaultLibrary;
@property (nonatomic) BOOL odd;
@property (nonatomic) id <MTLBuffer> mtlBuffer0;
@property (nonatomic) id <MTLBuffer> mtlBuffer1;
@property (nonatomic) id <MTLRenderPipelineState> edgesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> quadsPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> opaquesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> shapesPipelineState;
@property (nonatomic) id <MTLDepthStencilState> quadsDepthState;
@property (nonatomic) id <MTLDepthStencilState> opaquesDepthState;
@property (nonatomic) id <MTLTexture> depthTexture;
@property (nonatomic) id <MTLTexture> accumulationTexture;

@end


@implementation MetalLayer

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
    self.quadsDepthState = [self.device newDepthStencilStateWithDescriptor:depthStencilDescriptor];
    
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
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"quads_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"quads_fragment_main"];
    descriptor.label = @"quads";
    self.quadsPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"shapes_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"shapes_fragment_main"];
    descriptor.label = @"shapes";
    self.shapesPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatR32Float;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatInvalid;
    descriptor.vertexFunction = [self.defaultLibrary newFunctionWithName:@"edges_vertex_main"];
    descriptor.fragmentFunction = [self.defaultLibrary newFunctionWithName:@"edges_fragment_main"];
    descriptor.label = @"edges";
    self.edgesPipelineState = [self.device newRenderPipelineStateWithDescriptor:descriptor error:nil];
    
    return self;
}

- (void)display {
    @autoreleasepool {
        self.drawableSize = CGSizeMake(ceil(self.bounds.size.width * self.contentsScale), ceil(self.bounds.size.height * self.contentsScale));
        if (dispatch_semaphore_wait(_inflight_semaphore, DISPATCH_TIME_NOW))
            return;
        [self draw];
    }
}

- (void)draw {
    _odd = !_odd;
    Rasterizer::Buffer *buffer = _odd ? & _buffer1 : & _buffer0;
    buffer->entries.empty();
    if ([self.layerDelegate respondsToSelector:@selector(writeBuffer:forLayer:)])
        [self.layerDelegate writeBuffer:buffer forLayer:self];
    
    id <CAMetalDrawable> drawable = [self nextDrawable];
    if (drawable == nil) {
        dispatch_semaphore_signal(_inflight_semaphore);
        return;
    }
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
        desc.pixelFormat = MTLPixelFormatR32Float;
        self.accumulationTexture = [self.device newTextureWithDescriptor:desc];
        [self.accumulationTexture setLabel:@"accumulationTexture"];
    }
    
    id <MTLBuffer> mtlBuffer = _odd ? _mtlBuffer1 : _mtlBuffer0;
    if (mtlBuffer.contents != buffer->data.base || mtlBuffer.length != buffer->data.size) {
        mtlBuffer = [self.device newBufferWithBytesNoCopy:buffer->data.base
                                                   length:buffer->data.size
                                                  options:MTLResourceStorageModeShared
                                              deallocator:nil];
        if (_odd)
            _mtlBuffer1 = mtlBuffer;
        else
            _mtlBuffer0 = mtlBuffer;
    }
    
    id <MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    
    MTLRenderPassDescriptor *drawableDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    drawableDescriptor.colorAttachments[0].texture = drawable.texture;
    drawableDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    drawableDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    drawableDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(buffer->clearColor.src2 / 255.0, buffer->clearColor.src1 / 255.0, buffer->clearColor.src0 / 255.0, buffer->clearColor.src3 / 255.0);
    drawableDescriptor.depthAttachment.texture = _depthTexture;
    drawableDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
    drawableDescriptor.depthAttachment.storeAction = MTLStoreActionStore;
    drawableDescriptor.depthAttachment.clearDepth = 0;
    
    MTLRenderPassDescriptor *edgesDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    edgesDescriptor.colorAttachments[0].texture = _accumulationTexture;
    edgesDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    edgesDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    edgesDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    
    id <MTLRenderCommandEncoder> commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:drawableDescriptor];
    drawableDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
    drawableDescriptor.depthAttachment.loadAction = MTLLoadActionLoad;
    
    uint32_t reverse, pathCount;
    size_t colorantsOffset = 0, segmentsOffset = 0, edgeCellsOffset = 0;
    float width = drawable.texture.width, height = drawable.texture.height;
    Rasterizer::AffineTransform clip;
    for (size_t i = 0; i < buffer->entries.end; i++) {
        Rasterizer::Buffer::Entry& entry = buffer->entries.base[i];
        switch (entry.type) {
            case Rasterizer::Buffer::Entry::kColorants:
                pathCount = uint32_t((entry.end - entry.begin) / sizeof(Rasterizer::Colorant));
                colorantsOffset = entry.begin;
                break;
            case Rasterizer::Buffer::Entry::kSegments:
                segmentsOffset = entry.begin;
                break;
            case Rasterizer::Buffer::Entry::kEdgeCells:
                edgeCellsOffset = entry.begin;
                break;
            case Rasterizer::Buffer::Entry::kOpaques:
                [commandEncoder setDepthStencilState:_opaquesDepthState];
                [commandEncoder setRenderPipelineState:_opaquesPipelineState];
                [commandEncoder setVertexBuffer:mtlBuffer offset:colorantsOffset atIndex:0];
                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                reverse = uint32_t((entry.end - entry.begin) / sizeof(Rasterizer::GPU::Quad));
                [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                [commandEncoder setVertexBytes:& reverse length:sizeof(reverse) atIndex:12];
                [commandEncoder setVertexBytes:& pathCount length:sizeof(pathCount) atIndex:13];
                [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                   vertexStart:0
                                   vertexCount:4
                                 instanceCount:reverse
                                  baseInstance:0];
                break;
            case Rasterizer::Buffer::Entry::kEdges:
                [commandEncoder endEncoding];
                commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:edgesDescriptor];
                [commandEncoder setRenderPipelineState:_edgesPipelineState];
                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                [commandEncoder setVertexBuffer:mtlBuffer offset:segmentsOffset atIndex:2];
                [commandEncoder setVertexBuffer:mtlBuffer offset:edgeCellsOffset atIndex:3];
                [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                   vertexStart:0
                                   vertexCount:4
                                 instanceCount:(entry.end - entry.begin) / sizeof(Rasterizer::GPU::Edge)
                                  baseInstance:0];
                [commandEncoder endEncoding];
                commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:drawableDescriptor];
                break;
            case Rasterizer::Buffer::Entry::kQuads:
                [commandEncoder setDepthStencilState:_quadsDepthState];
                [commandEncoder setRenderPipelineState:_quadsPipelineState];
                [commandEncoder setVertexBuffer:mtlBuffer offset:colorantsOffset atIndex:0];
                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                [commandEncoder setVertexBytes:& pathCount length:sizeof(pathCount) atIndex:13];
                [commandEncoder setFragmentTexture:_accumulationTexture atIndex:0];
                [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                   vertexStart:0
                                   vertexCount:4
                                 instanceCount:(entry.end - entry.begin) / sizeof(Rasterizer::GPU::Quad)
                                  baseInstance:0];
                break;
            case Rasterizer::Buffer::Entry::kShapes:
                [commandEncoder setDepthStencilState:_quadsDepthState];
                [commandEncoder setRenderPipelineState:_shapesPipelineState];
                [commandEncoder setVertexBuffer:mtlBuffer offset:colorantsOffset atIndex:0];
                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                [commandEncoder setVertexBytes:& width length:sizeof(width) atIndex:10];
                [commandEncoder setVertexBytes:& height length:sizeof(height) atIndex:11];
                [commandEncoder setVertexBytes:& pathCount length:sizeof(pathCount) atIndex:13];
                [commandEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                   vertexStart:0
                                   vertexCount:4
                                 instanceCount:(entry.end - entry.begin) / sizeof(Rasterizer::GPU::Quad)
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
