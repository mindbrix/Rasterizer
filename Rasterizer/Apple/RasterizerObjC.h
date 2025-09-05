//
//  RasterizerObjC.h
//  Rasterizer
//
//  Created by Nigel Barber on 03/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

#import <CoreGraphics/CoreGraphics.h>


@interface RasterizerPath: NSObject
- (void)moveTo:(float)x y:(float)y;
- (void)lineTo:(float)x y:(float)y;
- (void)quadTo:(float)x1 y1:(float)y1 x2:(float)x2 y2:(float)y2;
- (void)cubicTo:(float)x1 y1:(float)y1 x2:(float)x2 y2:(float)y2 x3:(float)x3 y3:(float)y3;
- (void)close;
- (void)addCGPath:(CGPathRef)path;
- (void)addRect:(CGRect)rect;
- (void)addEllipse:(CGRect)rect;
@end

enum SceneFlags {
    kInvisible = 1 << 0, kFillEvenOdd = 1 << 1, kRoundCap = 1 << 2, kSquareCap = 1 << 3
};

@interface RasterizerScene: NSObject
@property(nonatomic, readonly) CGRect bounds;

- (void)addPath:(RasterizerPath *)path ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(float)width flags:(NSUInteger)flags;
- (void)addCGPath:(CGPathRef)cgPath ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(float)width flags:(NSUInteger)flags;
@end

@interface RasterizerSceneList: NSObject
@property(nonatomic, readonly) CGRect bounds;
@property(nonatomic) CGAffineTransform ctm;

- (void)addList:(RasterizerSceneList *)list;
- (void)addScene:(RasterizerScene *)scene ctm:(CGAffineTransform)ctm;
- (void)addScene:(RasterizerScene *)scene ctm:(CGAffineTransform)ctm clip:(CGRect)clip;
@end


@interface RasterizerObjCTest: NSObject
@property(nonatomic, readonly) RasterizerSceneList *test0;
@end
