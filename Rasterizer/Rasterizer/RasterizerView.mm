//
//  RasterizerView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 29/11/2023.
//  Copyright © 2023 @mindbrix. All rights reserved.
//

#import "RasterizerView.h"
#import "RasterizerLayer.h"
#import "RasterizerRenderer.hpp"


@interface RasterizerView () <CALayerDelegate, LayerDelegate>

@property(nonatomic) RasterizerRenderer renderer;

@end


@implementation RasterizerView

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
   self = [super initWithCoder:decoder];
   if (! self)
       return nil;
    _useCG = false;
    _useCurves = true;
    _useFastOutlines = true;
    _ctm = Ra::Transform();
   [self initLayer:_useCG];
   return self;
}

- (void)initLayer:(BOOL)useCPU {
    [self setWantsLayer:YES];
    CGFloat scale = self.layer.contentsScale ?: [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    if (useCPU) {
        [self setLayer:[CALayer layer]];
        self.layer.contentsFormat = kCAContentsFormatRGBA8Uint;
        self.layer.delegate = self;
        self.layer.magnificationFilter = kCAFilterNearest;
    } else {
        [self setLayer:[RasterizerLayer layer]];
        ((RasterizerLayer *)self.layer).layerDelegate = self;
    }
    self.layer.contentsScale = scale;
    self.layer.bounds = self.bounds;
    self.layer.opaque = YES;
    self.layer.needsDisplayOnBoundsChange = YES;
    self.layer.actions = @{ @"onOrderIn": [NSNull null], @"onOrderOut": [NSNull null], @"sublayers": [NSNull null], @"contents": [NSNull null], @"backgroundColor": [NSNull null], @"bounds": [NSNull null] };
    [self.layer setNeedsDisplay];
}

#pragma mark - LayerDelegate

- (CGColorSpaceRef)writeBuffer:(Ra::Buffer *)buffer forLayer:(CALayer *)layer {
    buffer->clearColor = Ra::Colorant(0xFF, 0xFF, 0xFF, 0xFF);
    buffer->useCurves = _useCurves, buffer->fastOutlines = _useFastOutlines;
    
    float scale = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
    Ra::Bounds device = Ra::Bounds(0.f, 0.f, ceilf(scale * w), ceilf(scale * h));
    Ra::Transform view = Ra::Transform(scale, 0.f, 0.f, scale, 0.f, 0.f).concat(_ctm);
    _renderer.renderList(_sceneList, device, view, buffer);
    return self.window.colorSpace.CGColorSpace;
}

#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    Ra::Colorant color = Ra::Colorant(0xFF, 0xFF, 0xFF, 0xFF);
    memset_pattern4(CGBitmapContextGetData(ctx), & color.b, CGBitmapContextGetBytesPerRow(ctx) * CGBitmapContextGetHeight(ctx));
    CGContextConcatCTM(ctx, RaCG::CGFromTransform(_ctm));
    
    float scale = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
    Ra::Bounds device = Ra::Bounds(0.f, 0.f, ceilf(scale * w), ceilf(scale * h));
    Ra::Transform view = Ra::Transform(scale, 0.f, 0.f, scale, 0.f, 0.f).concat(_ctm);
    RaCG::drawList(_sceneList, view, device, 0.f, ctx);
}


#pragma mark - Properies

- (void)setSceneList:(Ra::SceneList)sceneList {
    _sceneList = sceneList;
    [self.layer setNeedsDisplay];
}

- (void)setUseCG:(bool)useCG {
    _useCG = useCG;
    [self initLayer:_useCG];
}

@end
