//
//  RasterizerObjC.mm
//  Rasterizer
//
//  Created by Nigel Barber on 03/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "RasterizerAPI+Internal.h"
#import "RasterizerCG.hpp"
#import "RasterizerUtilities.h"
#import "RasterizerSVG.hpp"
#import "RasterizerCoreText.hpp"


#pragma mark - RAColor

@implementation RAColor: NSObject

- (id)initWithGray:(double)gray alpha:(double)alpha {
    self = [super init];
    if (!self)
        return nil;
    _color = Ra::BGRA(gray * 255, gray * 255, gray * 255, alpha * 255);
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
    _color = Ra::BGRA((b + m) * 255, (g + m) * 255, (r + m) * 255, alpha * 255);
    return self;
}

- (id)initWithRed:(double)red green:(double)green blue:(double)blue alpha:(double)alpha {
    self = [super init];
    if (!self)
        return nil;
    _color = Ra::BGRA(blue * 255, green * 255, red * 255, alpha * 255);
    return self;
}

- (id)initWithCGColor:(CGColorRef)cgColor {
    self = [super init];
    if (!self)
        return nil;
    _color = RaCG::colorantFromCG(cgColor);
    return self;
}

- (id)initLinearWithColors:(NSArray<RAColor *>*)colors
                 locations:(NSArray<NSNumber *>*)locations
                     start:(CGPoint)start
                       end:(CGPoint)end {
    double dx = end.x - start.x, dy = end.y - start.y;
    return [self initWithColors:colors
                      locations:locations
                      transform:CGAffineTransformMake(dy, -dx, dx, dy, start.x, start.y)
                       isRadial:NO];
}

- (id)initRadialWithColors:(NSArray<RAColor *>*)colors
                 locations:(NSArray<NSNumber *>*)locations
                    center:(CGPoint)center
                    radius:(double)radius {
    return [self initWithColors:colors
                      locations:locations
                      transform:CGAffineTransformMake(radius, 0, 0, radius, center.x, center.y)
                       isRadial:YES];
}

- (id)initWithColors:(NSArray<RAColor *>*)colors
               locations:(NSArray<NSNumber *>*)locations
               transform:(CGAffineTransform)transform
            isRadial:(BOOL)isRadial {
    self = [super init];
    if (!self)
        return nil;
    if (colors.count > 1 && colors.count == locations.count) {
        NSInteger count = colors.count;
        
        Ra::BGRA stops[count];
        float locs[count];
        for (NSInteger i = 0; i < count; i++) {
            stops[i] = colors[i].color.colorant;
            locs[i] = locations[i].floatValue;
        }
        _color = Ra::Color(stops, locs, count, RaCG::transformFromCG(transform), isRadial);
    }
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
    _path->addBounds(RaCG::BoundsFromCGRect(rect));
}
- (void)addEllipse:(CGRect)rect {
    _path->addEllipse(RaCG::BoundsFromCGRect(rect));
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

- (RAPath *)dashedCGCopyWithPhase:(double)phase
                    lengths:(NSArray<NSNumber *>*)lengths {
    return self;
}

@end


#pragma mark - RAScene

@implementation RAScene: NSObject

- (CGRect)bounds {
    return RaCG::CGRectFromBounds(_scene->bounds());
}

- (void)addFill:(RAPath *)path
            ctm:(CGAffineTransform)ctm
           color:(RAColor *)color
        evenOdd:(BOOL)evenOdd
           clip:(CGRect)clip {
    Ra::Path p = path.path;
    Ra::Bounds clipBounds = CGRectIsNull(clip) || CGRectIsEmpty(clip) || CGRectIsInfinite(clip) ? Ra::Bounds::huge() : RaCG::BoundsFromCGRect(clip);
    auto m = RaCG::transformFromCG(ctm);
    _scene->addFill(p, m, color.color, evenOdd, & clipBounds);
    
}

- (void)addStroke:(RAPath *)path
              ctm:(CGAffineTransform)ctm
            color:(RAColor *)color
            width:(double)width
         capStyle:(RACapStyle)capStyle
        joinStyle:(RAJoinStyle)joinStyle
             clip:(CGRect)clip {
    Ra::Path p = path.path;
    Ra::Bounds clipBounds = CGRectIsNull(clip) || CGRectIsEmpty(clip) || CGRectIsInfinite(clip) ? Ra::Bounds::huge() : RaCG::BoundsFromCGRect(clip);
    auto m = RaCG::transformFromCG(ctm);
    _scene->addStroke(p, m, color.color, width, (Ra::Scene::CapStyle)capStyle, (Ra::Scene::JoinStyle)joinStyle);
}

- (CGRect)addTextLine:(NSAttributedString *)string ctm:(CGAffineTransform)ctm clip:(CGRect)clip {
    return RasterizerCoreText::addTextLineToScene((__bridge CFAttributedStringRef)string, ctm, clip, _scene);
}
- (CGRect)addText:(NSAttributedString *)string inRect:(CGRect)rect ctm:(CGAffineTransform)ctm clip:(CGRect)clip {
    return RasterizerCoreText::addTextToSceneInRect((__bridge CFAttributedStringRef)string, rect, ctm, clip, _scene);
}
- (CGAffineTransform)addSvgFromData:(NSData *)data {
    return RaCG::CGFromTransform(RasterizerSVG::addSvgDataToScene(data.bytes, data.length, _scene));
}

@end


#pragma mark - RASceneList

@implementation RASceneList: NSObject

- (CGRect)bounds {
    return RaCG::CGRectFromBounds(_list.bounds());
}
- (CGAffineTransform)ctm {
    return RaCG::CGFromTransform(_list.ctm);
}
- (void)setCtm:(CGAffineTransform)ctm {
    _list.ctm = RaCG::transformFromCG(ctm);
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

- (void)addScene:(RAScene *)scene ctm:(CGAffineTransform)ctm clip:(CGRect)clip {
    Ra::Bounds clipBounds = CGRectIsNull(clip) || CGRectIsEmpty(clip) || CGRectIsInfinite(clip) ? Ra::Bounds::huge() : RaCG::BoundsFromCGRect(clip);
    _list.addScene(scene.scene, RaCG::transformFromCG(ctm), clipBounds);
}

@end
