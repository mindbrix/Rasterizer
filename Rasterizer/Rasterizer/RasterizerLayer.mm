//
//  RasterizerLayer.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "RasterizerLayer.h"
#import <cmath>

@implementation RasterizerLayer

- (id)init {
    self = [super init];
    if (!self)
        return nil;
    
    self.opaque = NO;
    self.needsDisplayOnBoundsChange = YES;
    self.actions = @{ @"onOrderIn": [NSNull null],
                      @"onOrderOut": [NSNull null],
                      @"sublayers": [NSNull null],
                      @"contents": [NSNull null],
                      @"backgroundColor": [NSNull null],
                      @"bounds": [NSNull null] };
    return self;
}

- (void)display {
    if (self.colorSpace != nil && [self.layerDelegate respondsToSelector:@selector(writeBitmap:forLayer:)]) {
        size_t width = ceil(self.bounds.size.width * self.contentsScale);
        size_t height = ceil(self.bounds.size.height * self.contentsScale);
        size_t bpp = 8;
        size_t components = CGColorSpaceGetNumberOfComponents(self.colorSpace);
        CGContextRef bitmapContext = CGBitmapContextCreate(NULL, width, height, bpp, width * (components + 1) * bpp / 8 , self.colorSpace,
                                                            kCGImageAlphaPremultipliedFirst| kCGBitmapByteOrder32Little);
        if (bitmapContext != NULL) {
            [self.layerDelegate writeBitmap:bitmapContext forLayer:self];
            CGImageRef img = CGBitmapContextCreateImage(bitmapContext);
            self.contents = (__bridge id)img;
            CGImageRelease(img);
            CGContextRelease(bitmapContext);
        }
    }
}

@end
