//
//  RasterizerAPI+Internal.h
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
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


@interface RALine ()
@property(nonatomic) CTLineRef line;
@end

@interface RAFrame ()
@property(nonatomic) CTFrameRef frame;
@end

@interface RAScene ()
@property(nonatomic) Ra::SceneRef scene;
@end


@interface RASceneList ()
@property(nonatomic) Ra::SceneList list;
@end
