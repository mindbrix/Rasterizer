//
//  RasterizerObjC+Internal.h
//  Rasterizer
//
//  Created by Nigel Barber on 04/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

#import "RasterizerAPI.h"
#import "Rasterizer.hpp"

@interface RasterizerPath ()
@property(nonatomic) Ra::Path path;
@end


@interface RasterizerScene ()
@property(nonatomic) Ra::Scene scene;
@end


@interface RasterizerSceneList ()
@property(nonatomic) Ra::SceneList list;
@end
