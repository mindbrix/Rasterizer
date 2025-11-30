//
//  RasterizerAPI+Internal.h
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//
#import "RasterizerAPI.h"
#import "Rasterizer.hpp"


@interface RAPaint ()
@property(nonatomic) Ra::Paint paint;
@end


@interface RAPath ()
@property(nonatomic) Ra::Path path;
@end


@interface RAScene ()
@property(nonatomic) Ra::SceneRef scene;
@end


@interface RASceneList ()
@property(nonatomic) Ra::SceneList list;
@end
