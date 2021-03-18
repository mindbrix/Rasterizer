//
//  Rasterizer.h
//  Rasterizer
//
//  Created by Nigel Barber on 15/12/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#define kTau 6.283185307179586f
#define kClipMargin 1e-2f
#define kCubicScale 0.25f
#define kCoverScale 2047.9375f
#define kQuadraticFlatness 1e-2f
#define kMonotoneFlatness 1e-2f
#define kFatMask 0xFFFFFFF0
#define kFatHeight 16
#define kfh 16.f
#define krfh 0.0625f
#define kFastHeight 32
#define kMoleculesHeight 128
#define kFastSegments 4
#define kNullIndex 0xFFFF
#define kPathIndexMask 0xFFFFFF
#define kMinUpperDet 16.f
#define kPageSize 4096
#define kMiterLimit 1.5
