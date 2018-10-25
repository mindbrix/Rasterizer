//
//  RasterizerView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "RasterizerView.h"
#import "CocoaRasterizer.hpp"
#import "AGGRasterizer.hpp"


@interface RasterizerView () <CALayerDelegate>

@property(nonatomic) BOOL useRasterizer;
@property(nonatomic) std::vector<CGPathRef> glyphCGPaths;
@property(nonatomic) std::vector<Rasterizer::Path> glyphPaths;
@property(nonatomic) std::vector<Rasterizer::Bounds> glyphBounds;
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
    
    CGRect rect = { 0, 0, _dimension * _phi, _dimension * _phi };
    CGPathRef rectPath = CGPathCreateWithRect(rect, NULL);
    CGPathRef ellipsePath = CGPathCreateWithEllipseInRect(rect, NULL);
    
    [self writeGlyphGrid:@"AppleSymbols"];
    
    Rasterizer::Path path0, path1;
    CocoaRasterizer::writeCGPathToPath(ellipsePath, path0);
    CGMutablePathRef mutablePath = CGPathCreateMutable();
    CocoaRasterizer::writePathToCGPath(path0, mutablePath);
    CocoaRasterizer::writeCGPathToPath(mutablePath, path1);
    assert(path0.atoms.size() == path1.atoms.size());
    CGPathRelease(mutablePath);
    CGPathRelease(ellipsePath);
    CGPathRelease(rectPath);
    return self;
}

- (void)dealloc {
    for (CGPathRef path : _glyphCGPaths)
        CFRelease(path);
}

- (void)drawRect:(NSRect)dirtyRect {}


#pragma mark - NSFontManager

- (void)changeFont:(id)sender {
    NSFont *oldFont = [NSFont fontWithName:@"Times" size:14];
    NSFont *newFont = [[NSFontManager sharedFontManager] convertFont:oldFont];
    [self writeGlyphGrid:newFont.fontName];
    [self redraw];
}

#pragma mark - Drawing

- (void)redraw {
    [self.layer setNeedsDisplay];
}

- (void)updateRasterizerLabel {
    self.rasterizerLabel.stringValue = self.useRasterizer ? @"Rasterizer" : @"Core Graphics";
}

- (void)writeGlyphGrid:(NSString *)fontName {
    for (CGPathRef path : _glyphCGPaths)
        CFRelease(path);
    _glyphCGPaths.resize(0), _glyphPaths.resize(0), _glyphBounds.resize(0);
    CGFontRef cgFont = CGFontCreateWithFontName((__bridge CFStringRef)fontName);
    CTFontRef ctFont = CTFontCreateWithGraphicsFont(cgFont, _dimension * _phi, NULL, NULL);
    CFIndex glyphCount = CTFontGetGlyphCount(ctFont);
    for (CFIndex glyph = 1; glyph < glyphCount; glyph++) {
        CGPathRef path = CTFontCreatePathForGlyph(ctFont, glyph, NULL);
        if (path) {
            _glyphCGPaths.emplace_back(path);
            _glyphPaths.emplace_back();
            CocoaRasterizer::writeCGPathToPath(path, _glyphPaths.back());
            _glyphBounds.emplace_back(CocoaRasterizer::boundsFromCGRect(CGPathGetBoundingBox(path)));
        }
    }
    CFRelease(cgFont);
    CFRelease(ctFont);
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

- (IBAction)toggleRasterizer:(id)sender {
    self.useRasterizer = !self.useRasterizer;
    [self updateRasterizerLabel];
    [self redraw];
}


#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    CGContextConcatCTM(ctx, self.CTM);
    CGContextConcatCTM(ctx, CGAffineTransformMake(1, 0, 0, 1, 0, 0));
    CGAffineTransform CTM = CGContextGetCTM(ctx);
    size_t square = ceilf(sqrtf(float(_glyphPaths.size())));
    float tx, ty;
    
    if (self.useRasterizer) {
        uint8_t black[4] = { 0, 0, 0, 255 };
        uint32_t bgra = *((uint32_t *)black);
        
        Rasterizer::AffineTransform ctm(CTM.a, CTM.b, CTM.c, CTM.d, CTM.tx, CTM.ty);
        Rasterizer::Bitmap bitmap(CGBitmapContextGetData(ctx), CGBitmapContextGetWidth(ctx), CGBitmapContextGetHeight(ctx), CGBitmapContextGetBytesPerRow(ctx), CGBitmapContextGetBitsPerPixel(ctx));
        Rasterizer::Context context(bitmap);
        
        for (size_t i = 0; i < _glyphPaths.size(); i++) {
            Rasterizer::Bounds glyphBounds = _glyphBounds[i];
            tx = (i % square) * _dimension - glyphBounds.lx, ty = (i / square) * _dimension - glyphBounds.ly;
            Rasterizer::writePathToBitmap(_glyphPaths[i], glyphBounds, ctm.concat(Rasterizer::AffineTransform(1, 0, 0, 1, tx, ty)), bgra, context);
        }
    } else {
        for (size_t i = 0; i < _glyphPaths.size(); i++) {
            Rasterizer::Bounds glyphBounds = _glyphBounds[i];
            tx = (i % square) * _dimension - glyphBounds.lx, ty = (i / square) * _dimension - glyphBounds.ly;
            CGContextSaveGState(ctx);
            CGContextTranslateCTM(ctx, tx, ty);
            CGContextAddPath(ctx, _glyphCGPaths[i]);
            CGContextFillPath(ctx);
            CGContextRestoreGState(ctx);
        }
    }
}

@end
