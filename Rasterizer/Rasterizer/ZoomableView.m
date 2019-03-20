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

@property(nonatomic) BOOL eventFlag;
@property(nonatomic) VGAffineTransform *transform;
@property(nonatomic) CVDisplayLinkRef displayLink;
- (void)timerFired:(double)time;

@end

static CVReturn OnDisplayLinkFrame(CVDisplayLinkRef displayLink,
                                   const CVTimeStamp *now,
                                   const CVTimeStamp *outputTime,
                                   CVOptionFlags flagsIn,
                                   CVOptionFlags *flagsOut,
                                   void *displayLinkContext) {
    ZoomableView *view = (__bridge ZoomableView *)displayLinkContext;
    @autoreleasepool {
        dispatch_async(dispatch_get_main_queue(), ^{
            [view timerFired:[NSDate date].timeIntervalSinceReferenceDate];
        });
    }
    return kCVReturnSuccess;
}


@implementation ZoomableView

#pragma mark - NSView

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    self.transform = [VGAffineTransform new];
    return self;
}

- (void)removeFromSuperview {
    [self stopTimer];
    [super removeFromSuperview];
}

- (void)drawRect:(NSRect)dirtyRect {}


#pragma mark - NSTimer

- (void)stopTimer {
    if (_displayLink) {
        CVDisplayLinkStop(_displayLink);
        CVDisplayLinkRelease(_displayLink);
        _displayLink = nil;
    }
}

- (void)resetTimer {
    [self stopTimer];
    CVReturn cvReturn = CVDisplayLinkCreateWithCGDisplay(CGMainDisplayID(), &_displayLink);
    cvReturn = CVDisplayLinkSetOutputCallback(_displayLink, &OnDisplayLinkFrame, (__bridge void *)self);
    CVDisplayLinkStart(_displayLink);
}

- (void)timerFired:(double)time {
    if (_eventFlag)
        [self redraw];
    _eventFlag = NO;
}

- (void)toggleTimer {
    if (_displayLink)
        [self stopTimer];
    else
        [self resetTimer];
    [self redraw];
}

#pragma mark - Override

- (void)redraw {}


#pragma mark - NSResponder

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    return YES;
}

- (void)keyDown:(NSEvent *)event {
    NSLog(@"%d", event.keyCode);
    if (event.keyCode == 36) {
        self.CTM = CGAffineTransformIdentity;
        [self redraw];
    } else {
        [super keyDown:event];
    }
}

- (void)magnifyWithEvent:(NSEvent *)event {
    [self.transform scaleBy:1.0f + event.magnification bounds:self.bounds];
    _eventFlag = YES;
    if (_displayLink == nil)
        [self redraw];
}

- (void)rotateWithEvent:(NSEvent *)event {
    if (!(event.modifierFlags & NSEventModifierFlagShift)) {
        [self.transform rotateBy:event.rotation / 10.f bounds:self.bounds];
        _eventFlag = YES;
    }
    if (_displayLink == nil)
        [self redraw];
}

- (void)mouseDragged:(NSEvent *)event {
    [self.transform translateByX:event.deltaX andY:-event.deltaY];
    _eventFlag = YES;
    if (_displayLink == nil)
        [self redraw];
}

- (void)scrollWheel:(NSEvent *)event {
    BOOL isInverted = ([event respondsToSelector:@selector(isDirectionInvertedFromDevice)] && [event isDirectionInvertedFromDevice]);
    CGFloat inversion = isInverted ? 1.0f : -1.0f;
    [self.transform translateByX:event.deltaX * inversion andY:-event.deltaY * inversion];
    _eventFlag = YES;
    if (_displayLink == nil)
        [self redraw];
}


#pragma mark - Properties

- (CGAffineTransform)CTM {
    return self.transform.affineTransform;
}

- (void)setCTM:(CGAffineTransform)CTM {
    self.transform = [[VGAffineTransform alloc] initWithTransform:CTM];
    _eventFlag = YES;
    if (_displayLink == nil)
        [self redraw];
}

@end
