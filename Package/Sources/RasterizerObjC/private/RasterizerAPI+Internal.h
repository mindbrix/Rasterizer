//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//  SPDX-License-Identifier: MIT
//
#import <CoreText/CoreText.h>
#import "RasterizerAPI.h"
#import "Rasterizer.hpp"


@interface RAPaint ()
@property(nonatomic) Ra::Paint paint;
@end


@interface RAPath ()
@property(nonatomic) Ra::Path path;
@end


@interface RAFrame ()
@property(nonatomic) CTFrameRef frame;
@end

@interface RADraw ()
@property(nonatomic) Ra::Draw draw;
- (void)setTarget:(Ra::Draw *)target;      // Edits *target in place, or the draw's own Ra::Draw if null
@end

@interface RAScene ()
@property(nonatomic) Ra::SceneRef scene;
@end


@interface RASceneList ()
@property(nonatomic) Ra::SceneList list;
@end
