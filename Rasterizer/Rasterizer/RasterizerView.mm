//
//  RasterizerView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "RasterizerView.h"
#import "RasterizerCG.hpp"
#import "RasterizerRenderer.hpp"
#import "RasterizerDB.hpp"
#import "RasterizerSVG.hpp"
#import "RasterizerFont.hpp"
#import "RasterizerTest.hpp"
#import "RasterizerLayer.h"

@interface RasterizerView () <CALayerDelegate, LayerDelegate>

@property(nonatomic) CVDisplayLinkRef displayLink;
@property(nonatomic) dispatch_semaphore_t inflight_semaphore;
@property(nonatomic) RasterizerRenderer renderer;
@property(nonatomic) Ra::Ref<RasterizerDB> db;
@property(nonatomic) RasterizerState state;
@property(nonatomic) Ra::SceneList list;
@property(nonatomic) Ra::Ref<RasterizerTest> test;
@property(nonatomic) BOOL useCG;
@property(nonatomic) NSString *pastedString;
- (void)timerFired:(double)time;

@end

static CVReturn OnDisplayLinkFrame(CVDisplayLinkRef displayLink, const CVTimeStamp *now, const CVTimeStamp *outputTime,
CVOptionFlags flagsIn, CVOptionFlags *flagsOut, void *displayLinkContext) {
    RasterizerView *view = (__bridge RasterizerView *)displayLinkContext;
    @autoreleasepool {
        if (dispatch_semaphore_wait(view.inflight_semaphore, DISPATCH_TIME_NOW) == 0)
            dispatch_async(dispatch_get_main_queue(), ^{
                [view timerFired:[NSDate date].timeIntervalSinceReferenceDate];
                dispatch_semaphore_signal(view.inflight_semaphore);
            });
    }
    return kCVReturnSuccess;
}

@implementation RasterizerView

#pragma mark - NSView

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    self.inflight_semaphore = dispatch_semaphore_create(1);
    [self initLayer:_useCG];
    [self changeFont:nil];
    return self;
}

- (void)removeFromSuperview {
    [self stopTimer];
    [super removeFromSuperview];
}

#pragma mark - NSTimer

- (void)stopTimer {
    if (_displayLink)
        CVDisplayLinkStop(_displayLink), CVDisplayLinkRelease(_displayLink), _displayLink = nil;
}

- (void)resetTimer {
    [self stopTimer];
    CVReturn cvReturn = CVDisplayLinkCreateWithCGDisplay(CGMainDisplayID(), &_displayLink);
    cvReturn = CVDisplayLinkSetOutputCallback(_displayLink, &OnDisplayLinkFrame, (__bridge void *)self);
    CVDisplayLinkStart(_displayLink);
}

- (void)timerFired:(double)time {
    if (_state.needsRedraw()) {
        _state.readEvents(_list,
            _db->db ? RasterizerDB::EventFunction : NULL,
                _db->db ? (void *)_db.ref : nullptr,
            _db->db ? RasterizerDB::WriteFunction : RasterizerTest::WriteFunction,
                _db->db ? (void *)_db.ref : (void *)_test.ref,
            RasterizerTest::TransferFunction,
                _test.ref);
        [self.layer setNeedsDisplay];
    }
    _state.resetEvents();
}

- (void)toggleTimer {
    if (_displayLink)
        [self stopTimer];
    else
        [self resetTimer];
}

#pragma mark - NSFontManager

- (void)changeFont:(id)sender {
    NSFont *fnt = [[NSFontManager sharedFontManager] convertFont:[NSFont fontWithName:@"AppleSymbols" size:14]];
    NSURL *url = RaCG::fontURL(fnt.fontName);
    Ra::Ref<RasterizerFont> font;  font->set(url.path.UTF8String, fnt.fontName.UTF8String);
    Ra::SceneList list;
    if ([_dbURL isFileURL]) {
        _db->font = font;
        _db->writeTables(RaCG::BoundsFromCGRect(self.bounds));
    } else if (_svgData != nil)
        RasterizerSVG::writeScene(_svgData.bytes, _svgData.length, list);
    else {
        Ra::Scene glyphs;
        if (self.pastedString)
            RasterizerFont::writeGlyphs(*font.ref, float(fnt.pointSize), Ra::Colorant(0, 0, 0, 255), RaCG::BoundsFromCGRect(self.bounds), false, false, false, self.pastedString.UTF8String, glyphs);
        else
            RasterizerFont::writeGlyphGrid(*font.ref, float(fnt.pointSize), Ra::Colorant(0, 0, 0, 255), glyphs);
        list.addScene(glyphs);
    }
    _test->addTestScenes(list, _state, RaCG::BoundsFromCGRect(self.bounds), *font.ref);
    _state.writeEvent(RasterizerState::Event(0.0, RasterizerState::Event::kNull, size_t(0)));
}

#pragma mark - Drawing

- (void)initLayer:(BOOL)useCPU {
    [self setWantsLayer:YES];
    CGFloat scale = self.layer.contentsScale ?: [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    if (useCPU) {
        [self setLayer:[CALayer layer]];
        self.layer.contentsFormat = kCAContentsFormatRGBA8Uint;
        self.layer.delegate = self;
        self.layer.magnificationFilter = kCAFilterNearest;
    } else {
        [self setLayer:[RasterizerLayer layer]];
        ((RasterizerLayer *)self.layer).layerDelegate = self;
    }
    self.layer.contentsScale = scale;
    self.layer.bounds = self.bounds;
    self.layer.opaque = YES;
    self.layer.needsDisplayOnBoundsChange = YES;
    self.layer.actions = @{ @"onOrderIn": [NSNull null], @"onOrderOut": [NSNull null], @"sublayers": [NSNull null], @"contents": [NSNull null], @"backgroundColor": [NSNull null], @"bounds": [NSNull null] };
    [self.layer setNeedsDisplay];
}

- (void)updateRasterizerLabel {
    self.rasterizerLabel.stringValue = _useCG ? @"Core Graphics" : @"Rasterizer (GPU)";
}

#pragma mark - NSResponder

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    self.window.acceptsMouseMovedEvents = YES;
    [self.rasterizerLabel setHidden:YES];
    [self toggleTimer];
    return YES;
}

- (void)flagsChanged:(NSEvent *)event {
    _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kFlags, event.modifierFlags));
}
- (void)keyDown:(NSEvent *)event {
    NSLog(@"%d", event.keyCode);
    int keyCode = event.keyCode;
    if (keyCode == 36) {
        _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kFit, _list.bounds()));
    } else if (_state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kKeyDown, event.keyCode))) {}
    else if (keyCode == 51) {
        _useCG = !_useCG;
        [self initLayer:_useCG];
        _renderer.reset();
        [self updateRasterizerLabel];
        [self.rasterizerLabel setHidden:NO];
    } else if (keyCode == 15) {
        CGFloat native = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
        self.layer.contentsScale = self.layer.contentsScale == native ? 1.0 : native;
        [self.layer setNeedsDisplay];
    } else if (keyCode == 49) {
        [self.rasterizerLabel setHidden:YES];
    } else if (keyCode == 1)
        RaCG::screenGrabToPDF(_list, _state, self.bounds);
    else {
        [super keyDown:event];
    }
}
- (void)keyUp:(NSEvent *)event {
    _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kKeyUp, event.keyCode));
}

- (void)paste:(id)sender {
	self.pastedString = [[[NSPasteboard generalPasteboard].pasteboardItems objectAtIndex:0] stringForType:NSPasteboardTypeString];
    [self changeFont: nil];
}

- (void)magnifyWithEvent:(NSEvent *)event {
     _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kMagnify, float(1 + event.magnification), 0.f));
}
- (void)rotateWithEvent:(NSEvent *)event {
    if (!(event.modifierFlags & NSEventModifierFlagShift))
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
    _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kMouseMove, float(event.locationInWindow.x), float(event.locationInWindow.y)));
}
- (void)mouseUp:(NSEvent *)event {
    _state.writeEvent(RasterizerState::Event(event.timestamp, RasterizerState::Event::kMouseUp, float(event.locationInWindow.x), float(event.locationInWindow.y)));
}

#pragma mark - LayerDelegate

- (void)writeBuffer:(Ra::Buffer *)buffer forLayer:(CALayer *)layer {
    _state.update(self.layer.contentsScale, self.bounds.size.width, self.bounds.size.height);
    buffer->clearColor = _svgData && _state.outlineWidth == 0.f ? Ra::Colorant(0xCC, 0xCC, 0xCC, 0xCC) : Ra::Colorant(0xFF, 0xFF, 0xFF, 0xFF);
    _renderer.renderList(_list, _state, buffer);
}

#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    _state.update(self.layer.contentsScale, self.bounds.size.width, self.bounds.size.height);
    Ra::Colorant color = _svgData && _state.outlineWidth == 0.f ? Ra::Colorant(0xCC, 0xCC, 0xCC, 0xCC) : Ra::Colorant(0xFF, 0xFF, 0xFF, 0xFF);
    memset_pattern4(CGBitmapContextGetData(ctx), & color.src0, CGBitmapContextGetBytesPerRow(ctx) * CGBitmapContextGetHeight(ctx));
    CGContextConcatCTM(ctx, RaCG::CGFromTransform(_state.ctm));
    RaCG::drawList(_list, _state, ctx);
}

#pragma mark - Properies

- (void)setDbURL:(NSURL *)dbURL {
    if ((_dbURL = dbURL)) {
        _db->open(dbURL.path.UTF8String);
        RaCG::writeFontsTable(*_db.ref);
        [self changeFont:nil];
    }
}

- (void)setSvgData:(NSData *)svgData {
    if ((_svgData = svgData))
        [self changeFont:nil];
}
@end
