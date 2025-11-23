//
//  RasterizerAPI.h
//  Rasterizer
//
//  Created by Nigel Barber on 03/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>


@interface RAColor: NSObject
- (id)initWithGray:(double)gray alpha:(double)alpha;
- (id)initWithHue:(double)hue saturation:(double)saturation value:(double)value alpha:(double)alpha;
- (id)initWithRed:(double)red green:(double)green blue:(double)blue alpha:(double)alpha;
- (id)initWithCGColor:(CGColorRef)cgColor;

- (id)initLinearWithColors:(NSArray<RAColor *>*)colors
                 locations:(NSArray<NSNumber *>*)locations
                     start:(CGPoint)start
                       end:(CGPoint)end;

- (id)initRadialWithColors:(NSArray<RAColor *>*)colors
                 locations:(NSArray<NSNumber *>*)locations
                    center:(CGPoint)center
                    radius:(double)radius;
@end


@interface RAPath: NSObject
@property(nonatomic, readonly) CGRect bounds;

- (id)initWithCGPath:(CGPathRef)cgPath;
- (id)initWithRect:(CGRect)rect;
- (id)initWithEllipse:(CGRect)rect;
- (void)moveTo:(double)x y:(double)y;
- (void)lineTo:(double)x y:(double)y;
- (void)quadTo:(double)x1 y1:(double)y1 x2:(double)x2 y2:(double)y2;
- (void)cubicTo:(double)x1 y1:(double)y1 x2:(double)x2 y2:(double)y2 x3:(double)x3 y3:(double)y3;
- (void)close;
- (void)addCGPath:(CGPathRef)path;
- (void)addRect:(CGRect)rect;
- (void)addEllipse:(CGRect)rect;
- (RAPath *)dashedCopyWithPhase:(double)phase
                        lengths:(NSArray<NSNumber *>*)lengths;
@end


typedef NS_ENUM(NSUInteger, RACapStyle) {
    kCapButt = 0, kCapSquare, kCapRound
};

typedef NS_ENUM(NSUInteger, RAJoinStyle) {
    kJoinMiter = 0, kJoinRound
};

@interface RAScene: NSObject
@property(nonatomic, readonly) CGRect bounds;

- (void)addFill:(RAPath *)path
            ctm:(CGAffineTransform)ctm
           color:(RAColor *)color
        evenOdd:(BOOL)evenOdd
           clip:(CGRect)clip;

- (void)addStroke:(RAPath *)path
              ctm:(CGAffineTransform)ctm
            color:(RAColor *)color
            width:(double)width
         capStyle:(RACapStyle)capStyle
        joinStyle:(RAJoinStyle)joinStyle
             clip:(CGRect)clip;

- (CGRect)addTextLine:(NSAttributedString *)string
                  ctm:(CGAffineTransform)ctm
                 clip:(CGRect)clip;

- (CGRect)addText:(NSAttributedString *)string
           inRect:(CGRect)rect
              ctm:(CGAffineTransform)ctm
             clip:(CGRect)clip;

- (CGAffineTransform)addSvgFromData:(NSData *)data;
@end


@interface RASceneList: NSObject
@property(nonatomic) RAPath *clipPath;
@property(nonatomic) CGAffineTransform clipCtm;
@property(nonatomic, readonly) CGRect bounds;
@property(nonatomic) CGAffineTransform ctm;
@property(nonatomic) BOOL useCurves;
@property(nonatomic) BOOL showOpaques;
@property(nonatomic) BOOL showOutlines;

- (void)addList:(RASceneList *)list;
- (void)addScene:(RAScene *)scene ctm:(CGAffineTransform)ctm clip:(CGRect)clip;
@end


@protocol RASceneListDelegate <NSObject>
- (BOOL)shouldRedrawAtTime:(double)time;
- (RASceneList *)getListAtTime:(double)time width:(double)width height:(double)height;
@end
