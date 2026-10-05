//
//  RasterizerAPI.mm
//  Rasterizer
//
//  Created by Nigel Barber on 03/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <CoreText/CoreText.h>
#import "RasterizerAPI+Internal.h"
#import "RasterizerCG.hpp"
#import "RasterizerSVG.hpp"
#import "RasterizerCoreText.hpp"
#import <map>


#pragma mark - RAColor

@implementation RAPaint: NSObject

- (id)initWithGray:(double)gray alpha:(double)alpha {
    self = [super init];
    if (!self)
        return nil;
    _paint = Ra::Color(gray * 255, gray * 255, gray * 255, alpha * 255);
    return self;
}

- (id)initWithHue:(double)hue saturation:(double)saturation value:(double)value alpha:(double)alpha {
    self = [super init];
    if (!self)
        return nil;
    double H, C, X, m, r, g, b;
    H = hue * 360.0;
    C = saturation * value;
    X = C * (1 - abs(fmod(H / 60.0, 2) - 1));
    m = value - C;
    if (H >= 0 && H < 60)
        r = C, g = X, b = 0.0;
    else if (H >= 60 && H < 120)
        r = X, g = C, b = 0.0;
    else if (H >= 120 && H < 180)
        r = 0.0, g = C, b = X;
    else if (H >= 180 && H < 240)
        r = 0.0, g = X, b = C;
    else if (H >= 240 && H < 300)
        r = X, g = 0.0, b = C;
    else
        r = C, g = 0.0, b = X;
    _paint = Ra::Color((b + m) * 255, (g + m) * 255, (r + m) * 255, alpha * 255);
    return self;
}

- (id)initWithRed:(double)red green:(double)green blue:(double)blue alpha:(double)alpha {
    self = [super init];
    if (!self)
        return nil;
    _paint = Ra::Color(blue * 255, green * 255, red * 255, alpha * 255);
    return self;
}

- (id)initWithCGColor:(CGColorRef)cgColor {
    self = [super init];
    if (!self)
        return nil;
    _paint = RaCG::colorFromCG(cgColor);
    return self;
}

- (nonnull id)initLinear:(NSArray<RAPaint *>*)colors
               locations:(NSArray<NSNumber *>*)locations
                   start:(CGPoint)start
                     end:(CGPoint)end {
    double dx = end.x - start.x, dy = end.y - start.y;
    return [self initWithColors:colors
                      locations:locations
                      transform:CGAffineTransformMake(dy, -dx, dx, dy, start.x, start.y)
                       isRadial:NO];
}

- (id)initRadial:(NSArray<RAPaint *>*)colors
       locations:(NSArray<NSNumber *>*)locations
          center:(CGPoint)center
          radius:(double)radius {
    return [self initWithColors:colors
                      locations:locations
                      transform:CGAffineTransformMake(radius, 0, 0, radius, center.x, center.y)
                       isRadial:YES];
}

- (id)initWithColors:(NSArray<RAPaint *>*)colors
               locations:(NSArray<NSNumber *>*)locations
               transform:(CGAffineTransform)transform
            isRadial:(BOOL)isRadial {
    self = [super init];
    if (!self)
        return nil;
    if (colors.count > 1 && colors.count == locations.count) {
        NSInteger count = colors.count;
        
        Ra::Vector<Ra::Color> stops(count);
        Ra::Vector<float> locs(count);
        for (NSInteger i = 0; i < count; i++) {
            stops[i] = colors[i].paint.color;
            locs[i] = locations[i].floatValue;
        }
        _paint = Ra::Paint(& stops[0], & locs[0], count, RaCG::transformFromCG(transform), isRadial);
    }
    return self;
}

- (id)initWithCGImage:(CGImageRef)cgImage {
    self = [super init];
    if (!self)
        return nil;
    _paint = RaCG::paintFromCGImage(cgImage);
    return self;
}
@end


#pragma mark - RAPath

@implementation RAPath: NSObject

- (id)initWithCGPath:(CGPathRef)cgPath {
    self = [super init];
    if (!self)
        return nil;
    [self addCGPath:cgPath];
    return self;
}

- (id)initWithRect:(CGRect)rect {
    self = [super init];
    if (!self)
        return nil;
    [self addRect:rect];
    return self;
}

- (id)initWithEllipse:(CGRect)rect {
    self = [super init];
    if (!self)
        return nil;
    [self addEllipse:rect];
    return self;
}

- (nonnull id)initWithRoundedRect:(CGRect)rect
                      cornerWidth:(double)cornerWidth
                     cornerHeight:(double)cornerHeight {
    self = [super init];
    if (!self)
        return nil;
    [self addRoundedRect:rect cornerWidth:cornerWidth cornerHeight:cornerHeight];
    return self;
}

- (CGRect)bounds {
    return RaCG::CGRectFromBounds(_path->bounds);
}

- (void)moveTo:(double)x y:(double)y {
    _path->moveTo(x, y);
}
- (void)lineTo:(double)x y:(double)y {
    _path->lineTo(x, y);
}
- (void)quadTo:(double)x1 y1:(double)y1 x2:(double)x2 y2:(double)y2 {
    _path->quadTo(x1, y1, x2, y2);
}
- (void)cubicTo:(double)x1 y1:(double)y1 x2:(double)x2 y2:(double)y2 x3:(double)x3 y3:(double)y3 {
    _path->cubicTo(x1, y1, x2, y2, x3, y3);
}
- (void)close {
    _path->close();
}
- (void)addRect:(CGRect)rect {
    if (CGRectGetWidth(rect) > 0 && CGRectGetHeight(rect) > 0) {
        _path->addBounds(RaCG::BoundsFromCGRect(rect));
    }
}
- (void)addEllipse:(CGRect)rect {
    if (CGRectGetWidth(rect) > 0 && CGRectGetHeight(rect) > 0) {
        _path->addEllipse(RaCG::BoundsFromCGRect(rect));
    }
}
- (void)addRoundedRect:(CGRect)rect
           cornerWidth:(double)cornerWidth
          cornerHeight:(double)cornerHeight {
    _path->addRoundedRect(RaCG::BoundsFromCGRect(rect), cornerWidth, cornerHeight);
}
- (void)addCGPath:(CGPathRef)cgPath {
    RaCG::writeCGPathToPath(cgPath, _path);
}

- (RAPath *)dashedCopyWithPhase:(double)phase
                        lengths:(NSArray<NSNumber *>*)lengths {
    NSInteger count = lengths.count;
    if (count < 2)
        return self;
    Ra::Vector<float> lens(count);
    for (NSInteger i = 0; i < count; i++)
        lens[i] = lengths[i].floatValue;
    RAPath *dashed = [RAPath new];
    dashed.path = Ra::Dasher::CreateDashedPath(self.path, phase, & lens[0], count);
    return dashed;
}

@end


@implementation RAFrame: NSObject
+ (double)lineHeightForFont:(nonnull NSString *)named size:(double)size {
    return RaCT::lineHeightFor(named.UTF8String, size);
}

- (id)initWithAttributedString:(nonnull NSAttributedString *)attributedString inRect:(CGRect)rect {
    self = [super init];
    if (!self)
        return nil;
    CTFramesetterRef framesetter = CTFramesetterCreateWithAttributedString((__bridge CFAttributedStringRef)attributedString);
    CGPathRef rectPath = CGPathCreateWithRect(rect, NULL);
    _frame = CTFramesetterCreateFrame(framesetter, CFRangeMake(0, 0), rectPath, NULL);
    CGPathRelease(rectPath);
    CFRelease(framesetter);
    return self;
}

- (void)applyRuns:(nonnull CTRunApplyBlock)block {
    [self applyLines:^(CTLineRef _Nonnull ctLine, CGPoint origin) {
        CFArrayRef glyphRuns = CTLineGetGlyphRuns(ctLine);
        for (int run = 0; run < CFArrayGetCount(glyphRuns); run++) {
            CTRunRef ctRun = (CTRunRef)CFArrayGetValueAtIndex(glyphRuns, run);
            CGRect image = CTRunGetImageBounds(ctRun, NULL, CFRangeMake(0, 0));
            if (image.size.width > 0 && image.size.height > 0) {
                CFRange range = CTRunGetStringRange(ctRun);
                block(
                    NSMakeRange(range.location, range.length),
                    CGRectApplyAffineTransform(image, CGAffineTransformMakeTranslation(origin.x, origin.y))
                );
            }
        }
    }];
}
- (void)applyLines:(nonnull CTLineApplyBlock)block {
    CGRect rect = CGPathGetBoundingBox(CTFrameGetPath(_frame));
    CFArrayRef lines = CTFrameGetLines(_frame);
    CFIndex lineCount = CFArrayGetCount(lines);
    Ra::Vector<CGPoint> origins(lineCount);
    CTFrameGetLineOrigins(_frame, CFRangeMake(0, 0), & origins[0]);
    for (int line = 0; line < lineCount; line++) {
        block(
            (CTLineRef)CFArrayGetValueAtIndex(lines, line),
            CGPointMake(rect.origin.x + origins[line].x, rect.origin.y + origins[line].y)
        );
    }
}
- (void)dealloc {
    CFRelease(_frame);
}
@end

#pragma mark - RAScene

@implementation RADraw: NSObject

- (id)init {
    self = [super init];
    if (!self)
        return nil;
    _draw.clip = Ra::Bounds::huge();
    return self;
}

- (id)initWithPath:(RAPath *)path ctm:(CGAffineTransform)ctm color:(RAPaint *)color {
    self = [self init];
    if (!self)
        return nil;
    _draw = Ra::Draw(path.path, RaCG::transformFromCG(ctm), color.paint, 0.f, 0);
    return self;
}

- (RAPath *)path {
    RAPath *path = [RAPath new];
    path.path = _draw.path;
    return path;
}
- (void)setPath:(RAPath *)path {
    _draw.path = path.path, _draw.validate();
}
- (CGAffineTransform)ctm {
    return RaCG::CGFromTransform(_draw.ctm);
}
- (void)setCtm:(CGAffineTransform)ctm {
    _draw.ctm = RaCG::transformFromCG(ctm);
}
- (RAPaint *)color {
    RAPaint *color = [RAPaint new];
    color.paint = _draw.paint;
    return color;
}
- (void)setColor:(RAPaint *)color {
    _draw.paint = color.paint, _draw.validate();
}
- (double)width {
    return _draw.width;
}
- (void)setWidth:(double)width {
    _draw.width = width;
}
- (BOOL)hidden {
    return (_draw.flags & Ra::Draw::kHidden) != 0;
}
- (void)setHidden:(BOOL)hidden {
    _draw.flags = (_draw.flags & ~Ra::Draw::kHidden) | (hidden ? Ra::Draw::kHidden : 0), _draw.validate();
}
- (BOOL)evenOdd {
    return (_draw.flags & Ra::Draw::kFillEvenOdd) != 0;
}
- (void)setEvenOdd:(BOOL)evenOdd {
    _draw.flags = (_draw.flags & ~Ra::Draw::kFillEvenOdd) | (evenOdd ? Ra::Draw::kFillEvenOdd : 0);
}
- (RACapStyle)capStyle {
    return _draw.flags & Ra::Draw::kSquareCap ? kCapSquare : _draw.flags & Ra::Draw::kRoundCap ? kCapRound : kCapButt;
}
- (void)setCapStyle:(RACapStyle)capStyle {
    uint8_t cap = capStyle == kCapSquare ? Ra::Draw::kSquareCap : capStyle == kCapRound ? Ra::Draw::kRoundCap : 0;
    _draw.flags = (_draw.flags & ~(Ra::Draw::kSquareCap | Ra::Draw::kRoundCap)) | cap;
}
- (RAJoinStyle)joinStyle {
    return _draw.flags & Ra::Draw::kRoundJoin ? kJoinRound : kJoinMiter;
}
- (void)setJoinStyle:(RAJoinStyle)joinStyle {
    _draw.flags = (_draw.flags & ~Ra::Draw::kRoundJoin) | (joinStyle == kJoinRound ? Ra::Draw::kRoundJoin : 0);
}
- (CGRect)clip {
    return _draw.clip.isHuge() ? CGRectInfinite : RaCG::CGRectFromBounds(_draw.clip);
}
- (void)setClip:(CGRect)clip {
    _draw.clip = CGRectIsNull(clip) || CGRectIsEmpty(clip) || CGRectIsInfinite(clip) ? Ra::Bounds::huge() : RaCG::BoundsFromCGRect(clip);
}
- (RAPath *)clipPath {
    if (_draw.clipPath.ptr == nullptr)
        return nil;
    RAPath *path = [RAPath new];
    path.path = _draw.clipPath;
    return path;
}
- (void)setClipPath:(RAPath *)clipPath {
    _draw.clipPath = clipPath ? clipPath.path : Ra::Path(nullptr), _draw.validate();
}

@end


#pragma mark - RAScene

@implementation RAScene: NSObject

- (NSUInteger)count {
    return _scene->count();
}

- (RADraw *)drawAtIndex:(NSUInteger)index {
    RADraw *draw = [RADraw new];
    if (index < _scene->count())
        draw.draw = _scene->draw(index);
    return draw;
}

- (void)setDraw:(RADraw *)draw atIndex:(NSUInteger)index {
    if (index < _scene->count())
        _scene->setDraw(index, draw.draw);
}

- (void)addDraw:(RADraw *)draw {
    Ra::Draw d = draw.draw;
    d.validate(), _scene->addDraws(& d, 1);
}

- (void)addDrawsFromScene:(RAScene *)scene {
    _scene->addDraws(*scene.scene.ptr, 0, scene.scene->count());
}

- (void)addDrawsFromScene:(RAScene *)scene range:(NSRange)range {
    _scene->addDraws(*scene.scene.ptr, range.location, range.location + range.length);
}

- (NSString *)validate {
    const char *error = _scene->validate();
    return error ? [NSString stringWithUTF8String:error] : nil;
}

+ (NSString *)validateCache {
    const char *error = Ra::GeometryCache::shared().validate();
    return error ? [NSString stringWithUTF8String:error] : nil;
}

+ (NSDictionary<NSString *, NSNumber *> *)cacheStatistics {
    Ra::GeometryCache& cache = Ra::GeometryCache::shared();
    std::lock_guard<std::mutex> lock(cache.mutex);
    size_t p16 = sizeof(Ra::Point16), outline = sizeof(Ra::Outline);
    return @{
        @"entries": @(cache.entries.end() - cache.freeSlots.end), @"retiringBytes": @(cache.retiringBytes),
        @"p16Capacity": @(cache.p16s.capacity * p16), @"p16End": @(cache.p16s.end * p16), @"p16Used": @(cache.p16s.used * p16),
        @"p16Waste": @((cache.p16s.allocated - cache.p16s.used) * p16), @"p16Free": @(cache.p16s.freeTotal * p16), @"p16Grows": @(cache.p16s.grows),
        @"outlineCapacity": @(cache.outlines.capacity * outline), @"outlineEnd": @(cache.outlines.end * outline), @"outlineUsed": @(cache.outlines.used * outline),
        @"outlineWaste": @((cache.outlines.allocated - cache.outlines.used) * outline), @"outlineFree": @(cache.outlines.freeTotal * outline), @"outlineGrows": @(cache.outlines.grows),
    };
}

- (CGRect)bounds {
    return RaCG::CGRectFromBounds(_scene->bounds());
}

- (void)addFill:(RAPath *)path
            ctm:(CGAffineTransform)ctm
           color:(RAPaint *)color
        evenOdd:(BOOL)evenOdd {
    [self addFill:path ctm:ctm color:color evenOdd:evenOdd clip:CGRectZero clipPath:nil];
}

- (void)addStroke:(RAPath *)path
              ctm:(CGAffineTransform)ctm
            color:(RAPaint *)color
            width:(double)width
         capStyle:(RACapStyle)capStyle
        joinStyle:(RAJoinStyle)joinStyle {
    [self addStroke:path ctm:ctm color:color width:width capStyle:capStyle joinStyle:joinStyle clip:CGRectZero clipPath:nil];
}

- (void)addImage:(nonnull RAPaint *)image
             ctm:(CGAffineTransform)ctm {
    if (image.paint.isImage()) {
        CGRect rect = CGRectMake(0, 0, image.paint.bitmap->w, image.paint.bitmap->h);
        RAPath *path = [[RAPath alloc] initWithRect:rect];
        [self addFill:path ctm:ctm color:image evenOdd:false];
    }
}

- (void)addFill:(RAPath *)path
            ctm:(CGAffineTransform)ctm
           color:(RAPaint *)color
        evenOdd:(BOOL)evenOdd
           clip:(CGRect)clip
       clipPath:(RAPath *)clipPath {
    Ra::Path p = path.path;
    Ra::Bounds clipBounds = CGRectIsNull(clip) || CGRectIsEmpty(clip) || CGRectIsInfinite(clip) ? Ra::Bounds::huge() : RaCG::BoundsFromCGRect(clip);
    auto m = RaCG::transformFromCG(ctm);
    Ra::Path clp = clipPath != nil ? clipPath.path : Ra::Path(nullptr);
    _scene->addPath(p, m, color.paint, 0, evenOdd ? Ra::Draw::kFillEvenOdd : 0, & clipBounds, clipPath != nil ? & clp : nullptr);
}

- (void)addStroke:(RAPath *)path
              ctm:(CGAffineTransform)ctm
            color:(RAPaint *)color
            width:(double)width
         capStyle:(RACapStyle)capStyle
        joinStyle:(RAJoinStyle)joinStyle
             clip:(CGRect)clip
         clipPath:(RAPath *)clipPath {
    Ra::Path p = path.path;
    Ra::Bounds clipBounds = CGRectIsNull(clip) || CGRectIsEmpty(clip) || CGRectIsInfinite(clip) ? Ra::Bounds::huge() : RaCG::BoundsFromCGRect(clip);
    auto m = RaCG::transformFromCG(ctm);
    uint8_t capFlags = capStyle == kCapButt ? 0 : capStyle == kCapSquare ? Ra::Draw::kSquareCap : Ra::Draw::kRoundCap;
    uint8_t joinFlags = joinStyle == kJoinMiter ? 0 : Ra::Draw::kRoundJoin;
    Ra::Path clp = clipPath != nil ? clipPath.path : Ra::Path(nullptr);
    _scene->addPath(p, m, color.paint, width, capFlags | joinFlags, & clipBounds, clipPath != nil ? & clp : nullptr);
}

- (CGRect)addFrame:(RAFrame *)frame
          excludes:(NSArray<NSValue *> *)excludes
               ctm:(CGAffineTransform)ctm
              clip:(CGRect)clip {
    GlyphCache cache;
    return RaCT::addFrameToScene(frame.frame, excludes, ctm, clip, _scene, cache);
}

- (CGRect)addText:(NSAttributedString *)string inRect:(CGRect)rect ctm:(CGAffineTransform)ctm clip:(CGRect)clip {
    GlyphCache cache;
    return RaCT::addTextToSceneInRect((__bridge CFAttributedStringRef)string, rect, ctm, clip, _scene, cache);
}
- (CGAffineTransform)addSvgFromUrl:(NSURL *)url {
    return RaCG::CGFromTransform(RaSVG::addSvgToScene(url.path.UTF8String, _scene));
}

@end


#pragma mark - RASceneList

@implementation RASceneList: NSObject

- (id)initWithScene:(RAScene *)scene {
    self = [super init];
    if (!self)
        return nil;
    [self addScene:scene];
    return self;
}

- (CGRect)bounds {
    return RaCG::CGRectFromBounds(_list.bounds());
}
- (CGAffineTransform)ctm {
    return RaCG::CGFromTransform(_list.ctm);
}
- (void)setCtm:(CGAffineTransform)ctm {
    _list.ctm = RaCG::transformFromCG(ctm);
}
- (nonnull RAPaint *)clearColor {
    RAPaint *clear = [RAPaint new];
    clear.paint = _list.params.clearColor;
    return clear;
}
- (void)setClearColor:(RAPaint *)clearColor {
    _list.params.clearColor = clearColor.paint.color;
}
- (BOOL)useClips {
    return _list.params.useClips;
}
- (void)setUseClips:(BOOL)useClips {
    _list.params.useClips = useClips;
}
- (BOOL)useCurves {
    return _list.params.useCurves;
}
- (void)setUseCurves:(BOOL)useCurves {
    _list.params.useCurves = useCurves;
}
- (BOOL)showOpaques {
    return _list.params.showOpaques;
}
- (void)setShowOpaques:(BOOL)showOpaques {
    _list.params.showOpaques = showOpaques;
}
- (BOOL)showOutlines {
    return _list.params.showOutlines;
}
- (void)setShowOutlines:(BOOL)showOutlines {
    _list.params.showOutlines = showOutlines;
}

- (void)addList:(RASceneList *)list {
    _list.addList(list.list);
}

- (void)addScene:(RAScene *)scene {
    _list.addScene(scene.scene, Ra::Transform(), Ra::Bounds::huge());
}

- (void)addScene:(RAScene *)scene ctm:(CGAffineTransform)ctm clip:(CGRect)clip {
    Ra::Bounds clipBounds = CGRectIsNull(clip) || CGRectIsEmpty(clip) || CGRectIsInfinite(clip) ? Ra::Bounds::huge() : RaCG::BoundsFromCGRect(clip);
    _list.addScene(scene.scene, RaCG::transformFromCG(ctm), clipBounds);
}

@end
