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

@property(nonatomic) Ra::SceneList sceneList;
@property(nonatomic) Ra::Transform ctm;
@property(nonatomic) bool useCurves;
@property(nonatomic) bool useFastOutlines;
@property(nonatomic) bool useCG;

@end
