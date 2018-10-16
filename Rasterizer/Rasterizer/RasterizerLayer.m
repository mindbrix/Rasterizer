//
//  RasterizerLayer.m
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "RasterizerLayer.h"

@implementation RasterizerLayer

- (void)display {
    if (self.colorSpace != nil && [self.layerDelegate respondsToSelector:@selector(writeBitmap:forLayer:)]) {
         CGContextRef bitmapContext = CGBitmapContextCreate(NULL, self.bounds.size.width * self.contentsScale, self.bounds.size.height * self.contentsScale, 8, self.bounds.size.width * self.contentsScale * 4, self.colorSpace, kCGImageAlphaPremultipliedFirst| kCGBitmapByteOrder32Little);
        
        [self.layerDelegate writeBitmap:bitmapContext forLayer:self];
        
        CGImageRef img = CGBitmapContextCreateImage(bitmapContext);
        self.contents = (__bridge id)img;
        CGImageRelease(img);
        CGContextRelease(bitmapContext);
    }
}

@end
