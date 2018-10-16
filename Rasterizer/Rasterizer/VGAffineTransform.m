//
//  VGAffineTransform.m
//  Rasterizer
//
//  Created by Nigel Barber on 02/05/2014.
//  Copyright (c) 2014 Nigel Barber (@mindbrix). All rights reserved.
//

#import "VGAffineTransform.h"


#pragma mark - Static C helpers

static inline CGAffineTransform makeCTM(CGFloat angle, CGFloat s, CGPoint t, CGPoint anchorPoint) {
    CGFloat sine = sinf(angle), cosine = cosf(angle);
    CGAffineTransform ctm = CGAffineTransformMakeTranslation(anchorPoint.x, anchorPoint.y);
    ctm = CGAffineTransformConcat(CGAffineTransformMake(cosine * s, sine * s, -sine * s, cosine * s, t.x, t.y), ctm);
    ctm = CGAffineTransformTranslate(ctm , -anchorPoint.x, -anchorPoint.y);
    return ctm;
}


#pragma mark - Lifecycle

@implementation VGAffineTransform

-(instancetype)init {
    self = [super init];
    
    if (self) {
        [self reset];
    }
    return self;
}

-(instancetype)initWithTransform:(CGAffineTransform)affineTransform {
    self = [super init];
    
    if (self) {
        _affineTransform = affineTransform;
        self.anchorPoint = CGPointZero;
    }
    return self;
}

-(instancetype)initWithRotation:(CGFloat)rotation scale:(CGFloat)scale translation:(CGPoint)translation anchorPoint:(CGPoint)anchorPoint {
    self = [super init];
    
    if (self) {
        _affineTransform = makeCTM(_rotation, _scale, _translation, CGPointZero);
        self.anchorPoint = anchorPoint;
    }
    return self;
}


#pragma mark - NSCoding

-(void)encodeWithCoder:(NSCoder *)aCoder {
    [aCoder encodeDouble:self.rotation forKey:@"rotation"];
    [aCoder encodeDouble:self.scale forKey:@"scale"];
    [aCoder encodeObject:[NSValue valueWithBytes:& _translation objCType:@encode(CGPoint)] forKey:@"translation"];
    [aCoder encodeObject:[NSValue valueWithBytes:& _anchorPoint objCType:@encode(CGPoint)] forKey:@"anchorPoint"];
}

-(instancetype)initWithCoder:(NSCoder *)aDecoder {
    CGFloat rotation = [aDecoder decodeDoubleForKey:@"rotation"];
    CGFloat scale = [aDecoder decodeDoubleForKey:@"scale"];
    CGPoint anchorPoint = CGPointZero;
    CGPoint translation = CGPointZero;
    
    [[aDecoder decodeObjectForKey:@"translation"] getValue:& translation];
    [[aDecoder decodeObjectForKey:@"anchorPoint"] getValue:& anchorPoint];
    
    return [self initWithRotation:rotation scale:scale translation:translation anchorPoint:anchorPoint];
}

     
#pragma mark - NSCopying

-(instancetype)copyWithZone:(NSZone *)zone {
    return [[VGAffineTransform allocWithZone:zone] initWithRotation:_rotation scale:_scale translation:_translation anchorPoint:_anchorPoint];
}


#pragma mark - Equality

-(BOOL)isEqualToTransform:(VGAffineTransform *)aTransform {
    return CGPointEqualToPoint(_anchorPoint, aTransform.anchorPoint) && CGAffineTransformEqualToTransform(_affineTransform, aTransform.affineTransform);
}


#pragma mark - Properties

-(void)setAnchorPoint:(CGPoint)anchorPoint {
    _anchorPoint = CGPointApplyAffineTransform(anchorPoint, CGAffineTransformInvert(_affineTransform));
    _rotation = -atan2f(_affineTransform.c, _affineTransform.d);
    _scale = sqrt(fabs(_affineTransform.a * _affineTransform.d - _affineTransform.b * _affineTransform.c));
    _translation = CGPointMake(anchorPoint.x - _anchorPoint.x, anchorPoint.y - _anchorPoint.y);
    _affineTransform = makeCTM(_rotation, _scale, _translation, _anchorPoint);
}

-(void)setRotation:(CGFloat)rotation {
    _rotation = rotation;
    _affineTransform = makeCTM(_rotation, _scale, _translation, _anchorPoint);
}

-(void)setScale:(CGFloat)scale {
    _scale = scale;
    _affineTransform = makeCTM(_rotation, _scale, _translation, _anchorPoint);
}

-(void)setTranslation:(CGPoint)translation {
    _translation = translation;
    _affineTransform = makeCTM(_rotation, _scale, _translation, _anchorPoint);
}


#pragma mark - Convenience methods

-(void)reset {
    _anchorPoint = CGPointZero;
    _affineTransform = CGAffineTransformIdentity;
    
    _rotation = 0.0f;
    _scale = 1.0f;
    _translation = CGPointZero;
}

-(void)rotateBy:(CGFloat)turn bounds:(CGRect)bounds {
    [self rotateTo:self.rotation + turn bounds:bounds];
}

- (void)rotateSteppedBy:(CGFloat)turn bounds:(CGRect)bounds {
    CGFloat rotation = self.rotation + turn;
    rotation = ceilf(rotation / turn) * turn;
    [self rotateTo:rotation bounds:bounds];
}

-(void)rotateTo:(CGFloat)rotation bounds:(CGRect)bounds {
    CGPoint centre = CGPointMake(CGRectGetMidX(bounds), CGRectGetMidY(bounds));
    
    self.anchorPoint = centre;
    self.rotation = rotation;
}

-(void)scaleBy:(CGFloat)scaleFactor bounds:(CGRect)bounds {
    CGPoint centre = CGPointMake(CGRectGetMidX(bounds), CGRectGetMidY(bounds));
    
    self.anchorPoint = centre;
    self.scale *= scaleFactor;
}

-(void)translateByX:(CGFloat)x andY:(CGFloat)y {
    self.translation = CGPointMake(self.translation.x + x, self.translation.y + y);
}


#pragma mark - Debug

-(NSString *)description {
    return [NSString stringWithFormat:@"anchorPoint = (%g, %g), rotation = %g, scale = %g, translation = (%g, %g)", _anchorPoint.x, _anchorPoint.y, _rotation, _scale, _translation.x, _translation.y];
}

@end
