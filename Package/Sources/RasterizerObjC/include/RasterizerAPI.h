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
- (nonnull id)initWithGray:(double)gray alpha:(double)alpha;
- (nonnull id)initWithHue:(double)hue saturation:(double)saturation value:(double)value alpha:(double)alpha;
- (nonnull id)initWithRed:(double)red green:(double)green blue:(double)blue alpha:(double)alpha;
- (nonnull id)initWithCGColor:(nonnull CGColorRef)cgColor;

- (nonnull id)initLinearWithColors:(nonnull NSArray<RAColor *>*)colors
                 locations:(nonnull NSArray<NSNumber *>*)locations
                     start:(CGPoint)start
                       end:(CGPoint)end;

- (nonnull id)initRadialWithColors:(nonnull NSArray<RAColor *>*)colors
                 locations:(nonnull NSArray<NSNumber *>*)locations
                    center:(CGPoint)center
                    radius:(double)radius;
@end


@interface RAPath: NSObject
@property(nonatomic, readonly) CGRect bounds;

- (nonnull id)initWithCGPath:(nonnull CGPathRef)cgPath;
- (nonnull id)initWithRect:(CGRect)rect;
- (nonnull id)initWithEllipse:(CGRect)rect;
- (void)moveTo:(double)x y:(double)y;
- (void)lineTo:(double)x y:(double)y;
- (void)quadTo:(double)x1 y1:(double)y1 x2:(double)x2 y2:(double)y2;
- (void)cubicTo:(double)x1 y1:(double)y1 x2:(double)x2 y2:(double)y2 x3:(double)x3 y3:(double)y3;
- (void)close;
- (void)addCGPath:(nonnull CGPathRef)path;
- (void)addRect:(CGRect)rect;
- (void)addEllipse:(CGRect)rect;
- (nonnull RAPath *)dashedCopyWithPhase:(double)phase
                        lengths:(nonnull NSArray<NSNumber *>*)lengths;
@end


typedef NS_ENUM(NSUInteger, RACapStyle) {
    kCapButt = 0, kCapSquare, kCapRound
};

typedef NS_ENUM(NSUInteger, RAJoinStyle) {
    kJoinMiter = 0, kJoinRound
};

@interface RAScene: NSObject
@property(nonatomic, readonly) CGRect bounds;

- (void)addFill:(nonnull RAPath *)path
            ctm:(CGAffineTransform)ctm
           color:(nonnull RAColor *)color
        evenOdd:(BOOL)evenOdd;

- (void)addFill:(nonnull RAPath *)path
            ctm:(CGAffineTransform)ctm
           color:(nonnull RAColor *)color
        evenOdd:(BOOL)evenOdd
           clip:(CGRect)clip
       clipPath:(nullable RAPath *)clipPath;

- (void)addStroke:(nonnull RAPath *)path
              ctm:(CGAffineTransform)ctm
            color:(nonnull RAColor *)color
            width:(double)width
         capStyle:(RACapStyle)capStyle
        joinStyle:(RAJoinStyle)joinStyle;

- (void)addStroke:(nonnull RAPath *)path
              ctm:(CGAffineTransform)ctm
            color:(nonnull RAColor *)color
            width:(double)width
         capStyle:(RACapStyle)capStyle
        joinStyle:(RAJoinStyle)joinStyle
             clip:(CGRect)clip
         clipPath:(nullable RAPath *)clipPath;

- (CGRect)addTextLine:(nonnull NSAttributedString *)string
                  ctm:(CGAffineTransform)ctm
                 clip:(CGRect)clip;

- (CGRect)addText:(nonnull NSAttributedString *)string
           inRect:(CGRect)rect
              ctm:(CGAffineTransform)ctm
             clip:(CGRect)clip;

- (CGAffineTransform)addSvgFromData:(nonnull NSData *)data;
@end


@interface RASceneList: NSObject
@property(nonatomic, nonnull) RAPath *clipPath;
@property(nonatomic) CGAffineTransform clipCtm;
@property(nonatomic, readonly) CGRect bounds;
@property(nonatomic) CGAffineTransform ctm;
@property(nonatomic) BOOL useCurves;
@property(nonatomic) BOOL showOpaques;
@property(nonatomic) BOOL showOutlines;

- (void)addList:(nonnull RASceneList *)list;
- (void)addScene:(nonnull RAScene *)scene ctm:(CGAffineTransform)ctm clip:(CGRect)clip;
@end


@protocol RASceneListDelegate <NSObject>
- (BOOL)shouldRedrawAtTime:(double)time;
- (nonnull RASceneList *)getListAtTime:(double)time width:(double)width height:(double)height;
@end
