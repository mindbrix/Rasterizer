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
                if ([self.listDelegate respondsToSelector:@selector(shouldRedrawAtTime:width:height:)]) {
                    float w = self.bounds.size.width, h = self.bounds.size.height;
                    if ([self.listDelegate shouldRedrawAtTime:CACurrentMediaTime()
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
    if ([self.listDelegate respondsToSelector:@selector(getListAtTime:width:height:)]) {
        float scale = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
        RASceneList *list = [self.listDelegate getListAtTime:CACurrentMediaTime()
                                                       width:w
                                                      height:h];
        _renderer.renderList(list.list, scale, w, h, buffer, layer);
    }
}

#pragma mark - CALayerDelegate

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx {
    if ([self.listDelegate respondsToSelector:@selector(getListAtTime:width:height:)]) {
        float scale = self.layer.contentsScale, w = self.bounds.size.width, h = self.bounds.size.height;
        RASceneList *list = [self.listDelegate getListAtTime:CACurrentMediaTime()
                                                       width:w
                                                      height:h];
        RaCG::renderListWithClear(list.list, scale, w, h, ctx);
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
        self.layer.contentsFormat = kCAContentsFormatRGBA8Uint;
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
