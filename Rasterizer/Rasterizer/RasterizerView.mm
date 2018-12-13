//
//  RasterizerView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#define RASTERIZER_SIMD 1
#import "RasterizerView.h"
#import "RasterizerCoreGraphics.hpp"
#import "RasterizerSVG.hpp"
#import "MetalLayer.h"

@interface RasterizerView () <CALayerDelegate, LayerDelegate>

@property(nonatomic) RasterizerCoreGraphics::CGTestScene testScene;
@property(nonatomic) BOOL useClip;

@end


@implementation RasterizerView

#pragma mark - NSView

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    
    [self setWantsLayer:YES];
    [self setLayer:[CALayer layer]];
    self.layer.contentsFormat = kCAContentsFormatRGBA8Uint;
    self.layer.contentsScale = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    self.layer.backgroundColor = CGColorGetConstantColor(kCGColorWhite);
    self.layer.delegate = self;
    self.layer.bounds = self.bounds;
    self.layer.opaque = YES;
    self.layer.needsDisplayOnBoundsChange = YES;
    self.layer.actions = @{ @"onOrderIn": [NSNull null], @"onOrderOut": [NSNull null], @"sublayers": [NSNull null], @"contents": [NSNull null], @"backgroundColor": [NSNull null], @"bounds": [NSNull null] };
    [self writeGlyphGrid:@"AppleSymbols"];
    return self;
}

- (void)drawRect:(NSRect)dirtyRect {}


#pragma mark - NSFontManager

- (void)changeFont:(id)sender {
    NSFont *newFont = [[NSFontManager sharedFontManager] convertFont:[NSFont fontWithName:@"Times" size:14]];
    [self writeGlyphGrid:newFont.fontName];
    self.CTM = CGAffineTransformIdentity;
    [self redraw];
}

#pragma mark - Drawing

- (void)redraw {
//    [self.layer setNeedsDisplayInRect:CGRectInset(self.bounds, 100, 100)];
    [self.layer setNeedsDisplay];
}

- (void)updateRasterizerLabel {
    [self.rasterizerLabel setHidden:YES];
    self.rasterizerLabel.stringValue = _testScene.rasterizerType == RasterizerCoreGraphics::CGTestScene::kRasterizerMT ? @"Rasterizer (mt)" : _testScene.rasterizerType == RasterizerCoreGraphics::CGTestScene::kRasterizer ?  @"Rasterizer" : @"Core Graphics";
}

- (void)writeGlyphGrid:(NSString *)fontName {
    RasterizerCoreGraphics::writeGlyphGrid(fontName, _testScene);
}

#pragma mark - NSResponder

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    [self updateRasterizerLabel];
    [self redraw];
    return YES;
}

- (void)keyDown:(NSEvent *)event {
    if (event.keyCode == 8) {
        _useClip = !_useClip;
        [self redraw];
    } else {
        [super keyDown:event];
    }
}

#pragma mark - IBOutlet

- (IBAction)toggleRasterizer:(id)sender {
    _testScene.rasterizerType = (++_testScene.rasterizerType) % RasterizerCoreGraphics::CGTestScene::kRasterizerCount;
    [self updateRasterizerLabel];
    [self redraw];
}

#pragma mark - LayerDelegate

- (void)writeBuffer:(Rasterizer::Buffer *)buffer forLayer:(CALayer *)layer {
    
}

#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    CGRect clip = CGRectApplyAffineTransform(CGContextGetClipBoundingBox(ctx), CGContextGetCTM(ctx));
    size_t square = ceilf(sqrtf(float(_testScene.scene.paths.size())));
    CGContextConcatCTM(ctx, self.CTM);
    CGFloat dimension = 24, phi = (sqrt(5) - 1) / 2;
    CGFloat w = self.bounds.size.width, h = self.bounds.size.height, scale = (w < h ? w : h) / CGFloat(square * dimension * phi);
    CGContextConcatCTM(ctx, CGAffineTransformMake(scale, 0, 0, scale, 0, 0));
    CGAffineTransform CTM = CGContextGetCTM(ctx);
    Rasterizer::AffineTransform ctm(CTM.a, CTM.b, CTM.c, CTM.d, CTM.tx, CTM.ty);
    Rasterizer::Bitmap bitmap(CGBitmapContextGetData(ctx), CGBitmapContextGetWidth(ctx), CGBitmapContextGetHeight(ctx), CGBitmapContextGetBytesPerRow(ctx), CGBitmapContextGetBitsPerPixel(ctx));
    bitmap.clear(_svgData ? 0xCCCCCCCC : 0xFFFFFFFF);
    Rasterizer::Path clipPath;
    clipPath.addBounds(Rasterizer::Bounds(100, 100, 200, 200));
    RasterizerCoreGraphics::writeTestScene(_testScene, ctm, RasterizerCoreGraphics::boundsFromCGRect(clip), _useClip ? &clipPath : nullptr, ctx, bitmap, nullptr);
}

- (void)setSvgData:(NSData *)svgData {
    _svgData = svgData;
    if (_svgData) {
        _testScene.scene.empty();
        _testScene.cgscene.empty();
        RasterizerSVG::writeScene(_svgData.bytes, _svgData.length, _testScene.scene);
        RasterizerCoreGraphics::writeSceneToCGScene(_testScene.scene, _testScene.cgscene);
    }
    [self redraw];
}
@end
