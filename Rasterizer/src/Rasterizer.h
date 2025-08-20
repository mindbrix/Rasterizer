//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//
//  This software is provided 'as-is', without any express or implied
//  warranty. In no event will the authors be held liable for any damages
//  arising from the use of this software.
//
//  Permission is granted to anyone to use this software for any purpose,
//  including commercial applications, and to alter it and redistribute it
//  freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
//

#define kTau 6.283185307179586f
#define kCubicPrecision 0.25f
#define kCubicMultiplier 10.3923048454f      // 18/sqrt(3);
#define kCoverScale 2047.9375f
#define kQuadraticFlatness 1e-2f
#define kFatMask 0xFFFFFFF0
#define kfh 16.f
#define krfh 0.0625f
#define kStripHeight 8.f
#define kStripCount 8
#define kMoleculesHeight 256
#define kMoleculesRange 32767.f
#define kFastSegments 4
#define kNullIndex 0xFFFF
#define kPathIndexMask 0xFFFFFF
#define kMinUpperDet 16.f
#define kPageSize 4096
#define kMiterLimit 1.5
#define kCubicSolverLimit 5e-2f
#define kDepthRange 0.1f
