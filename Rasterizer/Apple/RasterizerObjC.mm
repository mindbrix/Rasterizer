//
//  RasterizerObjC.mm
//  Rasterizer
//
//  Created by Nigel Barber on 03/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "RasterizerObjC+Internal.h"
#import "RasterizerCG.hpp"


#pragma mark - RasterizerPath

@implementation RasterizerPath: NSObject

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

@end


#pragma mark - RasterizerScene

@implementation RasterizerScene: NSObject

- (void)addPath:(RasterizerPath *)path ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(float)width flags:(NSUInteger)flags {
    _scene.addPath(path.path,
                   RaCG::transformFromCG(ctm),
                   RaCG::colorantFromCG(color),
                   width,
                   flags);
}

@end


#pragma mark - RasterizerScene

@implementation RasterizerSceneList: NSObject

- (void)addList:(RasterizerSceneList *)list {
    _list.addList(list.list);
}
- (void)addScene:(RasterizerScene *)scene {
    _list.addScene(scene.scene);
}

@end


#pragma mark - RasterizerObjCTest

@implementation RasterizerObjCTest: NSObject

- (RasterizerSceneList *)test0 {
    RasterizerPath *path = [RasterizerPath new];
    [path addEllipse:CGRectMake(0, 0, 100, 100)];
    
    CGFloat components[] = { 1, 0, 0, 1 };
    CGColorRef color = CGColorCreate(CGColorSpaceCreateDeviceRGB(), components);
    
    RasterizerScene *scene = [RasterizerScene new];
    [scene addPath:path ctm:CGAffineTransformIdentity color:color width:0 flags:0];
    
    RasterizerSceneList *list = [RasterizerSceneList new];
    [list addScene:scene];
    return list;
}

@end
