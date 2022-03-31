//
//  MetalLayer.h
//  Rasterizer
//
//  Created by Nigel Barber on 13/12/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import <QuartzCore/QuartzCore.h>
#import "RasterizerCG.hpp"

@protocol LayerDelegate <NSObject>
- (CGColorSpaceRef)writeBuffer:(Ra::Buffer *)buffer forLayer:(CALayer *)layer;
@end


@interface RasterizerLayer : CAMetalLayer
@property(weak) id <LayerDelegate> layerDelegate;
@end
