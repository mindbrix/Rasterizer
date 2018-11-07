//
//  RasterizerView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "RasterizerView.h"
#import "RasterizerCoreGraphics.hpp"
#import "AGGRasterizer.hpp"
#import "RasterizerSVG.hpp"

@interface RasterizerView () <CALayerDelegate>

@property(nonatomic) Rasterizer::Context context;
@property(nonatomic) BOOL useCoreGraphics;
@property(nonatomic) std::vector<CGPathRef> glyphCGPaths;
@property(nonatomic) std::vector<Rasterizer::Path> glyphPaths;
@property(nonatomic) std::vector<Rasterizer::Bounds> glyphBounds;
@property(nonatomic) Rasterizer::Scene svg;
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
    self.dimension = 24;
    self.phi = (sqrt(5) - 1) / 2;
    [self writeGlyphGrid:@"AppleSymbols"];
    return self;
}

- (void)dealloc {
    for (CGPathRef path : _glyphCGPaths)
        CFRelease(path);
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
    self.rasterizerLabel.stringValue = self.useCoreGraphics ? @"Core Graphics" :  @"Rasterizer";
}

- (void)writeGlyphGrid:(NSString *)fontName {
    RasterizerCoreGraphics::writeGlyphGrid(fontName, _glyphCGPaths, _dimension * _phi, _glyphPaths, _glyphBounds);
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
    self.useCoreGraphics = !self.useCoreGraphics;
    [self updateRasterizerLabel];
    [self redraw];
}


#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    size_t square = ceilf(sqrtf(float(_glyphPaths.size())));
    CGContextConcatCTM(ctx, self.CTM);
    CGFloat w = self.bounds.size.width, h = self.bounds.size.height, scale = (w < h ? w : h) / CGFloat(square * _dimension * _phi);
    CGContextConcatCTM(ctx, CGAffineTransformMake(scale, 0, 0, scale, 0, 0));
    CGAffineTransform CTM = CGContextGetCTM(ctx);
    Rasterizer::AffineTransform ctm(CTM.a, CTM.b, CTM.c, CTM.d, CTM.tx, CTM.ty);
    Rasterizer::Bitmap bitmap(CGBitmapContextGetData(ctx), CGBitmapContextGetWidth(ctx), CGBitmapContextGetHeight(ctx), CGBitmapContextGetBytesPerRow(ctx), CGBitmapContextGetBitsPerPixel(ctx));
    _context.setBitmap(bitmap);
    float tx, ty;
    
    if (self.useCoreGraphics) {
        for (size_t i = 0; i < _glyphPaths.size(); i++) {
            Rasterizer::Bounds glyphBounds = _glyphBounds[i];
            tx = (i % square) * _dimension * _phi - glyphBounds.lx, ty = (i / square) * _dimension * _phi - glyphBounds.ly;
            Rasterizer::Bounds device = glyphBounds.transform(ctm.concat(Rasterizer::AffineTransform(1, 0, 0, 1, tx, ty))).integral();
            Rasterizer::Bounds clipped = device.intersected(_context.clipBounds);
            if (clipped.lx != clipped.ux && clipped.ly != clipped.uy) {
                CGContextSaveGState(ctx);
                CGContextTranslateCTM(ctx, tx, ty);
                CGContextAddPath(ctx, _glyphCGPaths[i]);
                CGContextFillPath(ctx);
                CGContextRestoreGState(ctx);
            }
        }
    } else {
        if (_svg.paths.size()) {
            for (size_t i = 0; i < _svg.paths.size(); i++)
                Rasterizer::writePathToBitmap(_svg.paths[i], _svg.bounds[i], ctm, _svg.bgras[i], _context);
        } else {
            uint8_t black[4] = { 0, 0, 0, 255 };
            uint32_t bgra = *((uint32_t *)black);
            for (size_t i = 0; i < _glyphPaths.size(); i++) {
                Rasterizer::Bounds glyphBounds = _glyphBounds[i];
                tx = (i % square) * _dimension * _phi - glyphBounds.lx, ty = (i / square) * _dimension * _phi - glyphBounds.ly;
                Rasterizer::writePathToBitmap(_glyphPaths[i], glyphBounds, ctm.concat(Rasterizer::AffineTransform(1, 0, 0, 1, tx, ty)), bgra, _context);
            }
        }
    }
}

- (void)setSvgData:(NSData *)svgData {
    _svgData = svgData;
    
    if (_svgData) {
        _svg.empty();
        RasterizerSVG::writeToScene(_svgData.bytes, _svgData.length, _svg);
    }
    [self redraw];
}
@end
