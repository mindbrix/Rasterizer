//
//  RasterizerView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "RasterizerView.h"
#import "RasterizerCG.hpp"
#import "RasterizerDB.hpp"
#import "RasterizerSVG.hpp"
#import "RasterizerFont.hpp"
#import "RasterizerTest.hpp"
#import "RasterizerLayer.h"

@interface RasterizerView () <CALayerDelegate, LayerDelegate>

@property(nonatomic) CVDisplayLinkRef displayLink;
@property(nonatomic) RaCG::CGTestContext testScene;
@property(nonatomic) Ra::Ref<RasterizerDB> db;
@property(nonatomic) RasterizerState state;
@property(nonatomic) Ra::SceneList list;
@property(nonatomic) BOOL useCG;
@property(nonatomic) NSFont *font;
@property(nonatomic) NSString *pastedString;
- (void)timerFired:(double)time;

@end

static CVReturn OnDisplayLinkFrame(CVDisplayLinkRef displayLink, const CVTimeStamp *now, const CVTimeStamp *outputTime,
CVOptionFlags flagsIn, CVOptionFlags *flagsOut, void *displayLinkContext) {
    RasterizerView *view = (__bridge RasterizerView *)displayLinkContext;
    @autoreleasepool {
        dispatch_async(dispatch_get_main_queue(), ^{
            [view timerFired:[NSDate date].timeIntervalSinceReferenceDate];
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
    [self initLayer:_useCG];
    self.font = [NSFont fontWithName:@"AppleSymbols" size:14];
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
    [self readEvents: time];
}

- (void)toggleTimer {
    if (_displayLink)
        [self stopTimer];
    else
        [self resetTimer];
}

#pragma mark - NSFontManager

- (void)changeFont:(id)sender {
    self.font = [[NSFontManager sharedFontManager] convertFont:self.font];
    NSURL *url = RaCG::fontURL(self.font.fontName);
    RasterizerFont font(url.path.UTF8String);
    font.set(self.font.fontName.UTF8String);
    Ra::SceneList list;
    if ([_dbURL isFileURL]) {
        _db->writeTables(font, RaCG::BoundsFromCGRect(self.bounds));
        list.addList(_db->list);
    } else if (_svgData != nil)
        RasterizerSVG::writeScene(_svgData.bytes, _svgData.length, list);
    else {
        Ra::Scene glyphs;
        if (self.pastedString)
            RasterizerFont::writeGlyphs(font, float(self.font.pointSize), Ra::Colorant(0, 0, 0, 255), RaCG::BoundsFromCGRect(self.bounds), false, false, false, self.pastedString.UTF8String, glyphs);
        else
            RasterizerFont::writeGlyphGrid(font, float(self.font.pointSize), Ra::Colorant(0, 0, 0, 255), glyphs);
        list.addScene(glyphs);
    }
    RasterizerTest::addTestScenes(list, RaCG::BoundsFromCGRect(self.bounds), font);
    _list.empty().addList(list);
    [self.layer setNeedsDisplay];
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

#pragma mark - AppState

- (BOOL)writeEvent:(RasterizerState::Event)event {
    BOOL written = _state.writeEvent(event);
    if (written && _displayLink == nil)
        [self readEvents: 0];
    return written;
}

- (void)readEvents:(double)time {
    bool dbRedraw = _db->readEvents(_state);
    if (dbRedraw)
        _list.empty().addList(_db->list);
    bool stateRedraw = _state.readEvents();
    if (dbRedraw || stateRedraw) {
        _state.doMouseMove(_list);
        [self.layer setNeedsDisplay];
    }
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
    [self writeEvent:RasterizerState::Event(event.timestamp, RasterizerState::Event::kFlags, event.modifierFlags)];
}
- (void)keyDown:(NSEvent *)event {
    NSLog(@"%d", event.keyCode);
    int keyCode = event.keyCode;
    if ([self writeEvent:RasterizerState::Event(event.timestamp, RasterizerState::Event::kKeyDown, event.keyCode)]) {}
    else if (keyCode == 51) {
        _useCG = !_useCG;
        [self toggleTimer];
        [self initLayer:_useCG];
        _testScene.reset();
        [self updateRasterizerLabel];
        [self.rasterizerLabel setHidden:NO];
    } else if (keyCode == 15) {
        CGFloat native = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
        self.layer.contentsScale = self.layer.contentsScale == native ? 1.0 : native;
        [self.layer setNeedsDisplay];
    } else if (keyCode == 49) {
        [self.rasterizerLabel setHidden:YES];
    } else if (keyCode == 1)
        [self screenGrabToPDF];
    else {
        [super keyDown:event];
    }
}
- (void)keyUp:(NSEvent *)event {
    [self writeEvent:RasterizerState::Event(event.timestamp, RasterizerState::Event::kKeyUp, event.keyCode)];
}

- (void)paste:(id)sender {
	self.pastedString = [[[NSPasteboard generalPasteboard].pasteboardItems objectAtIndex:0] stringForType:NSPasteboardTypeString];
    [self changeFont: nil];
}

- (void)magnifyWithEvent:(NSEvent *)event {
    [self writeEvent:RasterizerState::Event(event.timestamp, RasterizerState::Event::kMagnify, float(1 + event.magnification), 0.f)];
}
- (void)rotateWithEvent:(NSEvent *)event {
    if (!(event.modifierFlags & NSEventModifierFlagShift))
        [self writeEvent:RasterizerState::Event(event.timestamp, RasterizerState::Event::kRotate, float(event.rotation / 10), 0.f)];
}
- (void)scrollWheel:(NSEvent *)event {
    CGFloat inversion = ([event respondsToSelector:@selector(isDirectionInvertedFromDevice)] && [event isDirectionInvertedFromDevice]) ? 1.0f : -1.0f;
    [self writeEvent:RasterizerState::Event(event.timestamp, RasterizerState::Event::kTranslate, float(event.deltaX * inversion), float(-event.deltaY * inversion))];
}
- (void)mouseDown:(NSEvent *)event {
    [self writeEvent:RasterizerState::Event(event.timestamp, RasterizerState::Event::kMouseDown, float(event.locationInWindow.x), float(event.locationInWindow.y))];
}
- (void)mouseDragged:(NSEvent *)event {
    [self writeEvent:RasterizerState::Event(event.timestamp, RasterizerState::Event::kDragged, float(event.deltaX), float(-event.deltaY))];
}
- (void)mouseMoved:(NSEvent *)event {
    [self writeEvent:RasterizerState::Event(event.timestamp, RasterizerState::Event::kMouseMove, float(event.locationInWindow.x), float(event.locationInWindow.y))];
}
- (void)mouseUp:(NSEvent *)event {
    [self writeEvent:RasterizerState::Event(event.timestamp, RasterizerState::Event::kMouseUp, float(event.locationInWindow.x), float(event.locationInWindow.y))];
}

#pragma mark - LayerDelegate

- (void)writeBuffer:(Ra::Buffer *)buffer forLayer:(CALayer *)layer {
    _state.update(self.layer.contentsScale, self.bounds.size.width, self.bounds.size.height, _list.bounds);
    buffer->clearColor = _svgData && _state.outlineWidth == 0.f ? Ra::Colorant(0xCC, 0xCC, 0xCC, 0xCC) : Ra::Colorant(0xFF, 0xFF, 0xFF, 0xFF);
    CGColorSpaceRef srcSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    _testScene.converter.set(srcSpace, self.window.colorSpace.CGColorSpace);
    RaCG::drawTestScene(_testScene, _list, _state, buffer);
    CGColorSpaceRelease(srcSpace);
}

#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    _state.update(self.layer.contentsScale, self.bounds.size.width, self.bounds.size.height, _list.bounds);
    Ra::Colorant color = _svgData && _state.outlineWidth == 0.f ? Ra::Colorant(0xCC, 0xCC, 0xCC, 0xCC) : Ra::Colorant(0xFF, 0xFF, 0xFF, 0xFF);
    memset_pattern4(CGBitmapContextGetData(ctx), & color.src0, CGBitmapContextGetBytesPerRow(ctx) * CGBitmapContextGetHeight(ctx));
    CGContextConcatCTM(ctx, RaCG::CGFromTransform(_state.ctm));
    RaCG::drawScenes(_list, _state.view, _state.device, _state.outlineWidth, ctx);
}

#pragma mark - Screen grab to PDF

- (void)screenGrabToPDF {
    NSArray *downloads = [NSFileManager.defaultManager URLsForDirectory: NSDownloadsDirectory inDomains:NSUserDomainMask];
    NSURL *fileURL = [downloads.firstObject URLByAppendingPathComponent:@"test.pdf"];
    CGRect mediaBox = self.bounds;
    CGContextRef ctx = CGPDFContextCreateWithURL((__bridge CFURLRef)fileURL, & mediaBox, NULL);
    CGPDFContextBeginPage(ctx, NULL);
    _state.update(1.0, self.bounds.size.width, self.bounds.size.height, _list.bounds);
    CGContextConcatCTM(ctx, RaCG::CGFromTransform(_state.ctm));
    RaCG::drawScenes(_list, _state.view, _state.device, _state.outlineWidth, ctx);
    CGPDFContextEndPage(ctx);
    CGPDFContextClose(ctx);
}

#pragma mark - Properies

- (void)setDbURL:(NSURL *)dbURL {
    if ((_dbURL = dbURL)) {
        _db->open(dbURL.path.UTF8String);
        RaCG::writeFontsTable(*(_db.ref));
        [self changeFont:nil];
    }
}

- (void)setSvgData:(NSData *)svgData {
    if ((_svgData = svgData))
        [self changeFont:nil];
}
@end
