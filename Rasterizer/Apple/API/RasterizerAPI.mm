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


#pragma mark - RasterizerPath

@implementation RasterizerPath: NSObject

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

- (void)moveTo:(float)x y:(float)y {
    _path->moveTo(x, y);
}
- (void)lineTo:(float)x y:(float)y {
    _path->lineTo(x, y);
}
- (void)quadTo:(float)x1 y1:(float)y1 x2:(float)x2 y2:(float)y2 {
    _path->quadTo(x1, y1, x2, y2);
}
- (void)cubicTo:(float)x1 y1:(float)y1 x2:(float)x2 y2:(float)y2 x3:(float)x3 y3:(float)y3 {
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


#pragma mark - RasterizerScene

@implementation RasterizerScene: NSObject

- (CGRect)bounds {
    return RaCG::CGRectFromBounds(_scene.bounds());
}

- (void)addPath:(RasterizerPath *)path ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags {
    _scene.addPath(path.path,
                   RaCG::transformFromCG(ctm),
                   RaCG::colorantFromCG(color),
                   width,
                   flags);
}
- (void)addPath:(RasterizerPath *)path ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags clip:(CGRect)clip {
    Ra::Bounds clipBounds = RaCG::BoundsFromCGRect(clip);
    _scene.addPath(path.path,
                   RaCG::transformFromCG(ctm),
                   RaCG::colorantFromCG(color),
                   width,
                   flags,
                   & clipBounds);
}
- (void)addCGPath:(CGPathRef)cgPath ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags {
    [self addPath:[[RasterizerPath alloc] initWithCGPath:cgPath]
              ctm:ctm
            color:color
            width:width
            flags:flags];
}
- (void)addCGPath:(CGPathRef)cgPath ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags clip:(CGRect)clip {
    [self addPath:[[RasterizerPath alloc] initWithCGPath:cgPath]
              ctm:ctm
            color:color
            width:width
            flags:flags
             clip:clip];
}

@end


#pragma mark - RasterizerScene

@implementation RasterizerSceneList: NSObject

- (CGRect)bounds {
    return RaCG::CGRectFromBounds(_list.bounds());
}
- (CGAffineTransform)ctm {
    return RaCG::CGFromTransform(_list.ctm);
}
- (void)setCtm:(CGAffineTransform)ctm {
    _list.ctm = RaCG::transformFromCG(ctm);
}

- (void)addList:(RasterizerSceneList *)list {
    _list.addList(list.list);
}
- (void)addScene:(RasterizerScene *)scene ctm:(CGAffineTransform)ctm {
    _list.addScene(scene.scene, RaCG::transformFromCG(ctm));
}
- (void)addScene:(RasterizerScene *)scene ctm:(CGAffineTransform)ctm clip:(CGRect)clip {
    _list.addScene(scene.scene, RaCG::transformFromCG(ctm), RaCG::BoundsFromCGRect(clip));
}

@end
