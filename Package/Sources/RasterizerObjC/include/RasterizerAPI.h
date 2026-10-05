//
//  RasterizerAPI.h
//  Rasterizer
//
//  Created by Nigel Barber on 03/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>

@interface RAPaint: NSObject
- (nonnull id)initWithGray:(double)gray alpha:(double)alpha;
- (nonnull id)initWithHue:(double)hue saturation:(double)saturation value:(double)value alpha:(double)alpha;
- (nonnull id)initWithRed:(double)red green:(double)green blue:(double)blue alpha:(double)alpha;
- (nonnull id)initWithCGColor:(nonnull CGColorRef)cgColor;

- (nonnull id)initLinear:(nonnull NSArray<RAPaint *>*)colors
               locations:(nonnull NSArray<NSNumber *>*)locations
                   start:(CGPoint)start
                     end:(CGPoint)end;

- (nonnull id)initRadial:(nonnull NSArray<RAPaint *>*)colors
               locations:(nonnull NSArray<NSNumber *>*)locations
                  center:(CGPoint)center
                  radius:(double)radius;

- (nonnull id)initWithCGImage:(nonnull CGImageRef)cgImage;
@end


@interface RAPath: NSObject
@property(nonatomic, readonly) CGRect bounds;

- (nonnull id)initWithCGPath:(nonnull CGPathRef)cgPath;
- (nonnull id)initWithRect:(CGRect)rect;
- (nonnull id)initWithEllipse:(CGRect)rect;
- (nonnull id)initWithRoundedRect:(CGRect)rect
                      cornerWidth:(double)cornerWidth
                     cornerHeight:(double)cornerHeight;
- (void)moveTo:(double)x y:(double)y;
- (void)lineTo:(double)x y:(double)y;
- (void)quadTo:(double)x1 y1:(double)y1 x2:(double)x2 y2:(double)y2;
- (void)cubicTo:(double)x1 y1:(double)y1 x2:(double)x2 y2:(double)y2 x3:(double)x3 y3:(double)y3;
- (void)close;
- (void)addCGPath:(nonnull CGPathRef)path;
- (void)addRect:(CGRect)rect;
- (void)addEllipse:(CGRect)rect;
- (void)addRoundedRect:(CGRect)rect
           cornerWidth:(double)cornerWidth
          cornerHeight:(double)cornerHeight;

- (nonnull RAPath *)dashedCopyWithPhase:(double)phase
                                lengths:(nonnull NSArray<NSNumber *>*)lengths;
@end


typedef void (^CTRunApplyBlock)(NSRange range, CGRect bounds);
typedef void (^CTLineApplyBlock)(CTLineRef _Nonnull, CGPoint origin);

@interface RAFrame: NSObject
+ (double)lineHeightForFont:(nonnull NSString *)named size:(double)size;

- (nonnull id)initWithAttributedString:(nonnull NSAttributedString *)attributedString inRect:(CGRect)rect;
- (void)applyRuns:(nonnull CTRunApplyBlock)block;
- (void)applyLines:(nonnull CTLineApplyBlock)block;
@end


typedef NS_ENUM(NSUInteger, RACapStyle) {
    kCapButt = 0, kCapSquare, kCapRound
};

typedef NS_ENUM(NSUInteger, RAJoinStyle) {
    kJoinMiter = 0, kJoinRound
};

// A draw, edited as a whole: read one from a scene, change it, then replace it with -[RAScene setDraw:atIndex:]
@interface RADraw: NSObject
@property(nonnull, nonatomic) RAPath *path;
@property(nonatomic) CGAffineTransform ctm;
@property(nonnull, nonatomic) RAPaint *color;
@property(nonatomic) double width;      // 0 fills, > 0 strokes
@property(nonatomic) BOOL evenOdd;
@property(nonatomic) RACapStyle capStyle;
@property(nonatomic) RAJoinStyle joinStyle;
@property(nonatomic) CGRect clip;       // Empty, null or infinite clips nothing
@property(nullable, nonatomic) RAPath *clipPath;
@property(nonatomic) BOOL hidden;       // Hidden draws keep their index, but are not drawn

- (nonnull id)initWithPath:(nonnull RAPath *)path ctm:(CGAffineTransform)ctm color:(nonnull RAPaint *)color;
@end


@interface RAScene: NSObject
@property(nonatomic, readonly) CGRect bounds;
@property(nonatomic, readonly) NSUInteger count;

// Whole-draw edits. Draws are indexed in the order they were added. Scenes only grow: hide draws instead of removing them.
- (nonnull RADraw *)drawAtIndex:(NSUInteger)index;
- (void)setDraw:(nonnull RADraw *)draw atIndex:(NSUInteger)index;
- (void)addDraw:(nonnull RADraw *)draw;
// Appends copies of a scene's draws, sharing its geometry. The scene may be this one.
// Copying from a prepared (rendered) scene into a scene with no pending additions needs no hashing, matching or derivation.
// The source is never prepared by copying, so an unrendered source, e.g. a parsed file, holds no cache references.
- (void)addDrawsFromScene:(nonnull RAScene *)scene;
- (void)addDrawsFromScene:(nonnull RAScene *)scene range:(NSRange)range;

// Test support: prepares the scene and checks its incremental state, or checks the global geometry cache.
// Each returns nil, or a description of the first broken invariant.
- (nullable NSString *)validate;
+ (nullable NSString *)validateCache;
// Test support: geometry cache statistics, including the fragmentation of its P16 & outline storage: bytes used, wasted by size
// class rounding, and free in size class free lists, within the end & capacity of each
+ (nonnull NSDictionary<NSString *, NSNumber *> *)cacheStatistics;

- (void)addFill:(nonnull RAPath *)path
            ctm:(CGAffineTransform)ctm
           color:(nonnull RAPaint *)color
        evenOdd:(BOOL)evenOdd;

- (void)addStroke:(nonnull RAPath *)path
              ctm:(CGAffineTransform)ctm
            color:(nonnull RAPaint *)color
            width:(double)width
         capStyle:(RACapStyle)capStyle
        joinStyle:(RAJoinStyle)joinStyle;

- (void)addImage:(nonnull RAPaint *)image
             ctm:(CGAffineTransform)ctm;

- (void)addFill:(nonnull RAPath *)path
            ctm:(CGAffineTransform)ctm
           color:(nonnull RAPaint *)color
        evenOdd:(BOOL)evenOdd
           clip:(CGRect)clip
       clipPath:(nullable RAPath *)clipPath;

- (void)addStroke:(nonnull RAPath *)path
              ctm:(CGAffineTransform)ctm
            color:(nonnull RAPaint *)color
            width:(double)width
         capStyle:(RACapStyle)capStyle
        joinStyle:(RAJoinStyle)joinStyle
             clip:(CGRect)clip
         clipPath:(nullable RAPath *)clipPath;

- (CGRect)addFrame:(nonnull RAFrame *)frame
          excludes:(nonnull NSArray<NSValue *> *)excludes
               ctm:(CGAffineTransform)ctm
              clip:(CGRect)clip;

- (CGRect)addText:(nonnull NSAttributedString *)string
           inRect:(CGRect)rect
              ctm:(CGAffineTransform)ctm
             clip:(CGRect)clip;
- (CGAffineTransform)addSvgFromUrl:(nonnull NSURL *)url;
@end


@interface RASceneList: NSObject
@property(nonatomic, readonly) CGRect bounds;
@property(nonatomic) CGAffineTransform ctm;
@property(nonnull, nonatomic) RAPaint *clearColor;
@property(nonatomic) BOOL useClips;
@property(nonatomic) BOOL useCurves;
@property(nonatomic) BOOL showOpaques;
@property(nonatomic) BOOL showOutlines;

- (nonnull id)initWithScene:(nonnull RAScene *)scene;
- (void)addList:(nonnull RASceneList *)list;
- (void)addScene:(nonnull RAScene *)scene;
- (void)addScene:(nonnull RAScene *)scene ctm:(CGAffineTransform)ctm clip:(CGRect)clip;
@end


@protocol RASceneListDelegate <NSObject>
- (BOOL)shouldRedrawAtTime:(double)time scale:(double)scale width:(double)width height:(double)height;
- (nonnull RASceneList *)getListAtTime:(double)time scale:(double)scale width:(double)width height:(double)height;
@end
