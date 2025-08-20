//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//
//  This software is provided 'as-is', without any express or implied
//  warranty. In no event will the authors be held liable for any damages
//  arising from the use of this software.
//
//  Permission is granted to anyone to use this software for any purpose,
//  including commercial applications, and to alter it and redistribute it
//  freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
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


#pragma mark - LayerDelegate

- (CGColorSpaceRef)writeBuffer:(Ra::Buffer *)buffer forLayer:(CALayer *)layer {
    buffer->clearColor = Ra::Colorant(0xFF, 0xFF, 0xFF, 0xFF);
    if ([self.listDelegate respondsToSelector:@selector(getList:height:)]) {
        float scale = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
        Ra::SceneList list = [self.listDelegate getList: w
                                                height: h];
        _renderer.renderList(list, scale, w, h, buffer);
    }
    return self.window.colorSpace.CGColorSpace;
}

#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    Ra::Colorant color = Ra::Colorant(0xFF, 0xFF, 0xFF, 0xFF);
    memset_pattern4(CGBitmapContextGetData(ctx), & color.b, CGBitmapContextGetBytesPerRow(ctx) * CGBitmapContextGetHeight(ctx));
    if ([self.listDelegate respondsToSelector:@selector(getList:height:)]) {
        float scale = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
        Ra::SceneList list = [self.listDelegate getList: w
                                                height: h];
        RaCG::renderList(list, scale, w, h, ctx);
    }
}


#pragma mark - Properies

- (void)setUseCG:(bool)useCG {
    _useCG = useCG;
    
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


@end
