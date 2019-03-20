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
#import "MetalLayer.h"

@interface RasterizerView () <CALayerDelegate, LayerDelegate>

@property(nonatomic) VGAffineTransform *transform;
@property(nonatomic) CVDisplayLinkRef displayLink;
- (void)timerFired:(double)time;

@property(nonatomic) RasterizerCoreGraphics::CGTestScene testScene;
@property(nonatomic) RasterizerScene::Scene textScene;
@property(nonatomic) BOOL useClip;
@property(nonatomic) BOOL useCPU;
@property(nonatomic) BOOL useOutline;
@property(nonatomic) BOOL showPaths;
@property(nonatomic) size_t shapesCount;
@property(nonatomic) NSFont *font;
@property(nonatomic) NSString *pastedString;
@property(nonatomic) CGPoint mouse;

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
    if ([self readEvents:_testScene.events])
        [self redraw];
    _testScene.events.resize(0);
}

- (void)toggleTimer {
    if (_displayLink)
        [self stopTimer];
    else
        [self resetTimer];
    [self redraw];
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
    if (!useCPU)
        [self setLayer:[MetalLayer layer]];
    else
        [self setLayer:[CALayer layer]];
    self.layer.contentsScale = scale;
    self.layer.bounds = self.bounds;
    self.layer.opaque = YES;
    self.layer.needsDisplayOnBoundsChange = YES;
    self.layer.actions = @{ @"onOrderIn": [NSNull null], @"onOrderOut": [NSNull null], @"sublayers": [NSNull null], @"contents": [NSNull null], @"backgroundColor": [NSNull null], @"bounds": [NSNull null] };
    if (!useCPU) {
        MetalLayer *layer = (MetalLayer *)self.layer;
        layer.layerDelegate = self;
    } else {
        self.layer.contentsFormat = kCAContentsFormatRGBA8Uint;
        self.layer.delegate = self;
        self.layer.magnificationFilter = kCAFilterNearest;
    }
}

- (void)updateRasterizerLabel {
    [self.rasterizerLabel setHidden:YES];
    self.rasterizerLabel.stringValue = _testScene.rasterizerType == RasterizerCoreGraphics::CGTestScene::kRasterizerMT ? @"Rasterizer (mt)" : _testScene.rasterizerType == RasterizerCoreGraphics::CGTestScene::kRasterizer ?  @"Rasterizer" : @"Core Graphics";
}

#pragma mark - RasterizerEvent

- (BOOL)readEvents:(std::vector<RasterizerEvent::Event>&)events {
    BOOL redraw = NO;
    for (RasterizerEvent::Event& e : events) {
        switch(e.type) {
            case RasterizerEvent::Event::kMouseMove:
                self.mouse = CGPointMake(e.x, e.y);
                if (self.showPaths)
                    redraw = YES;
                break;
            case RasterizerEvent::Event::kMouseUp:
            case RasterizerEvent::Event::kMouseDown:
            case RasterizerEvent::Event::kKeyDown:
            case RasterizerEvent::Event::kKeyUp:
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
    return redraw;
}
- (void)writeEvent:(RasterizerEvent::Event)event {
    _testScene.events.emplace_back(event);
    
    if (_displayLink == nil) {
        if ([self readEvents:_testScene.events])
            [self redraw];
        _testScene.events.resize(0);
    }
}

#pragma mark - NSResponder

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    [self updateRasterizerLabel];
    self.window.acceptsMouseMovedEvents = YES;
    [self toggleTimer];
    [self redraw];
    return YES;
}

- (void)keyDown:(NSEvent *)event {
    NSLog(@"%d", event.keyCode);
    int keyCode = event.keyCode;
    //[self writeEvent:RasterizerEvent::Event(RasterizerEvent::Event::kKeyDown, keyCode)];
    
    if (keyCode == 8) {
        _useClip = !_useClip;
        [self redraw];
    } else if (keyCode == 46) {
        _useCPU = !_useCPU;
        [self toggleTimer];
        [self initLayer:_useCPU];
        _testScene.reset();
        [self redraw];
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
        self.CTM = CGAffineTransformIdentity;
        [self redraw];
    } else {
        [super keyDown:event];
    }
}

- (void)paste:(id)sender {
	NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
	NSArray<NSPasteboardItem *> *items = pasteboard.pasteboardItems;
	self.pastedString = [[items objectAtIndex:0] stringForType:NSPasteboardTypeString];
    RasterizerCoreGraphics::writeGlyphs(self.font.fontName, self.font.pointSize, self.pastedString, self.bounds, _testScene);
	[self redraw];
}

- (void)magnifyWithEvent:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(RasterizerEvent::Event::kMagnify, float(1 + event.magnification), 0.f)];
}
- (void)rotateWithEvent:(NSEvent *)event {
    if (!(event.modifierFlags & NSEventModifierFlagShift))
        [self writeEvent:RasterizerEvent::Event(RasterizerEvent::Event::kRotate, float(event.rotation / 10), 0.f)];
}
- (void)scrollWheel:(NSEvent *)event {
    BOOL isInverted = ([event respondsToSelector:@selector(isDirectionInvertedFromDevice)] && [event isDirectionInvertedFromDevice]);
    CGFloat inversion = isInverted ? 1.0f : -1.0f;
    [self writeEvent:RasterizerEvent::Event(RasterizerEvent::Event::kTranslate, float(event.deltaX * inversion), float(-event.deltaY * inversion))];
}

- (void)mouseDown:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(RasterizerEvent::Event::kMouseDown, float(event.locationInWindow.x), float(event.locationInWindow.y))];
}
- (void)mouseDragged:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(RasterizerEvent::Event::kTranslate, float(event.deltaX), float(-event.deltaY))];
}
- (void)mouseMoved:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(RasterizerEvent::Event::kMouseMove, float(event.locationInWindow.x), float(event.locationInWindow.y))];
}
- (void)mouseUp:(NSEvent *)event {
    [self writeEvent:RasterizerEvent::Event(RasterizerEvent::Event::kMouseUp, float(event.locationInWindow.x), float(event.locationInWindow.y))];
}

#pragma mark - Properties

- (CGAffineTransform)CTM {
    return self.transform.affineTransform;
}

- (void)setCTM:(CGAffineTransform)CTM {
    self.transform = [[VGAffineTransform alloc] initWithTransform:CTM];
    [self redraw];
}
#pragma mark - LayerDelegate

- (void)writeBuffer:(Rasterizer::Buffer *)buffer forLayer:(CALayer *)layer {
    if (_testScene.rasterizerType == RasterizerCoreGraphics::CGTestScene::kCoreGraphics)
        return;
        
    float s = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
	Rasterizer::Transform view(self.CTM.a, self.CTM.b, self.CTM.c, self.CTM.d, self.CTM.tx, self.CTM.ty);
    Rasterizer::Transform contentsScale(s, 0.f, 0.f, s, 0.f, 0.f);
    Rasterizer::Transform ctm = contentsScale.concat(view);
    Rasterizer::Bitmap bitmap(nullptr, ceilf(s * w), ceilf(h * s), 0, 0);
    Rasterizer::Path clipPath;
    uint8_t svg[4] = { 0xCC, 0xCC, 0xCC, 0xCC }, font[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    buffer->clearColor = Rasterizer::Colorant(_svgData && !_useOutline ? svg : font);
    RasterizerCoreGraphics::drawTestScene(_testScene, ctm, _useClip ? &clipPath : nullptr, _useOutline, nullptr, self.window.colorSpace.CGColorSpace, bitmap, buffer,
                                          float(_showPaths ? _mouse.x * self.layer.contentsScale : FLT_MAX),
                                          float(_showPaths ? _mouse.y * self.layer.contentsScale : FLT_MAX));
}

#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    CGContextConcatCTM(ctx, self.CTM);
    CGAffineTransform CTM = CGContextGetCTM(ctx);
    Rasterizer::Transform ctm(CTM.a, CTM.b, CTM.c, CTM.d, CTM.tx, CTM.ty);
    Rasterizer::Bitmap bitmap(CGBitmapContextGetData(ctx), CGBitmapContextGetWidth(ctx), CGBitmapContextGetHeight(ctx), CGBitmapContextGetBytesPerRow(ctx), CGBitmapContextGetBitsPerPixel(ctx));
    uint8_t svg[4] = { 0xCC, 0xCC, 0xCC, 0xCC }, font[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    bitmap.clear(_svgData && !_useOutline ? svg : font);
    Rasterizer::Path clipPath;
    RasterizerCoreGraphics::drawTestScene(_testScene, ctm, _useClip ? &clipPath : nullptr, _useOutline, ctx, CGBitmapContextGetColorSpace(ctx), bitmap, nullptr,
                                          float(_showPaths ? _mouse.x * self.layer.contentsScale : FLT_MAX),
                                          float(_showPaths ? _mouse.y * self.layer.contentsScale : FLT_MAX));
}

- (void)setSvgData:(NSData *)svgData {
    _svgData = svgData;
    if (_svgData) {
        _testScene.scene.empty();
        _testScene.cgscene.empty();
        RasterizerSVG::writeScene(_svgData.bytes, _svgData.length, _testScene.scene);
    }
    [self redraw];
}
@end
