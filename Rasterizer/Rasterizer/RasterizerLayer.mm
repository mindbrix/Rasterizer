//
//  RasterizerLayer.mm
//  Rasterizer
//
//  Created by Nigel Barber on 13/12/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "RasterizerLayer.h"
#import <Metal/Metal.h>

@interface RasterizerLayer ()
{
    Ra::Buffer _buffer0, _buffer1;
}
@property (nonatomic) dispatch_semaphore_t inflight_semaphore;
@property (nonatomic) id <MTLCommandQueue> commandQueue;
@property (nonatomic) id <MTLLibrary> defaultLibrary;
@property (nonatomic) size_t tick;
@property (nonatomic) id <MTLBuffer> mtlBuffer0;
@property (nonatomic) id <MTLBuffer> mtlBuffer1;
@property (nonatomic) id <MTLRenderPipelineState> quadEdgesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> fastEdgesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> fastMoleculesPipelineState;
@property (nonatomic) id <MTLRenderPipelineState> quadMoleculesPipelineState;
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
    CGColorSpaceRef srcSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    self.colorspace = srcSpace;
    CGColorSpaceRelease(srcSpace);
    
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
    Ra::Buffer *buffer = odd ? & _buffer1 : & _buffer0;
    buffer->tick = _tick, buffer->entries.empty();
    if ([self.layerDelegate respondsToSelector:@selector(writeBuffer:forLayer:)])
        [self.layerDelegate writeBuffer:buffer forLayer:self];
    
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
        desc.pixelFormat = MTLPixelFormatR32Float;
        self.accumulationTexture = [self.device newTextureWithDescriptor:desc];
        [self.accumulationTexture setLabel:@"accumulationTexture"];
    }
    id <MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    
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
    
    for (size_t i = 0; i < buffer->entries.end; i++) {
        Ra::Buffer::Entry& entry = buffer->entries.base[i];
        switch (entry.type) {
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
                    [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:entry.segments atIndex:2];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->ctms atIndex:4];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:entry.instbase atIndex:5];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->bounds atIndex:7];
                    [commandEncoder setVertexBuffer:mtlBuffer offset:entry.points atIndex:8];
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
                [commandEncoder setDepthStencilState:_instancesDepthState];
                [commandEncoder setRenderPipelineState:_instancesPipelineState];
                [commandEncoder setVertexBuffer:mtlBuffer offset:buffer->colors atIndex:0];
                [commandEncoder setVertexBuffer:mtlBuffer offset:entry.begin atIndex:1];
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
