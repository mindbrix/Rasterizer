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
    self.rasterizerLayer.needsDisplayOnBoundsChange = TRUE;
    self.rasterizerLayer.bounds = self.bounds;
    return self;
}


- (void)drawRect:(NSRect)dirtyRect {}

- (void)redraw {
    self.rasterizerLayer.colorSpace = self.window.colorSpace.CGColorSpace;
    [self.rasterizerLayer setNeedsDisplay];
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
    uint8_t bgraRed[4] = { 0, 0, 255, 255 };
    if (data) {
        size_t size = CGBitmapContextGetHeight(ctx) * CGBitmapContextGetBytesPerRow(ctx);
        memset_pattern4(data, bgraRed, size);
    }
}


#pragma mark - Properties

-(RasterizerLayer *)rasterizerLayer {
    return (RasterizerLayer *)self.layer;
}

@end
