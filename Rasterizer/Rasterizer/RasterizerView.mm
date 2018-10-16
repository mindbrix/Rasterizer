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
@property(nonatomic) RasterizerLayer *rasterizerLayer;

@end


@implementation RasterizerView

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
    self.gridRectsBacking = [self createGridRects:10000 cellSize:24];
    return self;
}


- (void)drawRect:(NSRect)dirtyRect {}

- (void)redraw {
    self.rasterizerLayer.colorSpace = self.window.colorSpace.CGColorSpace;
    [self.rasterizerLayer setNeedsDisplay];
}

- (NSData *)createGridRects:(size_t)count cellSize:(size_t)cellSize {
    NSMutableData *backing = [NSMutableData dataWithLength:count * sizeof(CGRect)];
    CGRect *rect = (CGRect *)backing.bytes;
    CGFloat phi = (sqrt(5) - 1) / 2;
    CGFloat wh = CGFloat(cellSize) * phi;
    size_t dimension = ceil(sqrt(CGFloat(count)));
    for (size_t i = 0; i < count; i++) {
        size_t column = i % dimension, row = i / dimension;
        *rect++ = CGRectMake(column * cellSize, row * cellSize, wh, wh);
    }
    return [NSData dataWithData:backing];
}

#pragma mark - NSResponder

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    [self redraw];
    return YES;
}


#pragma mark - RasterizerLayerDelegate

- (void)writeBitmap:(CGContextRef)ctx forLayer:(CALayer *)layer {
    void *data = CGBitmapContextGetData(ctx);
    if (data) {
        CGContextConcatCTM(ctx, self.CTM);
        if (self.gridRectsBacking != nil) {
            size_t count = self.gridRectsBacking.length / sizeof(CGRect);
            CGRect *rects = (CGRect *)self.gridRectsBacking.bytes;
            for (size_t i = 0; i < count; i++)
                CGContextFillRect(ctx, rects[i]);
        } else
            CGContextFillRect(ctx, CGRectMake(0, 0, 100, 100));
    }
}


#pragma mark - Properties

-(RasterizerLayer *)rasterizerLayer {
    return (RasterizerLayer *)self.layer;
}

@end
