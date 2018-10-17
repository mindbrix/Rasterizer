//
//  RasterizerView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "RasterizerView.h"
#import "RasterizerLayer.h"

@interface RasterizerView () <RasterizerLayerDelegate>

@property(nonatomic) NSData *gridRectsBacking;
@property(nonatomic, readonly) RasterizerLayer *rasterizerLayer;
@property(nonatomic) BOOL useRasterizer;
@end


@implementation RasterizerView

+ (NSData *)createGridRects:(size_t)count cellSize:(size_t)cellSize {
    NSMutableData *backing = [NSMutableData dataWithLength:count * sizeof(CGRect)];
    CGRect *rects = (CGRect *)backing.bytes;
    CGFloat phi = (sqrt(5) - 1) / 2;
    CGFloat wh = CGFloat(cellSize) * phi;
    size_t dimension = ceil(sqrt(CGFloat(count)));
    for (size_t i = 0; i < count; i++)
        rects[i] = CGRectMake((i % dimension) * cellSize, (i / dimension) * cellSize, wh, wh);
    return [NSData dataWithData:backing];
}


#pragma mark - NSView

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    
    [self setWantsLayer:YES];
    [self setLayer:[RasterizerLayer layer]];
    self.rasterizerLayer.contentsScale = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    self.rasterizerLayer.backgroundColor = CGColorGetConstantColor(kCGColorWhite);
    self.rasterizerLayer.layerDelegate = self;
    self.rasterizerLayer.bounds = self.bounds;
    self.gridRectsBacking = [self.class createGridRects:10000 cellSize:24];
    return self;
}

- (void)drawRect:(NSRect)dirtyRect {}


#pragma mark - Drawing

- (void)redraw {
    self.rasterizerLayer.colorSpace = self.window.colorSpace.CGColorSpace;
    [self.rasterizerLayer setNeedsDisplay];
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


#pragma mark - RasterizerLayerDelegate

- (void)writeBitmapIntoContext:(CGContextRef)ctx forLayer:(CALayer *)layer {
    CGContextConcatCTM(ctx, self.CTM);
    size_t count = self.gridRectsBacking.length / sizeof(CGRect);
    CGRect *rects = (CGRect *)self.gridRectsBacking.bytes;
    for (size_t i = 0; i < count; i++)
        CGContextFillRect(ctx, rects[i]);
}


#pragma mark - Properties

-(RasterizerLayer *)rasterizerLayer {
    return (RasterizerLayer *)self.layer;
}

@end
