//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//
//  This software is provided 'as-is', without any express or implied
//  warranty. In no event will the authors be held liable for any damages
//  arising from the use of this software.
//
//  Permission is granted to anyone to use this software for personal use
//  (for a commercial licence please contact the author), and to alter it and
//  redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
//

#import "RasterizerView.h"
#import "RasterizerLayer.h"
#import "RasterizerCG.hpp"
#import "RasterizerAPI+Internal.h"
#if TARGET_OS_OSX
#import <IOSurface/IOSurface.h>
#import <memory>
#import <vector>

// A DeviceRGB IOSurface, tagged so CA color matches it as it does a DeviceRGB image, with a context drawing into it
struct CGSurface {
    CGSurface(size_t w, size_t h, CGColorSpaceRef rgb) {
        NSDictionary *props = @{ (id)kIOSurfaceWidth: @(w), (id)kIOSurfaceHeight: @(h), (id)kIOSurfaceBytesPerElement: @4,
                                 (id)kIOSurfacePixelFormat: @((uint32_t)'BGRA') };
        if ((surface = IOSurfaceCreate((__bridge CFDictionaryRef)props))) {
            CFPropertyListRef colorspace = CGColorSpaceCopyPropertyList(rgb);
            if (colorspace)
                IOSurfaceSetValue(surface, kIOSurfaceColorSpace, colorspace), CFRelease(colorspace);
            ctx = CGBitmapContextCreate(IOSurfaceGetBaseAddress(surface), w, h, 8, IOSurfaceGetBytesPerRow(surface), rgb,
                                        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        }
    }
    CGSurface(const CGSurface&) = delete;
    ~CGSurface() {
        if (ctx) CGContextRelease(ctx);
        if (surface) CFRelease(surface);
    }
    IOSurfaceRef surface = nullptr;  CGContextRef ctx = nullptr;
};
#endif


@interface RasterizerView () <CALayerDelegate, LayerDelegate>

#if TARGET_OS_OSX
@property(nonatomic) CVDisplayLinkRef displayLink;
#elif TARGET_OS_IPHONE
@property(nonatomic) CADisplayLink *displayLink;
#endif

@property(nonatomic) dispatch_semaphore_t inflight_semaphore;
@property(nonatomic) RasterizerRenderer renderer;
- (void)handleTimerTick:(id)sender;

@end

#if TARGET_OS_OSX
@interface RasterizerView ()
{
    std::vector<std::unique_ptr<CGSurface>> _cgSurfaces;    // CG mode's surfaces, at the layer's size, reused unless CA is using them
    size_t _cgWidth, _cgHeight;
}
@end
#endif

#if TARGET_OS_OSX
static CVReturn OnDisplayLinkFrame(CVDisplayLinkRef displayLink, const CVTimeStamp *now, const CVTimeStamp *outputTime,
CVOptionFlags flagsIn, CVOptionFlags *flagsOut, void *displayLinkContext) {
    RasterizerView *view = (__bridge RasterizerView *)displayLinkContext;
    [view handleTimerTick: nil];
    return kCVReturnSuccess;
}
#endif

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
    [self startTimer];
}

- (void)removeFromSuperview {
    [self stopTimer];
    [super removeFromSuperview];
}


#pragma mark - Timer

- (void)startTimer {
    _inflight_semaphore = dispatch_semaphore_create(1);
#if TARGET_OS_OSX
    CVReturn cvReturn = CVDisplayLinkCreateWithCGDisplay(CGMainDisplayID(), &_displayLink);
    cvReturn = CVDisplayLinkSetOutputCallback(_displayLink, &OnDisplayLinkFrame, (__bridge void *)self);
    CVDisplayLinkStart(_displayLink);
#elif TARGET_OS_IPHONE
    if ([CADisplayLink class]) {
        _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(handleTimerTick:)];
        [_displayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
    }
#endif
}

- (void)stopTimer {
#if TARGET_OS_OSX
    if (_displayLink)
        CVDisplayLinkStop(_displayLink), CVDisplayLinkRelease(_displayLink), _displayLink = nil;
#elif TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
    [_displayLink setPaused:YES];
    [_displayLink invalidate];
    _displayLink = nil;
#endif
}

- (void)handleTimerTick:(id)sender {
    @autoreleasepool {
        if (dispatch_semaphore_wait(_inflight_semaphore, DISPATCH_TIME_NOW) == 0)
            dispatch_async(dispatch_get_main_queue(), ^{
                if ([self.listDelegate respondsToSelector:@selector(shouldRedrawAtTime:scale:width:height:)]) {
                    double scale = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
                    if ([self.listDelegate shouldRedrawAtTime:CACurrentMediaTime()
                                                        scale:scale
                                                        width:w
                                                       height:h]) {
                        [self.layer setNeedsDisplay];
                    }
                }
                dispatch_semaphore_signal(_inflight_semaphore);
            });
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
#if TARGET_OS_OSX
        // A surface CA isn't using, as setting a CGImage as contents makes CA copy & color match it at each commit, & a new bitmap
        // each display faults in its pages
        if (pw != _cgWidth || ph != _cgHeight)
            _cgSurfaces.clear(), _cgWidth = pw, _cgHeight = ph;
        CGSurface *surface = nullptr;
        for (auto& s : _cgSurfaces)
            if (!IOSurfaceIsInUse(s->surface)) {
                surface = s.get();
                break;
            }
        if (surface == nullptr) {
            CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
            std::unique_ptr<CGSurface> s(new CGSurface(pw, ph, rgb));
            CGColorSpaceRelease(rgb);
            if (s->ctx == nullptr)
                return;
            _cgSurfaces.push_back(std::move(s)), surface = _cgSurfaces.back().get();
        }
        IOSurfaceLock(surface->surface, 0, nullptr);
        CGContextClearRect(surface->ctx, CGRectMake(0, 0, pw, ph));     // As the clear color can be transparent
        CGContextSaveGState(surface->ctx);
        CGContextScaleCTM(surface->ctx, scale, scale);
        RaCG::renderListWithClear(list.list, scale, w, h, surface->ctx);
        CGContextRestoreGState(surface->ctx);
        IOSurfaceUnlock(surface->surface, 0, nullptr);
        layer.contents = (__bridge id)surface->surface;
#else
        CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(nullptr, pw, ph, 8, pw * 4, rgb, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        CGColorSpaceRelease(rgb);
        if (ctx == nullptr)
            return;
        CGContextScaleCTM(ctx, scale, scale);
        RaCG::renderListWithClear(list.list, scale, w, h, ctx);
        CGImageRef image = CGBitmapContextCreateImage(ctx);
        layer.contents = (__bridge id)image;
        CGImageRelease(image), CGContextRelease(ctx);
#endif
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
