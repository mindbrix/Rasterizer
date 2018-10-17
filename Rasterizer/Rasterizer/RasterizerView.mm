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

#pragma clang diagnostic ignored "-Wcomma"


@interface RasterizerView () <CALayerDelegate>

@property(nonatomic) NSData *gridBoundsBacking;
@property(nonatomic) BOOL useRasterizer;

@end


@implementation RasterizerView

+ (NSData *)createGridBounds:(size_t)count cellSize:(size_t)cellSize {
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
    
    self.gridBoundsBacking = [self.class createGridBounds:10000 cellSize:24];
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
//    void *data = CGBitmapContextGetData(ctx);
//    CGColorSpaceRef colorSpace = CGBitmapContextGetColorSpace(ctx);
//    CGBitmapInfo info = CGBitmapContextGetBitmapInfo(ctx);
//    uint8_t red[4] = { 0, 0, 255, 255 };
//    size_t size = CGBitmapContextGetBytesPerRow(ctx) * CGBitmapContextGetHeight(ctx);
//    memset_pattern4(data, red, size);

    CGContextConcatCTM(ctx, self.CTM);
    size_t count = self.gridBoundsBacking.length / sizeof(Rasterizer::Bounds);
    Rasterizer::Bounds *bounds = (Rasterizer::Bounds *)self.gridBoundsBacking.bytes;
    for (size_t i = 0; i < count; i++)
        CGContextFillRect(ctx, CGRectMake(bounds[i].lx, bounds[i].ly, bounds[i].ux - bounds[i].lx, bounds[i].uy - bounds[i].ly));
}


#pragma mark - Properties


@end
