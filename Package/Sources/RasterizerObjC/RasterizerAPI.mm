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
#import "RasterizerPDF.hpp"
#import "RasterizerCoreText.hpp"


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


#pragma mark - RasterizerScene

@implementation RAScene: NSObject

- (CGRect)bounds {
    return RaCG::CGRectFromBounds(_scene.bounds());
}

- (void)addPath:(RAPath *)path ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags clip:(CGRect)clip {
    Ra::Path p = path.path;
    Ra::Bounds clipBounds = CGRectIsNull(clip) || CGRectIsEmpty(clip) || CGRectIsInfinite(clip) ? Ra::Bounds::huge() : RaCG::BoundsFromCGRect(clip);
    _scene.addPath(p,
                   RaCG::transformFromCG(ctm),
                   RaCG::colorantFromCG(color),
                   width,
                   flags,
                   & clipBounds);
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
- (CGAffineTransform)addPdfFromData:(NSData *)data pageNumber:(NSInteger)pageNumber {
    return RaCG::CGFromTransform(RasterizerPDF::addPdfToScene(data.bytes, data.length, pageNumber, _scene));
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

- (void)addScene:(RAScene *)scene ctm:(CGAffineTransform)ctm clip:(CGRect)clip {
    Ra::Bounds clipBounds = CGRectIsNull(clip) || CGRectIsEmpty(clip) || CGRectIsInfinite(clip) ? Ra::Bounds::huge() : RaCG::BoundsFromCGRect(clip);
    _list.addScene(scene.scene, RaCG::transformFromCG(ctm), clipBounds);
}

@end
