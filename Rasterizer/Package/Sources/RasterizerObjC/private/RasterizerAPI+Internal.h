//
//  RasterizerObjC+Internal.h
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//
#import "RasterizerAPI.h"
#import "Rasterizer.hpp"
#import "RasterizerFreetype.h"


@interface RAPath ()
@property(nonatomic) Ra::Path path;
@end


@interface RAFont ()
@property(nonatomic) RasterizerFreetype freetype;
@end


@interface RAScene ()
@property(nonatomic) Ra::Scene scene;
@end


@interface RASceneList ()
@property(nonatomic) Ra::SceneList list;
@end
