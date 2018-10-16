//
//  VGAffineTransform.h
//  Rasterizer
//
//  Created by Nigel Barber on 02/05/2014.
//  Copyright (c) 2014 Nigel Barber (@mindbrix). All rights reserved.
//

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

static inline CGFloat degrees(CGFloat radians) {
    return radians / M_PI * 180.0f;
}

static inline CGFloat radians(CGFloat degrees) {
    return degrees / 180.0f * M_PI;
}

@interface VGAffineTransform : NSObject<NSCopying, NSCoding>

@property(nonatomic, readonly) CGAffineTransform affineTransform;

@property(nonatomic, assign) CGPoint anchorPoint;
@property(nonatomic, assign) CGFloat rotation;
@property(nonatomic, assign) CGFloat scale;
@property(nonatomic, assign) CGPoint translation;

-(instancetype)initWithTransform:(CGAffineTransform)affineTransform;
-(instancetype)initWithRotation:(CGFloat)rotation scale:(CGFloat)scale translation:(CGPoint)translation anchorPoint:(CGPoint)anchorPoint;

-(BOOL)isEqualToTransform:(VGAffineTransform *)aTransform;

-(void)reset;
-(void)rotateBy:(CGFloat)rotation bounds:(CGRect)bounds;
-(void)rotateSteppedBy:(CGFloat)turn bounds:(CGRect)bounds;
-(void)rotateTo:(CGFloat)rotation bounds:(CGRect)bounds;
-(void)scaleBy:(CGFloat)scaleFactor bounds:(CGRect)bounds;
-(void)translateByX:(CGFloat)x andY:(CGFloat)y;


@end
