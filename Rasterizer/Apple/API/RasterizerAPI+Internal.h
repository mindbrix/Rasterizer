//
//  RasterizerObjC+Internal.h
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

#include "RasterizerC_API.h"
#import "RasterizerAPI.h"
#import "Rasterizer.hpp"
#import "RasterizerFont.hpp"

@interface RAPath ()
@property(nonatomic) RasterizerPath path;
@end


@interface RAFont ()
@property(nonatomic) RasterizerFont font;
@end


@interface RAScene ()
@property(nonatomic) Ra::Scene scene;
@end


@interface RASceneList ()
@property(nonatomic) Ra::SceneList list;
@end
