//
//  DemoView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "DemoView.h"
#import "RasterizerCG.hpp"
#import "RasterizerDemo.hpp"

@interface DemoView () <NSFontChanging, ListDelegate>

@property(nonatomic) CVDisplayLinkRef displayLink;
@property(nonatomic) dispatch_semaphore_t inflight_semaphore;
@property(nonatomic) RasterizerDemo demo;
@property(nonatomic) NSFont *font;

- (void)timerFired:(double)time;

@end

static CVReturn OnDisplayLinkFrame(CVDisplayLinkRef displayLink, const CVTimeStamp *now, const CVTimeStamp *outputTime,
CVOptionFlags flagsIn, CVOptionFlags *flagsOut, void *displayLinkContext) {
    DemoView *view = (__bridge DemoView *)displayLinkContext;
    @autoreleasepool {
        if (dispatch_semaphore_wait(view.inflight_semaphore, DISPATCH_TIME_NOW) == 0)
            dispatch_async(dispatch_get_main_queue(), ^{
                [view timerFired:[NSDate date].timeIntervalSinceReferenceDate];
                dispatch_semaphore_signal(view.inflight_semaphore);
            });
    }
    return kCVReturnSuccess;
}

@implementation DemoView

#pragma mark - NSView

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    self.listDelegate = self;
    self.font = nil;
    _demo.setUseGPU(!self.useCG);
    [self startTimer];
    return self;
}

- (void)removeFromSuperview {
    [self stopTimer];
    [super removeFromSuperview];
}

#pragma mark - CVDisplayLink

- (void)startTimer {
    _inflight_semaphore = dispatch_semaphore_create(1);
    CVReturn cvReturn = CVDisplayLinkCreateWithCGDisplay(CGMainDisplayID(), &_displayLink);
    cvReturn = CVDisplayLinkSetOutputCallback(_displayLink, &OnDisplayLinkFrame, (__bridge void *)self);
    CVDisplayLinkStart(_displayLink);
}

- (void)stopTimer {
    if (_displayLink)
        CVDisplayLinkStop(_displayLink), CVDisplayLinkRelease(_displayLink), _displayLink = nil;
}

- (void)timerFired:(double)time {
    if (_demo.getShouldRedraw()) {
        _demo.onRedraw(
            self.bounds.size.width,
            self.bounds.size.height
        );
        [self.layer setNeedsDisplay];
    }
}


#pragma mark - ListDelegate

- (Ra::DrawList)getList: (float)width height:(float) height {
    return _demo.getDrawList(width, height);
}


#pragma mark - NSFontManager

- (void)changeFont:(id)sender {
    self.font = [[NSFontManager sharedFontManager] convertFont:[NSFont fontWithName:@"HelveticaNeue-Medium" size:14]];
}


#pragma mark - NSResponder

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    self.window.acceptsMouseMovedEvents = YES;
    return YES;
}

- (void)flagsChanged:(NSEvent *)event {
    _demo.onFlags(event.modifierFlags);
}
- (void)keyDown:(NSEvent *)event {
//    NSLog(@"%d", event.keyCode);
    int keyCode = event.keyCode;
    if (_demo.onKeyDown(event.keyCode))
        ;
    else if (keyCode == 29) {  // 0
        self.useCG = !self.useCG;
        _demo.setUseGPU(!self.useCG);
    } else if (keyCode == 15) {  // R
        CGFloat native = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
        self.layer.contentsScale = self.layer.contentsScale == native ? 1.0 : native;
        [self.layer setNeedsDisplay];
    } else
        [super keyDown:event];
    
}
- (void)keyUp:(NSEvent *)event {
    _demo.onKeyUp(event.keyCode);
}

- (void)paste:(id)sender {
    NSString *pasted = [[[NSPasteboard generalPasteboard].pasteboardItems objectAtIndex:0] stringForType:NSPasteboardTypeString];
    _demo.onPaste(pasted.UTF8String, RaCG::BoundsFromCGRect(self.bounds));
}

- (void)magnifyWithEvent:(NSEvent *)event {
    _demo.onMagnify(float(1 + event.magnification));
}
- (void)rotateWithEvent:(NSEvent *)event {
    _demo.onRotate(float(event.rotation / 10));
}
- (void)scrollWheel:(NSEvent *)event {
    CGFloat inversion = ([event respondsToSelector:@selector(isDirectionInvertedFromDevice)] && [event isDirectionInvertedFromDevice]) ? 1.0f : -1.0f;
    _demo.onTranslate(float(event.deltaX * inversion), float(-event.deltaY * inversion));
}
- (void)mouseDown:(NSEvent *)event {
    _demo.onMouseDown(float(event.locationInWindow.x), float(event.locationInWindow.y));
}
- (void)mouseDragged:(NSEvent *)event {
    _demo.onDrag(float(event.deltaX), float(-event.deltaY));
}
- (void)mouseMoved:(NSEvent *)event {
    CGSize size = self.window.frame.size;
    CGFloat x = event.locationInWindow.x, y = event.locationInWindow.y;
    if (x >= 0 && x <= size.width && y >= 0 && y <= size.height)
        _demo.onMouseMove(float(x), float(y));
}
- (void)mouseUp:(NSEvent *)event {
    _demo.onMouseUp(float(event.locationInWindow.x), float(event.locationInWindow.y));
}

#pragma mark - Properies

- (void)setFont:(NSFont *)font {
    _font = font ?: [NSFont fontWithName:@"HelveticaNeue-Medium" size:14];

    NSURL *url = RaCG::fontURL(_font.fontName);
    _demo.setFont(url.path.UTF8String, _font.fontName.UTF8String, _font.pointSize);
}

- (void)setPdfData:(NSData *)pdfData {
    if (pdfData)
        _demo.setPdfData(pdfData.bytes, pdfData.length);
}

- (void)setSvgData:(NSData *)svgData {
    if (svgData)
        _demo.setSvgData(svgData.bytes, svgData.length);
}
@end
