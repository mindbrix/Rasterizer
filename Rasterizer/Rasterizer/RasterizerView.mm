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

enum RasterizerType : int { kRasterizerMT = 0, kRasterizer, kCoreGraphics, kRasterizerCount };

@interface RasterizerView () <CALayerDelegate>

@property(nonatomic) RasterizerCoreGraphics::CGTestScene testScene;
@property(nonatomic) CGFloat dimension;
@property(nonatomic) CGFloat phi;

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
    self.layer.opaque = NO;
    self.layer.needsDisplayOnBoundsChange = YES;
    self.layer.actions = @{ @"onOrderIn": [NSNull null], @"onOrderOut": [NSNull null], @"sublayers": [NSNull null], @"contents": [NSNull null], @"backgroundColor": [NSNull null], @"bounds": [NSNull null] };
    _testScene.contexts.resize(4);
    self.dimension = 24;
    self.phi = (sqrt(5) - 1) / 2;
    [self writeGlyphGrid:@"AppleSymbols"];
    return self;
}

- (void)drawRect:(NSRect)dirtyRect {}


#pragma mark - NSFontManager

- (void)changeFont:(id)sender {
    NSFont *newFont = [[NSFontManager sharedFontManager] convertFont:[NSFont fontWithName:@"Times" size:14]];
    [self writeGlyphGrid:newFont.fontName];
    [self resetCTM:nil];
    [self redraw];
}

#pragma mark - Drawing

- (void)redraw {
    [self.layer setNeedsDisplay];
}

- (void)updateRasterizerLabel {
    self.rasterizerLabel.stringValue = _testScene.rasterizerType == kRasterizerMT ? @"Rasterizer (mt)" : _testScene.rasterizerType == kRasterizer ?  @"Rasterizer" : @"Core Graphics";
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


#pragma mark - IBOutlet

- (IBAction)resetCTM:(id)sender {
    self.CTM = CGAffineTransformIdentity;
}

- (IBAction)toggleRasterizer:(id)sender {
    _testScene.rasterizerType = (++_testScene.rasterizerType) % kRasterizerCount;
    [self updateRasterizerLabel];
    [self redraw];
}


#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    size_t square = ceilf(sqrtf(float(_testScene.scene.paths.size())));
    CGContextConcatCTM(ctx, self.CTM);
    CGFloat w = self.bounds.size.width, h = self.bounds.size.height, scale = (w < h ? w : h) / CGFloat(square * _dimension * _phi);
    CGContextConcatCTM(ctx, CGAffineTransformMake(scale, 0, 0, scale, 0, 0));
    CGAffineTransform CTM = CGContextGetCTM(ctx);
    Rasterizer::AffineTransform ctm(CTM.a, CTM.b, CTM.c, CTM.d, CTM.tx, CTM.ty);
    Rasterizer::Bitmap bitmap(CGBitmapContextGetData(ctx), CGBitmapContextGetWidth(ctx), CGBitmapContextGetHeight(ctx), CGBitmapContextGetBytesPerRow(ctx), CGBitmapContextGetBitsPerPixel(ctx));
    RasterizerCoreGraphics::writeTestSceneToContextOrBitmap(_testScene, ctm, ctx, bitmap);
}

- (void)setSvgData:(NSData *)svgData {
    _svgData = svgData;
    
    if (_svgData)
        RasterizerSVG::writeTestScene(_svgData.bytes, _svgData.length, _testScene);
    [self redraw];
}
@end
