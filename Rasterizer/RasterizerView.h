//
//  RasterizerView.h
//  Rasterizer
//
//  Created by Nigel Barber on 29/11/2023.
//  Copyright © 2023 @mindbrix. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "Rasterizer.hpp"

@interface RasterizerView : NSView

@property(nonatomic) bool useCG;

- (void)drawList: (Ra::SceneList)sceneList ctm:(Ra::Transform) ctm useCurves:(bool) useCurves;

@end
