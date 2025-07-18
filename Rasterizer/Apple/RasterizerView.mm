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
@property(nonatomic) Ra::SceneList sceneList;
@property(nonatomic) Ra::Transform ctm;
@property(nonatomic, readonly) Ra::Bounds device;
@property(nonatomic, readonly) Ra::Transform view;
@property(nonatomic) float clipInset;

@end


@implementation RasterizerView

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
   self = [super initWithCoder:decoder];
   if (! self)
       return nil;
    self.useCG = false;
    self.ctm = Ra::Transform();
    self.clipInset = 0.f;
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
    Ra::Bounds deviceClip = self.device.inset(self.clipInset, self.clipInset);
    
    if ([self.listDelegate respondsToSelector:@selector(getList:height:)]) {
        Ra::DrawList list = [self.listDelegate getList: self.bounds.size.width
                                                 height: self.bounds.size.height];
        buffer->useCurves = list.useCurves;
        self.ctm = list.ctm;
        _renderer.renderList(list.list, self.device, deviceClip, self.view, buffer);
    }
    return self.window.colorSpace.CGColorSpace;
}

#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    Ra::Colorant color = Ra::Colorant(0xFF, 0xFF, 0xFF, 0xFF);
    memset_pattern4(CGBitmapContextGetData(ctx), & color.b, CGBitmapContextGetBytesPerRow(ctx) * CGBitmapContextGetHeight(ctx));
    Ra::Bounds deviceClip = self.device.inset(self.clipInset, self.clipInset);
    CGFloat scale = 1.0 / self.layer.contentsScale;
    
    CGContextClipToRect(ctx, CGRectApplyAffineTransform(RaCG::CGRectFromBounds(deviceClip), CGAffineTransformMakeScale(scale, scale)) );
    
    if ([self.listDelegate respondsToSelector:@selector(getList:height:)]) {
        Ra::DrawList list = [self.listDelegate getList: self.bounds.size.width
                                                 height: self.bounds.size.height];
        if (!list.useCurves)
            CGContextSetFlatness(ctx, 20 * self.layer.contentsScale);
        self.ctm = list.ctm;
        CGContextConcatCTM(ctx, RaCG::CGFromTransform(list.ctm));
        RaCG::drawList(list.list, self.view, self.device, deviceClip, 0.f, ctx);
    }
}


#pragma mark - Properies

- (Ra::Bounds) device {
    float scale = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
    return Ra::Bounds(0.f, 0.f, ceilf(scale * w), ceilf(scale * h));
}

- (Ra::Transform) view {
    float scale = self.layer.contentsScale;
    return Ra::Transform(scale, 0.f, 0.f, scale, 0.f, 0.f).concat(_ctm);
}

- (void)setUseCG:(bool)useCG {
    _useCG = useCG;
    [self initLayer];
}


@end
