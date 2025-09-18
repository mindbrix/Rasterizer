//
//  RasterizerAPI.h
//  Rasterizer
//
//  Created by Nigel Barber on 03/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>


@interface RasterizerPath: NSObject
@property(nonatomic, readonly) CGRect bounds;

- (id)initWithCGPath:(CGPathRef)cgPath;
- (void)moveTo:(double)x y:(double)y;
- (void)lineTo:(double)x y:(double)y;
- (void)quadTo:(double)x1 y1:(double)y1 x2:(double)x2 y2:(double)y2;
- (void)cubicTo:(double)x1 y1:(double)y1 x2:(double)x2 y2:(double)y2 x3:(double)x3 y3:(double)y3;
- (void)close;
- (void)addCGPath:(CGPathRef)path;
- (void)addRect:(CGRect)rect;
- (void)addEllipse:(CGRect)rect;
@end


@interface RasterizerTypeface: NSObject
- (id)initWithName:(NSString *)name;
@end


typedef NS_ENUM(NSUInteger, SceneFlags) {
    kInvisible = 1 << 0,
    kFillEvenOdd = 1 << 1,
    kRoundCap = 1 << 2,
    kSquareCap = 1 << 3
};

@interface RasterizerScene: NSObject
@property(nonatomic, readonly) CGRect bounds;

- (void)addPath:(RasterizerPath *)path ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags;
- (void)addPath:(RasterizerPath *)path ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags clip:(CGRect)clip;
- (void)addCGPath:(CGPathRef)cgPath ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags;
- (void)addCGPath:(CGPathRef)cgPath ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags clip:(CGRect)clip;
- (void)addText:(NSString *)text typeface:(RasterizerTypeface *)typeface pointSize:(double)pointSize ctm:(CGAffineTransform)ctm color:(CGColorRef)color;
@end

@interface RasterizerSceneList: NSObject
@property(nonatomic, readonly) CGRect bounds;
@property(nonatomic) CGAffineTransform ctm;

- (void)addList:(RasterizerSceneList *)list;
- (void)addScene:(RasterizerScene *)scene ctm:(CGAffineTransform)ctm;
- (void)addScene:(RasterizerScene *)scene ctm:(CGAffineTransform)ctm clip:(CGRect)clip;
@end


@protocol SceneListDelegate <NSObject>
- (BOOL)shouldRedrawAtTime:(double)time;
- (RasterizerSceneList *)getListAtTime:(double)time width:(double)width height:(double)height;
@end
