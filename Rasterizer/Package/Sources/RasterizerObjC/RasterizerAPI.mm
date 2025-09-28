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


#pragma mark - RasterizerPath

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

@end


#pragma mark - RasterizerTypeface

@implementation RAFont: NSObject

- (id)initWithName:(NSString *)name {
    self = [super init];
    if (!self)
        return nil;
    NSURL *url = RaUtils::fontURL(name);
    if (url != nil)
        _font.load(url.path.UTF8String, name.UTF8String);
    return self;
}

@end


#pragma mark - RasterizerScene

@implementation RAScene: NSObject

- (CGRect)bounds {
    return RaCG::CGRectFromBounds(_scene.bounds());
}

- (void)addPath:(RAPath *)path ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags {
    Ra::Path p = path.path;
    _scene.addPath(p,
                   RaCG::transformFromCG(ctm),
                   RaCG::colorantFromCG(color),
                   width,
                   flags);
}
- (void)addPath:(RAPath *)path ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags clip:(CGRect)clip {
    Ra::Path p = path.path;
    Ra::Bounds clipBounds = RaCG::BoundsFromCGRect(clip);
    _scene.addPath(p,
                   RaCG::transformFromCG(ctm),
                   RaCG::colorantFromCG(color),
                   width,
                   flags,
                   & clipBounds);
}
- (void)addCGPath:(CGPathRef)cgPath ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags {
    [self addPath:[[RAPath alloc] initWithCGPath:cgPath]
              ctm:ctm
            color:color
            width:width
            flags:flags];
}
- (void)addCGPath:(CGPathRef)cgPath ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags clip:(CGRect)clip {
    [self addPath:[[RAPath alloc] initWithCGPath:cgPath]
              ctm:ctm
            color:color
            width:width
            flags:flags
             clip:clip];
}

- (void)addText:(NSString *)text font:(RAFont *)font pointSize:(double)pointSize ctm:(CGAffineTransform)ctm color:(CGColorRef)color {
    font.font.layoutGlyphs(pointSize, RaCG::colorantFromCG(color), Ra::Bounds(0, 0, 1e3, pointSize), RaCG::transformFromCG(ctm), false, false, false, text.UTF8String, _scene);
}

@end


#pragma mark - RasterizerScene

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

- (void)addList:(RASceneList *)list {
    _list.addList(list.list);
}
- (void)addScene:(RAScene *)scene ctm:(CGAffineTransform)ctm {
    _list.addScene(scene.scene, RaCG::transformFromCG(ctm));
}
- (void)addScene:(RAScene *)scene ctm:(CGAffineTransform)ctm clip:(CGRect)clip {
    _list.addScene(scene.scene, RaCG::transformFromCG(ctm), RaCG::BoundsFromCGRect(clip));
}

@end
