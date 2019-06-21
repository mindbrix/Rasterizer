//
//  RasterizerView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#define RASTERIZER_SIMD 1
#import "RasterizerView.h"
#import "RasterizerCG.hpp"
#import "RasterizerDB.hpp"
#import "RasterizerSVG.hpp"
#import "RasterizerFont.hpp"
#import "RasterizerTest.hpp"
#import "MetalLayer.h"

@interface RasterizerView () <CALayerDelegate, LayerDelegate>

@property(nonatomic) CVDisplayLinkRef displayLink;
@property(nonatomic) RasterizerCG::CGTestContext testScene;
@property(nonatomic) RasterizerState state;
@property(nonatomic) Rasterizer::SceneList list;
@property(nonatomic) BOOL useCPU;
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
    [self initLayer:_useCPU];
    self.font = [NSFont fontWithName:@"AppleSymbols" size:14];
    RasterizerCG::writeGlyphs(self.font.fontName, self.font.pointSize, nil, self.bounds, _list.empty().addScene());
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
    if (_state.readEvents(self.layer.contentsScale, self.bounds.size.width, self.bounds.size.height, 0, _list))
        [self.layer setNeedsDisplay];
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
    if ([_dbURL isFileURL]) {
        NSData *data = [NSData dataWithContentsOfURL:RasterizerCG::fontURL(self.font.fontName)];
        RasterizerFont font;
        if (font.set(data.bytes, self.font.fontName.UTF8String)) {
            RasterizerDB db;
            db.open(_dbURL.path.UTF8String);
            RasterizerCG::writeFontsTable(db);
            db.writeTables(font, RasterizerCG::boundsFromCGRect(self.bounds), _list.empty());
        }
    } else
        RasterizerCG::writeGlyphs(self.font.fontName, self.font.pointSize, self.pastedString, self.bounds, _list.empty().addScene());
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
        [self setLayer:[MetalLayer layer]];
        ((MetalLayer *)self.layer).layerDelegate = self;
    }
    self.layer.contentsScale = scale;
    self.layer.bounds = self.bounds;
    self.layer.opaque = YES;
    self.layer.needsDisplayOnBoundsChange = YES;
    self.layer.actions = @{ @"onOrderIn": [NSNull null], @"onOrderOut": [NSNull null], @"sublayers": [NSNull null], @"contents": [NSNull null], @"backgroundColor": [NSNull null], @"bounds": [NSNull null] };
    [self.layer setNeedsDisplay];
    _state.update(self.layer.contentsScale, self.bounds.size.width, self.bounds.size.height);
}

- (void)updateRasterizerLabel {
    self.rasterizerLabel.stringValue = _testScene.rasterizerType == RasterizerCG::CGTestContext::kRasterizerMT ? @"Rasterizer (GPU)" : _testScene.rasterizerType == RasterizerCG::CGTestContext::kRasterizer ?  @"Rasterizer" : @"Core Graphics";
}

#pragma mark - AppState

- (BOOL)writeEvent:(RasterizerState::Event)event {
    BOOL written = _state.writeEvent(event);
    if (written && _displayLink == nil)
        if (_state.readEvents(self.layer.contentsScale, self.bounds.size.width, self.bounds.size.height, 0, _list))
            [self.layer setNeedsDisplay];
    return written;
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
        _useCPU = !_useCPU;
        [self toggleTimer];
        [self initLayer:_useCPU];
        _testScene.rasterizerType = _useCPU ? RasterizerCG::CGTestContext::RasterizerType::kCoreGraphics : RasterizerCG::CGTestContext::RasterizerType::kRasterizerMT;
        [self updateRasterizerLabel];
        [self.rasterizerLabel setHidden:NO];
    } else if (keyCode == 46) {
        _useCPU = !_useCPU;
        [self toggleTimer];
        [self initLayer:_useCPU];
        _testScene.reset();
        [self.rasterizerLabel setHidden:YES];
    } else if (keyCode == 15) {
        CGFloat native = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
        self.layer.contentsScale = self.layer.contentsScale == native ? 1.0 : native;
        [self.layer setNeedsDisplay];
        _state.update(self.layer.contentsScale, self.bounds.size.width, self.bounds.size.height);
    } else if (keyCode == 49) {
        _testScene.rasterizerType = (++_testScene.rasterizerType) % RasterizerCG::CGTestContext::kRasterizerCount;
        [self.rasterizerLabel setHidden:YES];
    } else {
        [super keyDown:event];
    }
}
- (void)keyUp:(NSEvent *)event {
    [self writeEvent:RasterizerState::Event(event.timestamp, RasterizerState::Event::kKeyUp, event.keyCode)];
}

- (void)paste:(id)sender {
	self.pastedString = [[[NSPasteboard generalPasteboard].pasteboardItems objectAtIndex:0] stringForType:NSPasteboardTypeString];
    RasterizerCG::writeGlyphs(self.font.fontName, self.font.pointSize, self.pastedString, self.bounds, _list.empty().addScene());
	[self.layer setNeedsDisplay];
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
    [self writeEvent:RasterizerState::Event(event.timestamp, RasterizerState::Event::kTranslate, float(event.deltaX), float(-event.deltaY))];
}
- (void)mouseMoved:(NSEvent *)event {
    [self writeEvent:RasterizerState::Event(event.timestamp, RasterizerState::Event::kMouseMove, float(event.locationInWindow.x), float(event.locationInWindow.y))];
}
- (void)mouseUp:(NSEvent *)event {
    [self writeEvent:RasterizerState::Event(event.timestamp, RasterizerState::Event::kMouseUp, float(event.locationInWindow.x), float(event.locationInWindow.y))];
}

#pragma mark - LayerDelegate

- (void)writeBuffer:(Rasterizer::Buffer *)buffer forLayer:(CALayer *)layer {
    if (_testScene.rasterizerType == RasterizerCG::CGTestContext::kCoreGraphics)
        return;
    _state.update(self.layer.contentsScale, self.bounds.size.width, self.bounds.size.height);
    buffer->clearColor = _svgData && !_state.useOutline ? Rasterizer::Colorant(0xCC, 0xCC, 0xCC, 0xCC) : Rasterizer::Colorant(0xFF, 0xFF, 0xFF, 0xFF);
    RasterizerCG::drawTestScene(_testScene, _list, _state.view, _state.useOutline, nullptr, self.window.colorSpace.CGColorSpace, Rasterizer::Bitmap(nullptr, _state.device.ux, _state.device.uy, 0, 0), buffer, _state.index);
}

#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    _state.update(self.layer.contentsScale, self.bounds.size.width, self.bounds.size.height);
    CGContextConcatCTM(ctx, RasterizerCG::CGFromTransform(_state.ctm));
    Rasterizer::Bitmap bitmap(CGBitmapContextGetData(ctx), CGBitmapContextGetWidth(ctx), CGBitmapContextGetHeight(ctx), CGBitmapContextGetBytesPerRow(ctx), CGBitmapContextGetBitsPerPixel(ctx));
    bitmap.clear(_svgData && !_state.useOutline ? Rasterizer::Colorant(0xCC, 0xCC, 0xCC, 0xCC) : Rasterizer::Colorant(0xFF, 0xFF, 0xFF, 0xFF));
    RasterizerCG::drawTestScene(_testScene, _list, RasterizerCG::transformFromCG(CGContextGetCTM(ctx)), _state.useOutline, ctx, CGBitmapContextGetColorSpace(ctx), bitmap, nullptr, _state.index);
}

- (void)setDbURL:(NSURL *)dbURL {
    if ((_dbURL = dbURL))
        [self changeFont:nil];
}

- (void)setSvgData:(NSData *)svgData {
    if ((_svgData = svgData)) {
        Rasterizer::Scene& scene = _list.empty().addScene();
        RasterizerSVG::writeScene(_svgData.bytes, _svgData.length, scene);
        RasterizerTest::addTestPaths(_list.addScene());
    }
    [self.layer setNeedsDisplay];
}
@end
