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
#import "RasterizerCG.hpp"
#import "RasterizerSQL.hpp"
#import "RasterizerSVG.hpp"
#import "RasterizerTrueType.hpp"
#import "RasterizerTest.hpp"
#import "MetalLayer.h"

@interface RasterizerView () <CALayerDelegate, LayerDelegate>

@property(nonatomic) VGAffineTransform *transform;
@property(nonatomic) CVDisplayLinkRef displayLink;
@property(nonatomic) RasterizerCG::CGTestContext testScene;
@property(nonatomic) RasterizerEvent::State state;
@property(nonatomic) Rasterizer::SceneList list;
@property(nonatomic) BOOL useCPU;
@property(nonatomic) NSFont *font;
@property(nonatomic) NSString *pastedString;
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
    RasterizerCG::writeGlyphs(self.font.fontName, self.font.pointSize, nil, self.bounds, _list.firstScene());
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
    [self readEvents:_state forTime:time withScenes:_list];
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
    if ([_dbURL isFileURL]) {
        NSData *data = [NSData dataWithContentsOfURL:RasterizerCG::fontURL(self.font.fontName)];
        RasterizerTrueType::Font font;
        if (font.set(data.bytes, self.font.fontName.UTF8String) != 0) {
            RasterizerSQL::DB db;
            db.open(_dbURL.path.UTF8String);
            db.writeQuery(font, self.font.pointSize, 20, RasterizerCG::boundsFromCGRect(self.bounds), "select * from tbl1", _list.firstScene());
        }
    } else
        RasterizerCG::writeGlyphs(self.font.fontName, self.font.pointSize, self.pastedString, self.bounds, _list.firstScene());
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
}

- (void)updateRasterizerLabel {
    self.rasterizerLabel.stringValue = _testScene.rasterizerType == RasterizerCG::CGTestContext::kRasterizerMT ? @"Rasterizer (GPU)" : _testScene.rasterizerType == RasterizerCG::CGTestContext::kRasterizer ?  @"Rasterizer" : @"Core Graphics";
}

#pragma mark - RasterizerEvent

- (void)readEvents:(RasterizerEvent::State&)state forTime:(double)time withScenes:(Rasterizer::SceneList&)list {
    BOOL redraw = NO;
    for (RasterizerEvent::Event& e : state.events) {
        switch(e.type) {
            case RasterizerEvent::Event::kMouseMove:
                state.x = e.x, state.y = e.y;
                redraw = state.mouseMove;
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
                redraw = YES;
                if (e.keyCode == 8)
                    state.useClip = !state.useClip;
                else if (e.keyCode == 31)
                    state.useOutline = !state.useOutline;
                else if (e.keyCode == 35)
                    state.mouseMove = !state.mouseMove;
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
    [self updateState:state forTime:time withScenes:list];
    state.index = !state.mouseMove ? INT_MAX : RasterizerWinding::pathIndexForPoint(_list, false, state.view, state.bounds, self.layer.contentsScale * state.x, self.layer.contentsScale * state.y);
    if (redraw)
        [self.layer setNeedsDisplay];
    state.events.resize(0);
}
- (void)writeEvent:(RasterizerEvent::Event)event {
    _state.events.emplace_back(event);
    if (_displayLink == nil)
        [self readEvents:_state forTime:0 withScenes:_list];
}
- (void)updateState:(RasterizerEvent::State&)state forTime:(double)time withScenes:(Rasterizer::SceneList&)list {
    state.update(self.layer.contentsScale, self.bounds.size.width, self.bounds.size.height, RasterizerCG::transformFromCG(self.transform.affineTransform));
    list.setClip(state.useClip ? Rasterizer::Bounds(100, 100, 200, 200).unit(state.view) : Rasterizer::Transform::nullclip());
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
    [self writeEvent:RasterizerEvent::Event(event.timestamp, RasterizerEvent::Event::kFlags, event.modifierFlags)];
}
- (void)keyDown:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(event.timestamp, RasterizerEvent::Event::kKeyDown, event.keyCode)];
    
    NSLog(@"%d", event.keyCode);
    int keyCode = event.keyCode;
    if (keyCode == 8 || keyCode == 31 || keyCode == 35) {}
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
    } else if (keyCode == 49) {
        _testScene.rasterizerType = (++_testScene.rasterizerType) % RasterizerCG::CGTestContext::kRasterizerCount;
        [self.rasterizerLabel setHidden:YES];
    } else if (keyCode == 36) {
        self.transform = [VGAffineTransform new];
    } else {
        [super keyDown:event];
    }
}
- (void)keyUp:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(event.timestamp, RasterizerEvent::Event::kKeyUp, event.keyCode)];
}

- (void)paste:(id)sender {
	self.pastedString = [[[NSPasteboard generalPasteboard].pasteboardItems objectAtIndex:0] stringForType:NSPasteboardTypeString];
    RasterizerCG::writeGlyphs(self.font.fontName, self.font.pointSize, self.pastedString, self.bounds, _list.firstScene());
	[self.layer setNeedsDisplay];
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
    if (_testScene.rasterizerType == RasterizerCG::CGTestContext::kCoreGraphics)
        return;
    [self updateState:_state forTime:0 withScenes:_list];
    buffer->clearColor = _svgData && !_state.useOutline ? Rasterizer::Colorant(0xCC, 0xCC, 0xCC, 0xCC) : Rasterizer::Colorant(0xFF, 0xFF, 0xFF, 0xFF);
    RasterizerCG::drawTestScene(_testScene, _list, _state.view, _state.useOutline, nullptr, self.window.colorSpace.CGColorSpace, Rasterizer::Bitmap(nullptr, _state.bounds.ux, _state.bounds.uy, 0, 0), buffer, _state.index);
}

#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    [self updateState:_state forTime:0 withScenes:_list];
    CGContextConcatCTM(ctx, self.transform.affineTransform);
    Rasterizer::Bitmap bitmap(CGBitmapContextGetData(ctx), CGBitmapContextGetWidth(ctx), CGBitmapContextGetHeight(ctx), CGBitmapContextGetBytesPerRow(ctx), CGBitmapContextGetBitsPerPixel(ctx));
    bitmap.clear(_svgData && !_state.useOutline ? Rasterizer::Colorant(0xCC, 0xCC, 0xCC, 0xCC) : Rasterizer::Colorant(0xFF, 0xFF, 0xFF, 0xFF));
    RasterizerCG::drawTestScene(_testScene, _list, RasterizerCG::transformFromCG(CGContextGetCTM(ctx)), _state.useOutline, ctx, CGBitmapContextGetColorSpace(ctx), bitmap, nullptr, _state.index);
}

- (void)setDbURL:(NSURL *)dbURL {
    _dbURL = dbURL;
    if (_dbURL)
        [self changeFont:nil];
}

- (void)setSvgData:(NSData *)svgData {
    _svgData = svgData;
    if (_svgData) {
        Rasterizer::Scene& scene = _list.firstScene();
        RasterizerSVG::writeScene(_svgData.bytes, _svgData.length, scene);
        RasterizerTest::addTestPaths(_list.addScene());
    }
    [self.layer setNeedsDisplay];
}
@end
