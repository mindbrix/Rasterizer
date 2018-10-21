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
@property(nonatomic) std::vector<float> path;

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

+ (void)writePath:(CGPathRef)path toFloats:(std::vector<float>&)floats {
    CGPathApplyWithBlock(path, ^(const CGPathElement *element) {
        switch (element->type) {
            case kCGPathElementMoveToPoint:
                floats.emplace_back(float(element->points[0].x));
                floats.emplace_back(float(element->points[0].y));
                break;
            case kCGPathElementAddLineToPoint:
                floats.emplace_back(float(element->points[0].x));
                floats.emplace_back(float(element->points[0].y));
                break;
            case kCGPathElementAddQuadCurveToPoint:
                floats.emplace_back(float(element->points[0].x));
                floats.emplace_back(float(element->points[0].y));
                floats.emplace_back(float(element->points[1].x));
                floats.emplace_back(float(element->points[1].y));
                break;
            case kCGPathElementAddCurveToPoint:
                floats.emplace_back(float(element->points[0].x));
                floats.emplace_back(float(element->points[0].y));
                floats.emplace_back(float(element->points[1].x));
                floats.emplace_back(float(element->points[1].y));
                floats.emplace_back(float(element->points[2].x));
                floats.emplace_back(float(element->points[2].y));
                break;
            case kCGPathElementCloseSubpath:
                break;
        }
    });
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
    CGFloat phi = (sqrt(5) - 1) / 2;
    CGRect rect = { 0, 0, 24 * phi, 24 * phi };
    CGPathRef ellipse = CGPathCreateWithEllipseInRect(rect, NULL);
    [self.class writePath:ellipse toFloats:_path];
    return self;
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
    CGAffineTransform CTM = CGContextGetCTM(ctx);
    
    if (self.useRasterizer) {
        Rasterizer::AffineTransform ctm(CTM.a, CTM.b, CTM.c, CTM.d, CTM.tx, CTM.ty);
        Rasterizer::Bitmap bitmap(CGBitmapContextGetData(ctx), CGBitmapContextGetWidth(ctx), CGBitmapContextGetHeight(ctx), CGBitmapContextGetBytesPerRow(ctx), CGBitmapContextGetBitsPerPixel(ctx));
        Rasterizer::Context context;
        Rasterizer::renderBoundingBoxes(context, boundingBoxes, count, ctm, bitmap);
    } else
        for (size_t i = 0; i < count; i++)
            CGContextFillRect(ctx, CGRectMake(boundingBoxes[i].lx, boundingBoxes[i].ly, boundingBoxes[i].ux - boundingBoxes[i].lx, boundingBoxes[i].uy - boundingBoxes[i].ly));
}


#pragma mark - Properties


@end
