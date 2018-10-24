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

@property(nonatomic) NSData *gridBoundingBoxesBacking;
@property(nonatomic) BOOL useRasterizer;
@property(nonatomic) std::vector<CGPathRef> glyphCGPaths;
@property(nonatomic) std::vector<Rasterizer::Path> glyphPaths;
@property(nonatomic) std::vector<Rasterizer::Bounds> glyphBounds;
@property(nonatomic) CGFloat dimension;
@property(nonatomic) CGFloat phi;

@end


@implementation RasterizerView

+ (NSData *)createGridBoundingBoxes:(size_t)count cellSize:(size_t)cellSize {
    NSMutableData *backing = [NSMutableData dataWithLength:count * sizeof(Rasterizer::Bounds)];
    Rasterizer::Bounds *bounds = (Rasterizer::Bounds *)backing.bytes;
    CGFloat phi = (sqrt(5) - 1) / 2;
    CGFloat wh = CGFloat(cellSize) * phi;
    size_t dimension = ceil(sqrt(CGFloat(count)));
    float lx, ly;
    for (size_t i = 0; i < count; i++) {
        lx = (i % dimension) * cellSize, ly = (i / dimension) * cellSize;
        bounds[i] = Rasterizer::Bounds(lx, ly, lx + wh, ly + wh);
    }
    return [NSData dataWithData:backing];
}


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
    self.gridBoundingBoxesBacking = [self.class createGridBoundingBoxes:10000 cellSize:24];
    
    self.dimension = 24;
    self.phi = (sqrt(5) - 1) / 2;
    CGRect rect = { 0, 0, _dimension * _phi, _dimension * _phi };
    CGPathRef rectPath = CGPathCreateWithRect(rect, NULL);
    CGPathRef ellipsePath = CGPathCreateWithEllipseInRect(rect, NULL);
    
    CGFontRef cgFont = CGFontCreateWithFontName(CFSTR("AppleSymbols"));
    CTFontRef ctFont = CTFontCreateWithGraphicsFont(cgFont, _dimension * _phi, NULL, NULL);
    CFIndex glyphCount = CTFontGetGlyphCount(ctFont);
    for (CFIndex glyph = 1; glyph < glyphCount; glyph++) {
//        CGPathRef path = CGPathRetain(ellipsePath);
        CGPathRef path = CTFontCreatePathForGlyph(ctFont, glyph, NULL);
        if (path) {
            _glyphCGPaths.emplace_back(path);
            _glyphPaths.emplace_back();
            CocoaRasterizer::writeCGPathToPath(path, _glyphPaths.back());
            _glyphBounds.emplace_back(CocoaRasterizer::boundsFromCGRect(CGPathGetBoundingBox(path)));
        }
    }
    Rasterizer::Path path0, path1;
    CocoaRasterizer::writeCGPathToPath(ellipsePath, path0);
    CGMutablePathRef mutablePath = CGPathCreateMutable();
    CocoaRasterizer::writePathToCGPath(path0, mutablePath);
    CocoaRasterizer::writeCGPathToPath(mutablePath, path1);
    assert(path0.atoms.size() == path1.atoms.size());
    CGPathRelease(mutablePath);
    CGPathRelease(ellipsePath);
    CGPathRelease(rectPath);
    CFRelease(cgFont);
    CFRelease(ctFont);
    return self;
}

- (void)dealloc {
    for (CGPathRef path : _glyphCGPaths)
        CFRelease(path);
}

- (void)drawRect:(NSRect)dirtyRect {}


#pragma mark - NSFontManager

- (void)changeFont:(id)sender {
    for (CGPathRef path : _glyphCGPaths)
        CFRelease(path);
    _glyphCGPaths.resize(0), _glyphPaths.resize(0), _glyphBounds.resize(0);
    NSFont *oldFont = [NSFont fontWithName:@"Times" size:14];
    NSFont *newFont = [[NSFontManager sharedFontManager] convertFont:oldFont];
    CGFontRef cgFont = CGFontCreateWithFontName((__bridge CFStringRef)newFont.fontName);
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
    [self redraw];
}

#pragma mark - Drawing

- (void)redraw {
    [self.layer setNeedsDisplay];
}

- (void)updateRasterizerLabel {
    self.rasterizerLabel.stringValue = self.useRasterizer ? @"Rasterizer" : @"Core Graphics";
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
    size_t count = self.gridBoundingBoxesBacking.length / sizeof(Rasterizer::Bounds);
    Rasterizer::Bounds *boundingBoxes = (Rasterizer::Bounds *)self.gridBoundingBoxesBacking.bytes;
    CGContextConcatCTM(ctx, self.CTM);
    CGContextConcatCTM(ctx, CGAffineTransformMake(1, 0, 0, 1, 0, 0));
    CGAffineTransform CTM = CGContextGetCTM(ctx);
    
    if (self.useRasterizer) {
        uint8_t black[4] = { 0, 0, 0, 255 };
        uint32_t bgra = *((uint32_t *)black);
        
        Rasterizer::AffineTransform ctm(CTM.a, CTM.b, CTM.c, CTM.d, CTM.tx, CTM.ty);
        Rasterizer::Bitmap bitmap(CGBitmapContextGetData(ctx), CGBitmapContextGetWidth(ctx), CGBitmapContextGetHeight(ctx), CGBitmapContextGetBytesPerRow(ctx), CGBitmapContextGetBitsPerPixel(ctx));
        Rasterizer::Context context(bitmap);
        for (size_t i = 0; i < _glyphPaths.size() && i < count; i++) {
            Rasterizer::Bounds glyphBounds = _glyphBounds[i];
            Rasterizer::Bounds bounds = boundingBoxes[i];
            Rasterizer::writePathToBitmap(_glyphPaths[i], glyphBounds, ctm.concat(Rasterizer::AffineTransform(1, 0, 0, 1, bounds.lx - glyphBounds.lx, bounds.ly - glyphBounds.ly)), bgra, context);
        }
    } else {
        for (size_t i = 0; i < _glyphPaths.size() && i < count; i++) {
            Rasterizer::Bounds glyphBounds = _glyphBounds[i];
            Rasterizer::Bounds bounds = boundingBoxes[i];
            CGContextSaveGState(ctx);
            CGContextTranslateCTM(ctx, bounds.lx - glyphBounds.lx, bounds.ly - glyphBounds.ly);
            CGContextAddPath(ctx, _glyphCGPaths[i]);
            CGContextFillPath(ctx);
            CGContextRestoreGState(ctx);
        }
    }
}

@end
