//
//  RasterizerView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "RasterizerView.h"
#import "Rasterizer.hpp"
#import "AGGRasterizer.hpp"


@interface RasterizerView () <CALayerDelegate>

@property(nonatomic) NSData *gridBoundingBoxesBacking;
@property(nonatomic) BOOL useRasterizer;
@property(nonatomic) Rasterizer::Path path;
@property(nonatomic) std::vector<CGPathRef> glyphCGPaths;
@property(nonatomic) std::vector<Rasterizer::Path> glyphPaths;
@property(nonatomic) std::vector<Rasterizer::Bounds> glyphBounds;
@property(nonatomic) CGMutablePathRef ellipse;

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

+ (void)writeCGPath:(CGPathRef)path toPath:(Rasterizer::Path &)p {
    CGPathApplyWithBlock(path, ^(const CGPathElement *element) {
        switch (element->type) {
            case kCGPathElementMoveToPoint:
                p.moveTo(float(element->points[0].x), float(element->points[0].y));
                break;
            case kCGPathElementAddLineToPoint:
                p.lineTo(float(element->points[0].x), float(element->points[0].y));
                break;
            case kCGPathElementAddQuadCurveToPoint:
                p.quadTo(float(element->points[0].x), float(element->points[0].y), float(element->points[1].x), float(element->points[1].y));
                break;
            case kCGPathElementAddCurveToPoint:
                p.cubicTo(float(element->points[0].x), float(element->points[0].y), float(element->points[1].x), float(element->points[1].y), float(element->points[2].x), float(element->points[2].y));
                break;
            case kCGPathElementCloseSubpath:
                p.close();
                break;
        }
    });
}

+ (void)writePath:(Rasterizer::Path &)p toCGPath:(CGMutablePathRef)path {
    float *points;
    for (Rasterizer::Path::Atom& atom : p.atoms) {
        size_t index = 0;
        auto type = 0xF & atom.types[0];
        while (type) {
            points = atom.points + index * 2;
            switch (type) {
                case Rasterizer::Path::Atom::kMove:
                    CGPathMoveToPoint(path, NULL, points[0], points[1]);
                    index++;
                    break;
                case Rasterizer::Path::Atom::kLine:
                    CGPathAddLineToPoint(path, NULL, points[0], points[1]);
                    index++;
                    break;
                case Rasterizer::Path::Atom::kQuadratic:
                    CGPathAddQuadCurveToPoint(path, NULL, points[0], points[1], points[2], points[3]);
                    index += 2;
                    break;
                case Rasterizer::Path::Atom::kCubic:
                    CGPathAddCurveToPoint(path, NULL, points[0], points[1], points[2], points[3], points[4], points[5]);
                    index += 3;
                    break;
                case Rasterizer::Path::Atom::kClose:
                    CGPathCloseSubpath(path);
                    index++;
                    break;
            }
            type = 0xF & (atom.types[index / 2] >> (index & 1 ? 4 : 0));
        }
    }
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
    
    CGFloat dimension = 24;
    CGFloat phi = (sqrt(5) - 1) / 2;
    CGRect rect = { 0, 0, dimension * phi, dimension * phi };
    CGPathRef rectPath = CGPathCreateWithRect(rect, NULL);
    CGPathRef ellipsePath = CGPathCreateWithEllipseInRect(rect, NULL);
    CGFontRef cgFont = CGFontCreateWithFontName(CFSTR("Menlo-Regular"));
    CTFontRef ctFont = CTFontCreateWithGraphicsFont(cgFont, dimension * phi, NULL, NULL);
    CFIndex glyphCount = CTFontGetGlyphCount(ctFont);
    for (CFIndex glyph = 1; glyph < glyphCount; glyph++) {
        CGPathRef path = CTFontCreatePathForGlyph(ctFont, glyph, NULL);
        if (path) {
            _glyphCGPaths.emplace_back(path);
            _glyphPaths.emplace_back();
            [self.class writeCGPath:path toPath:_glyphPaths.back()];
            
            CGRect glyphBounds = CGPathGetBoundingBox(path);
            Rasterizer::Bounds bounds(float(glyphBounds.origin.x), float(glyphBounds.origin.y), float(glyphBounds.origin.x + glyphBounds.size.width), float(glyphBounds.origin.y + glyphBounds.size.height));
            _glyphBounds.emplace_back(bounds);
        }
    }
    CGPathRef glyphPath = CTFontCreatePathForGlyph(ctFont, 27, NULL);
    
    [self.class writeCGPath:ellipsePath toPath:_path];
    _ellipse = CGPathCreateMutable();
    [self.class writePath:_path toCGPath:_ellipse];
    Rasterizer::Path testPath;
    [self.class writeCGPath:_ellipse toPath:testPath];
    assert(_path.atoms.size() == testPath.atoms.size());
    
    CGPathRelease(ellipsePath);
    CGPathRelease(rectPath);
    CFRelease(cgFont);
    CFRelease(ctFont);
    CFRelease(glyphPath);
    return self;
}

- (void)dealloc {
    for (CGPathRef path : _glyphCGPaths)
        CFRelease(path);
    CGPathRelease(_ellipse);
}

- (void)drawRect:(NSRect)dirtyRect {}


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
    CGRect pathBounds = CGPathGetBoundingBox(self.ellipse);
    Rasterizer::Bounds polygonBounds(float(pathBounds.origin.x), float(pathBounds.origin.y), float(pathBounds.origin.x + pathBounds.size.width), float(pathBounds.origin.y + pathBounds.size.height));
    
    if (self.useRasterizer) {
        uint8_t black[4] = { 0, 0, 0, 255 };
        uint32_t bgra = *((uint32_t *)black);
        
        Rasterizer::AffineTransform ctm(CTM.a, CTM.b, CTM.c, CTM.d, CTM.tx, CTM.ty);
        Rasterizer::Bitmap bitmap(CGBitmapContextGetData(ctx), CGBitmapContextGetWidth(ctx), CGBitmapContextGetHeight(ctx), CGBitmapContextGetBytesPerRow(ctx), CGBitmapContextGetBitsPerPixel(ctx));
        Rasterizer::Context context;
        for (size_t i = 0; i < _glyphPaths.size(); i++) {
            Rasterizer::Bounds glyphBounds = _glyphBounds[i];
            Rasterizer::Bounds bounds = boundingBoxes[i];
            Rasterizer::renderPath(context, glyphBounds, _glyphPaths[i], bgra, ctm.concat(Rasterizer::AffineTransform(1, 0, 0, 1, bounds.lx - glyphBounds.lx, bounds.ly - glyphBounds.ly)), bitmap);
        }
        
//        for (size_t i = 0; i < count; i++) {
//            Rasterizer::Bounds bounds = boundingBoxes[i];
//            Rasterizer::renderPath(context, polygonBounds, _path, bgra, ctm.concat(Rasterizer::AffineTransform(1, 0, 0, 1, bounds.lx - polygonBounds.lx, bounds.ly - polygonBounds.ly)), bitmap);
//        }
    } else {
        for (size_t i = 0; i < _glyphPaths.size(); i++) {
            Rasterizer::Bounds glyphBounds = _glyphBounds[i];
            Rasterizer::Bounds bounds = boundingBoxes[i];
            CGContextSaveGState(ctx);
            CGContextTranslateCTM(ctx, bounds.lx - glyphBounds.lx, bounds.ly - glyphBounds.ly);
            CGContextAddPath(ctx, _glyphCGPaths[i]);
            CGContextFillPath(ctx);
            CGContextRestoreGState(ctx);
        }
        
//        for (size_t i = 0; i < count; i++) {
//            Rasterizer::Bounds bounds = boundingBoxes[i];
//            CGContextSaveGState(ctx);
//            CGContextTranslateCTM(ctx, bounds.lx - polygonBounds.lx, bounds.ly - polygonBounds.ly);
//            CGContextAddPath(ctx, _ellipse);
//            CGContextFillPath(ctx);
//            CGContextRestoreGState(ctx);
//        }
    }
}


#pragma mark - Properties


@end
