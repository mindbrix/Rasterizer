//
//  DocumentView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "DocumentView.h"
#import "RasterizerCG.hpp"
#import "RasterizerPDF.hpp"
#import "RasterizerSVG.hpp"
#import "RasterizerFont.hpp"
#import "RasterizerTest.hpp"
#import "RasterizerRenderer.hpp"
#import "RasterizerState.hpp"

@interface DocumentView () <NSFontChanging>

@property(nonatomic) CVDisplayLinkRef displayLink;
@property(nonatomic) NSFont *fnt;
@property(nonatomic) dispatch_semaphore_t inflight_semaphore;
@property(nonatomic) RasterizerState state;
@property(nonatomic) NSString *pastedString;
@property(nonatomic) BOOL showGlyphGrid;
@property(nonatomic) BOOL showTestScenes;
@property(nonatomic) size_t pageIndex;

- (void)timerFired:(double)time;

@end

static CVReturn OnDisplayLinkFrame(CVDisplayLinkRef displayLink, const CVTimeStamp *now, const CVTimeStamp *outputTime,
CVOptionFlags flagsIn, CVOptionFlags *flagsOut, void *displayLinkContext) {
    DocumentView *view = (__bridge DocumentView *)displayLinkContext;
    @autoreleasepool {
        if (dispatch_semaphore_wait(view.inflight_semaphore, DISPATCH_TIME_NOW) == 0)
            dispatch_async(dispatch_get_main_queue(), ^{
                [view timerFired:[NSDate date].timeIntervalSinceReferenceDate];
                dispatch_semaphore_signal(view.inflight_semaphore);
            });
    }
    return kCVReturnSuccess;
}

@implementation DocumentView

#pragma mark - NSView

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    self.pageIndex = 0;
    [self startTimer];
    return self;
}

- (void)removeFromSuperview {
    [self stopTimer];
    [super removeFromSuperview];
}

#pragma mark - NSTimer

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
    if (_state.getShouldRedraw()) {
        _state.onRedraw(
            self.layer.contentsScale,
            self.bounds.size.width,
            self.bounds.size.height
        );
        [self drawList:_state.list ctm:_state.ctm useCurves: _state.useCurves];
    }
}

#pragma mark - NSFontManager

- (void)changeFont:(id)sender {
    self.fnt = [[NSFontManager sharedFontManager] convertFont:[NSFont fontWithName:@"AppleSymbols" size:14]];
    [self writeList];
}
- (void)writeList {
    NSFont *fnt = self.fnt ?: [NSFont fontWithName:@"AppleSymbols" size:14];
    NSURL *url = RaCG::fontURL(fnt.fontName);
    Ra::Ref<RasterizerFont> font;  font->load(url.path.UTF8String, fnt.fontName.UTF8String);
    Ra::SceneList list;
    Ra::Colorant textColor(0, 0, 0, 255);
    if (self.pastedString) {
        Ra::Scene glyphs;
        RasterizerFont::layoutGlyphs(*font.ptr, float(fnt.pointSize), 0.f, textColor, RaCG::BoundsFromCGRect(self.bounds), false, false, false, self.pastedString.UTF8String, glyphs);
        list.addScene(glyphs);
    } else if (self.showGlyphGrid) {
        list.addScene(RasterizerFont::writeGlyphGrid(*font.ptr, float(fnt.pointSize), textColor));
    } else if (self.showTestScenes) {
        list = RasterizerTest::makeConcentrichron(*font.ptr);
    } else if (_svgData != nil)
        list.addScene(RasterizerSVG::createScene(_svgData.bytes, _svgData.length));
    else if (_pdfData != nil)
        RasterizerPDF::writeScene(_pdfData.bytes, _pdfData.length, self.pageIndex, list);

    _state.list = list;
    _state.setRedraw();
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
    _state.onFlags(event.modifierFlags);
}
- (void)keyDown:(NSEvent *)event {
//    NSLog(@"%d", event.keyCode);
    int keyCode = event.keyCode;
    if (keyCode == 36) {
        _state.onFit();
    } else if (_state.onKeyDown(event.keyCode, event.characters.UTF8String)) {
    } else if (keyCode == 5) {
        self.showTestScenes = NO;
        self.showGlyphGrid = !self.showGlyphGrid;
        self.pastedString = nil;
        [self writeList];
    } else if (keyCode == 17) {
        self.showGlyphGrid = NO;
        self.showTestScenes = !self.showTestScenes;
        self.pastedString = nil;
        [self writeList];
    } else if (keyCode == 51) {
        self.useCG = !self.useCG;
        self.rasterizerLabel.stringValue = self.useCG ? @"Core Graphics" : @"Rasterizer (GPU)";
        [self.rasterizerLabel setHidden:NO];
    } else if (keyCode == 15) {
        CGFloat native = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
        self.layer.contentsScale = self.layer.contentsScale == native ? 1.0 : native;
        [self.layer setNeedsDisplay];
    } else if (keyCode == 49) {
        [self.rasterizerLabel setHidden:YES];
    } else if (keyCode == 1)
        RaCG::screenGrabToPDF(_state.list, _state.ctm, self.bounds);
    else if (keyCode == 123) {
        if (self.pageIndex > 0) {
            self.pageIndex--;
            [self changeFont:nil];
        }
    } else if (keyCode == 124) {
        self.pageIndex++;
        [self changeFont:nil];
    } else
        [super keyDown:event];
    
}
- (void)keyUp:(NSEvent *)event {
    _state.onKeyUp(event.keyCode, event.characters.UTF8String);
}

- (void)paste:(id)sender {
	self.pastedString = [[[NSPasteboard generalPasteboard].pasteboardItems objectAtIndex:0] stringForType:NSPasteboardTypeString];
    self.showGlyphGrid = NO;
    self.showTestScenes = NO;
    [self writeList];
}

- (void)magnifyWithEvent:(NSEvent *)event {
    _state.onMagnify(float(1 + event.magnification));
}
- (void)rotateWithEvent:(NSEvent *)event {
    _state.onRotate(float(event.rotation / 10));
}
- (void)scrollWheel:(NSEvent *)event {
    CGFloat inversion = ([event respondsToSelector:@selector(isDirectionInvertedFromDevice)] && [event isDirectionInvertedFromDevice]) ? 1.0f : -1.0f;
    _state.onTranslate(float(event.deltaX * inversion), float(-event.deltaY * inversion));
}
- (void)mouseDown:(NSEvent *)event {
    _state.onMouseDown(float(event.locationInWindow.x), float(event.locationInWindow.y));
}
- (void)mouseDragged:(NSEvent *)event {
    _state.onDrag(float(event.deltaX), float(-event.deltaY));
}
- (void)mouseMoved:(NSEvent *)event {
    CGSize size = self.window.frame.size;
    CGFloat x = event.locationInWindow.x, y = event.locationInWindow.y;
    if (x >= 0 && x <= size.width && y >= 0 && y <= size.height)
        _state.onMouseMove(float(x), float(y));
}
- (void)mouseUp:(NSEvent *)event {
    _state.onMouseUp(float(event.locationInWindow.x), float(event.locationInWindow.y));
}

#pragma mark - Properies

- (void)setPdfData:(NSData *)pdfData {
    if ((_pdfData = pdfData)) {
        [self writeList];
    }
}

- (void)setSvgData:(NSData *)svgData {
    if ((_svgData = svgData))
        [self writeList];
}
@end
