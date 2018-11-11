//
//  ZoomableView.m
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "ZoomableView.h"
#import "VGAffineTransform.h"

@interface ZoomableView ()

@property(nonatomic) VGAffineTransform *transform;

@end


@implementation ZoomableView

#pragma mark - NSView

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    self.transform = [VGAffineTransform new];
    return self;
}

- (void)drawRect:(NSRect)dirtyRect {}


#pragma mark - Override

- (void)redraw {}


#pragma mark - NSResponder

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    return YES;
}

- (void)magnifyWithEvent:(NSEvent *)event {
    [self.transform scaleBy:1.0f + event.magnification bounds:self.bounds];
    [self redraw];
}

- (void)rotateWithEvent:(NSEvent *)event {
    if (!(event.modifierFlags & NSEventModifierFlagShift))
        [self.transform rotateBy:event.rotation / 10.f bounds:self.bounds];
    [self redraw];
}

- (void)mouseDragged:(NSEvent *)event {
    [self.transform translateByX:event.deltaX andY:-event.deltaY];
    [self redraw];
}

- (void)scrollWheel:(NSEvent *)event {
    BOOL isInverted = ([event respondsToSelector:@selector(isDirectionInvertedFromDevice)] && [event isDirectionInvertedFromDevice]);
    CGFloat inversion = isInverted ? 1.0f : -1.0f;
    [self.transform translateByX:event.deltaX * inversion andY:-event.deltaY * inversion];
    [self redraw];
}


#pragma mark - Properties

- (CGAffineTransform)CTM {
    return self.transform.affineTransform;
}

- (void)setCTM:(CGAffineTransform)CTM {
    self.transform = [[VGAffineTransform alloc] initWithTransform:CTM];
    [self redraw];
}

@end
