//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//  SPDX-License-Identifier: MIT
//

#define kTau 6.283185307179586f
#define kCubicPrecision 0.25f
#define kCubicMultiplier 10.3923048454f      // 18/sqrt(3);
#define kQuadraticFlatness 1e-2f
#define kFatMask 0xFFFFFFF0
#define kfh 16.f
#define krfh 0.0625f
#define kStripHeight 8.f
#define kStripCount 12
#define kMoleculesHeight 256
#define kMoleculesRange 32767.f
#define kMoleculesPixelsPerEdge 256
#define kFastSegments 4
#define kNullIndex 0xFFFF
#define kPathIndexMask 0xFFFFF
#define kMiterLimit -0.866025403784439
#define kCubicSolverLimit 5e-2f
#define kDepthRange 0.1f
#define kColorTextureWidth 64

// Blend modes, as PDF & CSS compositing define them, & in CGBlendMode's order. Draws other than Normal blend with what's beneath
// them, read with framebuffer fetch
enum BlendMode {
    kBlendNormal = 0, kBlendMultiply, kBlendScreen, kBlendOverlay, kBlendDarken, kBlendLighten, kBlendColorDodge, kBlendColorBurn,
    kBlendSoftLight, kBlendHardLight, kBlendDifference, kBlendExclusion, kBlendHue, kBlendSaturation, kBlendColor, kBlendLuminosity,
    kBlendModeCount
};
