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
         CGContextRef bitmapContext = CGBitmapContextCreate(NULL, ceil(self.bounds.size.width * self.contentsScale), ceil(self.bounds.size.height * self.contentsScale), 8, ceil(self.bounds.size.width * self.contentsScale) * 4, self.colorSpace, kCGImageAlphaPremultipliedFirst| kCGBitmapByteOrder32Little);
        
        [self.layerDelegate writeBitmap:bitmapContext forLayer:self];
        
        CGImageRef img = CGBitmapContextCreateImage(bitmapContext);
        self.contents = (__bridge id)img;
        CGImageRelease(img);
        CGContextRelease(bitmapContext);
    }
}

@end
