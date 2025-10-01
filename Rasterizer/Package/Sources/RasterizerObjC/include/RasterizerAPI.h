//
//  RasterizerAPI.h
//  Rasterizer
//
//  Created by Nigel Barber on 03/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>


@interface RAPath: NSObject
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


typedef NS_ENUM(NSUInteger, RASceneFlags) {
    kInvisible = 1 << 0,
    kFillEvenOdd = 1 << 1,
    kRoundCap = 1 << 2,
    kSquareCap = 1 << 3
};

@interface RAScene: NSObject
@property(nonatomic, readonly) CGRect bounds;

- (void)addPath:(RAPath *)path ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags;
- (void)addPath:(RAPath *)path ctm:(CGAffineTransform)ctm color:(CGColorRef)color width:(double)width flags:(NSUInteger)flags clip:(CGRect)clip;
- (void)addAttributedString:(NSAttributedString *)string ctm:(CGAffineTransform)ctm;
- (CGAffineTransform)addSvgFromData:(NSData *)data;
- (CGAffineTransform)addPdfFromData:(NSData *)data pageNumber:(NSInteger)pageNumber;
@end


@interface RASceneList: NSObject
@property(nonatomic, readonly) CGRect bounds;
@property(nonatomic) CGAffineTransform ctm;

- (void)addList:(RASceneList *)list;
- (void)addScene:(RAScene *)scene ctm:(CGAffineTransform)ctm;
- (void)addScene:(RAScene *)scene ctm:(CGAffineTransform)ctm clip:(CGRect)clip;
@end


@protocol RASceneListDelegate <NSObject>
- (BOOL)shouldRedrawAtTime:(double)time;
- (RASceneList *)getListAtTime:(double)time width:(double)width height:(double)height;
@end
