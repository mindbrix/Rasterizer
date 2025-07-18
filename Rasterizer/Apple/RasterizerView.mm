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
    self.useCG = false;
   return self;
}

- (void)initLayer {
    [self setWantsLayer:YES];
    CGFloat scale = self.layer.contentsScale ?: [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    if (self.useCG) {
        [self setLayer:[CALayer layer]];
        self.layer.contentsFormat = kCAContentsFormatRGBA8Uint;
        self.layer.delegate = self;
        self.layer.magnificationFilter = kCAFilterNearest;
    } else {
        [self setLayer:[RasterizerLayer layer]];
        ((RasterizerLayer *)self.layer).layerDelegate = self;
    }
    _renderer.reset();
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
    if ([self.listDelegate respondsToSelector:@selector(getList:height:)]) {
        Ra::DrawList list = [self.listDelegate getList: self.bounds.size.width
                                                 height: self.bounds.size.height];
        buffer->useCurves = list.useCurves;
        
        float scale = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
        Ra::Bounds device(0.f, 0.f, ceilf(scale * w), ceilf(scale * h));
        Ra::Transform view = Ra::Transform(scale, 0.f, 0.f, scale, 0.f, 0.f).concat(list.ctm);
        
        _renderer.renderList(list.list, device, view, buffer);
    }
    return self.window.colorSpace.CGColorSpace;
}

#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    Ra::Colorant color = Ra::Colorant(0xFF, 0xFF, 0xFF, 0xFF);
    memset_pattern4(CGBitmapContextGetData(ctx), & color.b, CGBitmapContextGetBytesPerRow(ctx) * CGBitmapContextGetHeight(ctx));
    if ([self.listDelegate respondsToSelector:@selector(getList:height:)]) {
        Ra::Bounds bounds = RaCG::BoundsFromCGRect(self.bounds);
        Ra::DrawList list = [self.listDelegate getList: bounds.width()
                                                 height: bounds.height()];
        if (!list.useCurves)
            CGContextSetFlatness(ctx, 20 * self.layer.contentsScale);
        CGContextConcatCTM(ctx, RaCG::CGFromTransform(list.ctm));
        RaCG::drawList(list.list, list.ctm, bounds, 0.f, ctx);
    }
}


#pragma mark - Properies

- (void)setUseCG:(bool)useCG {
    _useCG = useCG;
    [self initLayer];
}


@end
