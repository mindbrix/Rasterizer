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


@interface DocumentView () <NSFontChanging>

@property(nonatomic) CVDisplayLinkRef displayLink;
@property(nonatomic) NSFont *fnt;
@property(nonatomic) dispatch_semaphore_t inflight_semaphore;
@property(nonatomic) RasterizerState state;
@property(nonatomic) Ra::SceneList list;
@property(nonatomic) Ra::Ref<RasterizerTest> test;
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
    if (_state.needsRedraw()) {
        _state.readEvents(_list);
        
        float scale = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
        _state.update(scale, w, h);
        _state.runTransferFunction(_list, RasterizerTest::TransferFunction);
        
        self.sceneList = _list;
        self.ctm = _state.ctm;
        self.useCurves = _state.useCurves;
    }
}

#pragma mark - NSFontManager

- (void)changeFont:(id)sender {
    self.fnt = [[NSFontManager sharedFontManager] convertFont:[NSFont fontWithName:@"AppleSymbols" size:14]];
    [self writeList: self.fnt];
}
- (void)writeList:(NSFont *)fnt {
    NSURL *url = RaCG::fontURL(fnt.fontName);
    Ra::Ref<RasterizerFont> font;  font->load(url.path.UTF8String, fnt.fontName.UTF8String);
    Ra::SceneList list;
    Ra::Scene glyphs;
    if (self.pastedString) {
        RasterizerFont::layoutGlyphs(*font.ptr, float(fnt.pointSize), 0.f, Ra::Colorant(0, 0, 0, 255), RaCG::BoundsFromCGRect(self.bounds), false, false, false, self.pastedString.UTF8String, glyphs);
        list.addScene(glyphs);
    } else if (self.showGlyphGrid) {
        RasterizerFont::writeGlyphGrid(*font.ptr, float(fnt.pointSize), Ra::Colorant(0, 0, 0, 255), glyphs);
        list.addScene(glyphs);
    } else if (self.showTestScenes) {
        _test->addTestScenes(list, _state, _state.bounds, *font.ptr);
    } else if (_svgData != nil)
        RasterizerSVG::writeScene(_svgData.bytes, _svgData.length, list);
    else if (_pdfData != nil)
        RasterizerPDF::writeScene(_pdfData.bytes, _pdfData.length, self.pageIndex, list);

    _list = list;
    _state.writeEvent(RasterizerState::Event(0.0, RasterizerState::Event::kNull, size_t(0)));
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
    _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kFlags, event.modifierFlags));
}
- (void)keyDown:(NSEvent *)event {
//    NSLog(@"%d", event.keyCode);
    int keyCode = event.keyCode;
    if (keyCode == 36) {
        _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kFit, _list.bounds()));
    } else if (_state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kKeyDown, event.keyCode, event.characters.UTF8String))) {}
    else if (keyCode == 5) {
        self.showTestScenes = NO;
        self.showGlyphGrid = !self.showGlyphGrid;
        self.pastedString = nil;
        [self writeList: self.fnt];
    } else if (keyCode == 17) {
        self.showGlyphGrid = NO;
        self.showTestScenes = !self.showTestScenes;
        self.pastedString = nil;
        [self writeList: self.fnt];
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
        RaCG::screenGrabToPDF(_list, _state.ctm, self.bounds);
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
    _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kKeyUp, event.keyCode, event.characters.UTF8String));
}

- (void)paste:(id)sender {
	self.pastedString = [[[NSPasteboard generalPasteboard].pasteboardItems objectAtIndex:0] stringForType:NSPasteboardTypeString];
    self.showGlyphGrid = NO;
    self.showTestScenes = NO;
    [self writeList: self.fnt];
}

- (void)magnifyWithEvent:(NSEvent *)event {
     _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kMagnify, float(1 + event.magnification), 0.f));
}
- (void)rotateWithEvent:(NSEvent *)event {
    if (!(event.modifierFlags & NSEventModifierFlagControl))
        _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kRotate, float(event.rotation / 10), 0.f));
}
- (void)scrollWheel:(NSEvent *)event {
    CGFloat inversion = ([event respondsToSelector:@selector(isDirectionInvertedFromDevice)] && [event isDirectionInvertedFromDevice]) ? 1.0f : -1.0f;
    _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kTranslate, float(event.deltaX * inversion), float(-event.deltaY * inversion)));
}
- (void)mouseDown:(NSEvent *)event {
    _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kMouseDown, float(event.locationInWindow.x), float(event.locationInWindow.y)));
}
- (void)mouseDragged:(NSEvent *)event {
    _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kDragged, float(event.deltaX), float(-event.deltaY)));
}
- (void)mouseMoved:(NSEvent *)event {
    CGSize size = self.window.frame.size;
    CGFloat x = event.locationInWindow.x, y = event.locationInWindow.y;
    if (x >= 0 && x <= size.width && y >= 0 && y <= size.height)
        _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kMouseMove, float(x), float(y)));
}
- (void)mouseUp:(NSEvent *)event {
    _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kMouseUp, float(event.locationInWindow.x), float(event.locationInWindow.y)));
}

#pragma mark - Properies

- (void)setPdfData:(NSData *)pdfData {
    if ((_pdfData = pdfData)) {
        [self changeFont:nil];
    }
}

- (void)setSvgData:(NSData *)svgData {
    if ((_svgData = svgData))
        [self changeFont:nil];
}
@end
