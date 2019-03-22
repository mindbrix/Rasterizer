//
//  RasterizerView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#define RASTERIZER_SIMD 1
#import "RasterizerView.h"
#import "VGAffineTransform.h"
#import "RasterizerCoreGraphics.hpp"
#import "RasterizerSVG.hpp"
#import "RasterizerTrueType.hpp"
#import "RasterizerTest.hpp"
#import "MetalLayer.h"

@interface RasterizerView () <CALayerDelegate, LayerDelegate>

@property(nonatomic) VGAffineTransform *transform;
@property(nonatomic) CVDisplayLinkRef displayLink;
@property(nonatomic) RasterizerCoreGraphics::CGTestScene testScene;
@property(nonatomic) Rasterizer::Scene textScene;
@property(nonatomic) BOOL useClip;
@property(nonatomic) BOOL useCPU;
@property(nonatomic) BOOL useOutline;
@property(nonatomic) BOOL showPaths;
@property(nonatomic) NSFont *font;
@property(nonatomic) NSString *pastedString;
@property(nonatomic) CGPoint mouse;
- (void)timerFired:(double)time;

@end

static CVReturn OnDisplayLinkFrame(CVDisplayLinkRef displayLink,
                                   const CVTimeStamp *now,
                                   const CVTimeStamp *outputTime,
                                   CVOptionFlags flagsIn,
                                   CVOptionFlags *flagsOut,
                                   void *displayLinkContext) {
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
    self.transform = [VGAffineTransform new];
    RasterizerCoreGraphics::writeGlyphs(self.font.fontName, self.font.pointSize, nil, self.bounds, _testScene);
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
    [self readEvents:_testScene.state forTime:time];
}

- (void)toggleTimer {
    if (_displayLink)
        [self stopTimer];
    else
        [self resetTimer];
}

#pragma mark - NSFontManager

- (void)changeFont:(id)sender {
    self.font = [[NSFontManager sharedFontManager] convertFont:[NSFont fontWithName:@"Times" size:14]];
    RasterizerCoreGraphics::writeGlyphs(self.font.fontName, self.font.pointSize, self.pastedString, self.bounds, _testScene);
    [self redraw];
}

#pragma mark - Drawing

- (void)redraw {
    [self.layer setNeedsDisplay];
}

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
}

- (void)updateRasterizerLabel {
    [self.rasterizerLabel setHidden:YES];
    self.rasterizerLabel.stringValue = _testScene.rasterizerType == RasterizerCoreGraphics::CGTestScene::kRasterizerMT ? @"Rasterizer (mt)" : _testScene.rasterizerType == RasterizerCoreGraphics::CGTestScene::kRasterizer ?  @"Rasterizer" : @"Core Graphics";
}

#pragma mark - RasterizerEvent

- (void)readEvents:(RasterizerEvent::State&)state forTime:(double)time {
    BOOL redraw = NO;
    for (RasterizerEvent::Event& e : state.events) {
        switch(e.type) {
            case RasterizerEvent::Event::kMouseMove:
                self.mouse = CGPointMake(e.x, e.y);
                redraw = self.showPaths;
                break;
            case RasterizerEvent::Event::kMouseUp:
                state.mouseDown = false;
                break;
            case RasterizerEvent::Event::kMouseDown:
                state.mouseDown = true;
                break;
            case RasterizerEvent::Event::kFlags:
                state.flags = e.flags;
                break;
            case RasterizerEvent::Event::kKeyDown:
                state.keyDown = true, state.keyCode = e.keyCode;
                break;
            case RasterizerEvent::Event::kKeyUp:
                state.keyDown = true, state.keyCode = e.keyCode;
                break;
            case RasterizerEvent::Event::kMagnify:
                [self.transform scaleBy:e.x bounds:self.bounds];
                redraw = YES;
                break;
            case RasterizerEvent::Event::kRotate:
                [self.transform rotateBy:e.x bounds:self.bounds];
                redraw = YES;
                break;
            case RasterizerEvent::Event::kTranslate:
                [self.transform translateByX:e.x andY:e.y];
                redraw = YES;
                break;
            case RasterizerEvent::Event::kNull:
                assert(0);
        }
    }
    state.events.resize(0);
    if (redraw)
        [self redraw];
}
- (void)writeEvent:(RasterizerEvent::Event)event {
    _testScene.state.events.emplace_back(event);
    if (_displayLink == nil)
        [self readEvents:_testScene.state forTime:0];
}

#pragma mark - NSResponder

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    [self updateRasterizerLabel];
    self.window.acceptsMouseMovedEvents = YES;
    [self toggleTimer];
    return YES;
}

- (void)flagsChanged:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(event.timestamp, RasterizerEvent::Event::kFlags, event.modifierFlags)];
}
- (void)keyDown:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(event.timestamp, RasterizerEvent::Event::kKeyDown, event.keyCode)];
    
    NSLog(@"%d", event.keyCode);
    int keyCode = event.keyCode;
    if (keyCode == 8) {
        _useClip = !_useClip;
        [self redraw];
    } else if (keyCode == 46) {
        _useCPU = !_useCPU;
        [self toggleTimer];
        [self initLayer:_useCPU];
        _testScene.reset();
    } else if (keyCode == 15) {
        CGFloat native = [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
        self.layer.contentsScale = self.layer.contentsScale == native ? 1.0 : native;
        [self redraw];
    } else if (keyCode == 31) {
        _useOutline = !_useOutline;
        [self redraw];
    } else if (keyCode == 35) {
        _showPaths = !_showPaths;
        [self redraw];
    } else if (keyCode == 49) {
        _testScene.rasterizerType = (++_testScene.rasterizerType) % RasterizerCoreGraphics::CGTestScene::kRasterizerCount;
        [self updateRasterizerLabel];
        [self redraw];
    } else if (keyCode == 36) {
        self.transform = [VGAffineTransform new];
        [self redraw];
    } else {
        [super keyDown:event];
    }
}
- (void)keyUp:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(event.timestamp, RasterizerEvent::Event::kKeyUp, event.keyCode)];
}

- (void)paste:(id)sender {
	self.pastedString = [[[NSPasteboard generalPasteboard].pasteboardItems objectAtIndex:0] stringForType:NSPasteboardTypeString];
    RasterizerCoreGraphics::writeGlyphs(self.font.fontName, self.font.pointSize, self.pastedString, self.bounds, _testScene);
	[self redraw];
}

- (void)magnifyWithEvent:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(event.timestamp, RasterizerEvent::Event::kMagnify, float(1 + event.magnification), 0.f)];
}
- (void)rotateWithEvent:(NSEvent *)event {
    if (!(event.modifierFlags & NSEventModifierFlagShift))
        [self writeEvent:RasterizerEvent::Event(event.timestamp, RasterizerEvent::Event::kRotate, float(event.rotation / 10), 0.f)];
}
- (void)scrollWheel:(NSEvent *)event {
    BOOL isInverted = ([event respondsToSelector:@selector(isDirectionInvertedFromDevice)] && [event isDirectionInvertedFromDevice]);
    CGFloat inversion = isInverted ? 1.0f : -1.0f;
    [self writeEvent:RasterizerEvent::Event(event.timestamp, RasterizerEvent::Event::kTranslate, float(event.deltaX * inversion), float(-event.deltaY * inversion))];
}

- (void)mouseDown:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(event.timestamp, RasterizerEvent::Event::kMouseDown, float(event.locationInWindow.x), float(event.locationInWindow.y))];
}
- (void)mouseDragged:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(event.timestamp, RasterizerEvent::Event::kTranslate, float(event.deltaX), float(-event.deltaY))];
}
- (void)mouseMoved:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(event.timestamp, RasterizerEvent::Event::kMouseMove, float(event.locationInWindow.x), float(event.locationInWindow.y))];
}
- (void)mouseUp:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(event.timestamp, RasterizerEvent::Event::kMouseUp, float(event.locationInWindow.x), float(event.locationInWindow.y))];
}

#pragma mark - LayerDelegate

- (void)writeBuffer:(Rasterizer::Buffer *)buffer forLayer:(CALayer *)layer {
    if (_testScene.rasterizerType == RasterizerCoreGraphics::CGTestScene::kCoreGraphics)
        return;
        
    float s = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
    CGAffineTransform CTM = self.transform.affineTransform;
    Rasterizer::Transform view = RasterizerCoreGraphics::transformFromCG(CTM);
    Rasterizer::Transform contentsScale(s, 0.f, 0.f, s, 0.f, 0.f);
    Rasterizer::Transform ctm = contentsScale.concat(view);
    _testScene.scenes[0].ref->clip = _useClip ? Rasterizer::Bounds(100, 100, 200, 200).unit(ctm) : Rasterizer::Transform::nullclip();
    Rasterizer::Bitmap bitmap(nullptr, ceilf(s * w), ceilf(h * s), 0, 0);
    uint8_t svg[4] = { 0xCC, 0xCC, 0xCC, 0xCC }, font[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    buffer->clearColor = Rasterizer::Colorant(_svgData && !_useOutline ? svg : font);
    RasterizerCoreGraphics::drawTestScene(_testScene, ctm, _useOutline, nullptr, self.window.colorSpace.CGColorSpace, bitmap, buffer,
                                          float(_showPaths ? _mouse.x * self.layer.contentsScale : FLT_MAX),
                                          float(_showPaths ? _mouse.y * self.layer.contentsScale : FLT_MAX));
}

#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    CGContextConcatCTM(ctx, self.transform.affineTransform);
    Rasterizer::Transform ctm = RasterizerCoreGraphics::transformFromCG(CGContextGetCTM(ctx));
    Rasterizer::Bitmap bitmap(CGBitmapContextGetData(ctx), CGBitmapContextGetWidth(ctx), CGBitmapContextGetHeight(ctx), CGBitmapContextGetBytesPerRow(ctx), CGBitmapContextGetBitsPerPixel(ctx));
    uint8_t svg[4] = { 0xCC, 0xCC, 0xCC, 0xCC }, font[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    bitmap.clear(_svgData && !_useOutline ? svg : font);
    _testScene.scenes[0].ref->clip = _useClip ? Rasterizer::Bounds(100, 100, 200, 200).unit(ctm) : Rasterizer::Transform::nullclip();
    RasterizerCoreGraphics::drawTestScene(_testScene, ctm, _useOutline, ctx, CGBitmapContextGetColorSpace(ctx), bitmap, nullptr,
                                          float(_showPaths ? _mouse.x * self.layer.contentsScale : FLT_MAX),
                                          float(_showPaths ? _mouse.y * self.layer.contentsScale : FLT_MAX));
}

- (void)setSvgData:(NSData *)svgData {
    _svgData = svgData;
    if (_svgData) {
        Rasterizer::Scene& scene = *_testScene.scenes[0].ref;
        scene.empty(), _testScene.cgscene.empty();
        RasterizerSVG::writeScene(_svgData.bytes, _svgData.length, scene);
        RasterizerTest::addTestPaths(scene);
    }
    [self redraw];
}
@end
