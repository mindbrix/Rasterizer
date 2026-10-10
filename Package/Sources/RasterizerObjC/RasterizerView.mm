//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//  SPDX-License-Identifier: MIT
//

#import "RasterizerView.h"
#import "RasterizerLayer.h"
#import "RasterizerCG.hpp"
#import "RasterizerAPI+Internal.h"


@interface RasterizerView () <CALayerDelegate, LayerDelegate>

@property(nonatomic) CADisplayLink *displayLink;
@property(nonatomic) RasterizerRenderer renderer;

@end

@implementation RasterizerView

#if TARGET_OS_IPHONE
+ (Class)layerClass {
    return [RasterizerLayer class];
}
#endif

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    [self rasterizerInit];
    return self;
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    [self rasterizerInit];
    return self;
}

- (void)rasterizerInit {
    self.useCG = false;
}

// The display link retains its target, so it runs only while the view is in a window: leaving the window breaks the cycle, & a view
// added to a window again restarts it. On macOS, a new window may be on another display, which a new link follows
#if TARGET_OS_OSX
- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
#elif TARGET_OS_IPHONE
- (void)didMoveToWindow {
    [super didMoveToWindow];
#endif
    [self stopTimer];
    if (self.window)
        [self startTimer];
}

- (void)dealloc {
    [self stopTimer];
}


#pragma mark - Timer

// The display link calls back on the main thread, in step with the display, so frames are drawn in its callback. On macOS, a view's
// display link follows the display the view is on
- (void)startTimer {
#if TARGET_OS_OSX
    _displayLink = [self displayLinkWithTarget:self selector:@selector(onDisplayLink:)];
#elif TARGET_OS_IPHONE
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(onDisplayLink:)];
    // ProMotion displays run up to 120Hz, but a display link defaults to 60Hz. iPhone apps also need CADisableMinimumFrameDurationOnPhone
    if (@available(iOS 15.0, *)) {
        float fps = UIScreen.mainScreen.maximumFramesPerSecond;
        _displayLink.preferredFrameRateRange = CAFrameRateRangeMake(fminf(60, fps), fps, fps);
    }
#endif
    [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)stopTimer {
    [_displayLink invalidate], _displayLink = nil;
}

- (void)onDisplayLink:(CADisplayLink *)link {
    if ([self.listDelegate respondsToSelector:@selector(shouldRedrawAtTime:scale:width:height:)]) {
        double scale = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
        if ([self.listDelegate shouldRedrawAtTime:CACurrentMediaTime()
                                            scale:scale
                                            width:w
                                           height:h]) {
            [self.layer setNeedsDisplay];
        }
    }
}

#pragma mark - LayerDelegate

- (void)writeBuffer:(RenderBuffer *)buffer forLayer:(CAMetalLayer *)layer {
    if ([self.listDelegate respondsToSelector:@selector(getListAtTime:scale:width:height:)]) {
        double scale = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
        RASceneList *list = [self.listDelegate getListAtTime:CACurrentMediaTime()
                                                       scale:scale
                                                       width:w
                                                      height:h];
        _renderer.renderList(list.list, scale, w, h, buffer);
    }
}

#pragma mark - CALayerDelegate

// CG renders into a DeviceRGB bitmap, the layer's contents, as the Metal layer's colorspace is DeviceRGB, so both draw & blend the
// same device values. The context a layer draws in is color matched to the window's color space, e.g. Display P3
- (void)displayLayer:(CALayer *)layer {
    if ([self.listDelegate respondsToSelector:@selector(getListAtTime:scale:width:height:)]) {
        double scale = layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
        size_t pw = ceil(w * scale), ph = ceil(h * scale);
        if (pw == 0 || ph == 0)
            return;
        RASceneList *list = [self.listDelegate getListAtTime:CACurrentMediaTime()
                                                       scale:scale
                                                       width:w
                                                      height:h];
        CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(nullptr, pw, ph, 8, pw * 4, rgb, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        CGContextScaleCTM(ctx, scale, scale);
        RaCG::renderListWithClear(list.list, scale, w, h, ctx);
        CGImageRef image = CGBitmapContextCreateImage(ctx);
        layer.contents = (__bridge id)image;
        CGImageRelease(image), CGContextRelease(ctx), CGColorSpaceRelease(rgb);
    }
}


#pragma mark - Properies

- (void)setUseCG:(bool)useCG {
    _useCG = useCG;
#if TARGET_OS_IPHONE
    if ([self.layer isKindOfClass:[RasterizerLayer class]]) {
        ((RasterizerLayer *)self.layer).layerDelegate = self;
        CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
        ((RasterizerLayer *)self.layer).colorspace = rgb;
        CGColorSpaceRelease(rgb);
    }
    self.layer.contentsScale = UIScreen.mainScreen.scale;
#elif TARGET_OS_OSX
    [self setWantsLayer:YES];
    CGFloat scale = self.layer.contentsScale ?: [self convertSizeToBacking:NSMakeSize(1.f, 1.f)].width;
    if (self.useCG) {
        self.layer = [CALayer layer];
        self.layer.delegate = self;
        self.layer.magnificationFilter = kCAFilterNearest;
    } else {
        self.layer = [RasterizerLayer layer];
        ((RasterizerLayer *)self.layer).layerDelegate = self;
        CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
        ((RasterizerLayer *)self.layer).colorspace = rgb;
        CGColorSpaceRelease(rgb);
    }
    _renderer.reset();
    self.layer.contentsScale = scale;
#endif
    self.layer.bounds = self.bounds;
    self.layer.opaque = YES;
    self.layer.needsDisplayOnBoundsChange = YES;
    self.layer.actions = @{ @"onOrderIn": [NSNull null], @"onOrderOut": [NSNull null], @"sublayers": [NSNull null], @"contents": [NSNull null], @"backgroundColor": [NSNull null], @"bounds": [NSNull null] };
    [self.layer setNeedsDisplay];
}

@end
