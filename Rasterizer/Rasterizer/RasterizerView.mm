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
@property(nonatomic) std::vector<std::vector<float>> polygons;
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

+ (void)writePath:(CGPathRef)path toPolygons:(std::vector<std::vector<float>>&)polygons {
    __block CGFloat x0, y0, x1, y1, x2, y2, x3, y3, w0, w1, w2, w3;
    w0 = 8.0 / 27.0, w1 = 4.0 / 9.0, w2 = 2.0 / 9.0, w3 = 1.0 / 27.0;
    CGPathApplyWithBlock(path, ^(const CGPathElement *element) {
        switch (element->type) {
            case kCGPathElementMoveToPoint:
                polygons.emplace_back();
                polygons.back().emplace_back(float(element->points[0].x));
                polygons.back().emplace_back(float(element->points[0].y));
                break;
            case kCGPathElementAddLineToPoint:
                polygons.back().emplace_back(float(element->points[0].x));
                polygons.back().emplace_back(float(element->points[0].y));
                break;
            case kCGPathElementAddQuadCurveToPoint:
                x0 = *((& polygons.back().back()) - 1), y0 = *((& polygons.back().back()) - 0);
                x1 = element->points[0].x, y1 = element->points[0].y;
                x2 = element->points[1].x, y2 = element->points[1].y;
                polygons.back().emplace_back(float(x0 * 0.25 + x1 * 0.5 + x2 * 0.25));
                polygons.back().emplace_back(float(y0 * 0.25 + y1 * 0.5 + y2 * 0.25));
                polygons.back().emplace_back(float(x2));
                polygons.back().emplace_back(float(y2));
                break;
            case kCGPathElementAddCurveToPoint:
                x0 = *((& polygons.back().back()) - 1), y0 = *((& polygons.back().back()) - 0);
                x1 = element->points[0].x, y1 = element->points[0].y;
                x2 = element->points[1].x, y2 = element->points[1].y;
                x3 = element->points[2].x, y3 = element->points[2].y;
                polygons.back().emplace_back(float(x0 * w0 + x1 * w1 + x2 * w2 + x3 * w3));
                polygons.back().emplace_back(float(y0 * w0 + y1 * w1 + y2 * w2 + y3 * w3));
                polygons.back().emplace_back(float(x0 * w3 + x1 * w2 + x2 * w1 + x3 * w0));
                polygons.back().emplace_back(float(y0 * w3 + y1 * w2 + y2 * w1 + y3 * w0));
                polygons.back().emplace_back(float(x3));
                polygons.back().emplace_back(float(y3));
                break;
            case kCGPathElementCloseSubpath:
                break;
        }
    });
}

+ (void)writePolygons:(std::vector<std::vector<float>>&)polygons toPath:(CGMutablePathRef)path {
    for (std::vector<float>& polygon : polygons) {
        CGPathMoveToPoint(path, NULL, polygon[0], polygon[1]);
        for (size_t i = 2; i < polygon.size(); i += 2)
            CGPathAddLineToPoint(path, NULL, polygon[i], polygon[i + 1]);
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
//    CGFontRef cgFont = CGFontCreateWithFontName(CFSTR("Menlo-Regular"));
//    CTFontRef ctFont = CTFontCreateWithGraphicsFont(cgFont, dimension, NULL, NULL);
//    CGPathRef glyphPath = CTFontCreatePathForGlyph(ctFont, 27, NULL);
//    [self.class writePath:glyphPath toPolygons:_polygons];
    
    CGFloat phi = (sqrt(5) - 1) / 2;
    CGRect rect = { 0, 0, dimension * phi, dimension * phi };
    CGPathRef ellipse = CGPathCreateWithEllipseInRect(rect, NULL);
    [self.class writePath:ellipse toPolygons:_polygons];
    CGPathRelease(ellipse);
    
    _ellipse = CGPathCreateMutable();
    [self.class writePolygons:_polygons toPath:_ellipse];
    std::vector<std::vector<float>> testPolygons;
    [self.class writePath:_ellipse toPolygons:testPolygons];
    assert(testPolygons == _polygons);
    
//    CFRelease(cgFont);
//    CFRelease(ctFont);
//    CFRelease(glyphPath);
    return self;
}

- (void)dealloc {
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
        for (size_t i = 0; i < count; i++) {
            Rasterizer::Bounds bounds = boundingBoxes[i];
            Rasterizer::renderPolygons(context, polygonBounds, _polygons, bgra, ctm.concat(Rasterizer::AffineTransform(1, 0, 0, 1, bounds.lx - polygonBounds.lx, bounds.ly - polygonBounds.ly)), bitmap);
        }
    } else
        for (size_t i = 0; i < count; i++) {
            Rasterizer::Bounds bounds = boundingBoxes[i];
            CGContextSaveGState(ctx);
            CGContextTranslateCTM(ctx, bounds.lx - polygonBounds.lx, bounds.ly - polygonBounds.ly);
            CGContextAddPath(ctx, _ellipse);
            CGContextFillPath(ctx);
            CGContextRestoreGState(ctx);
        }
    
}


#pragma mark - Properties


@end
