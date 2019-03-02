//
//  MetalLayer.h
//  Rasterizer
//
//  Created by Nigel Barber on 13/12/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import <QuartzCore/QuartzCore.h>
#import "RasterizerCoreGraphics.hpp"

@protocol LayerDelegate <NSObject>
- (void)writeBuffer:(Rasterizer::Buffer *)buffer backBuffer:(Rasterizer::Buffer *)backBuffer forLayer:(CALayer *)layer;
@end


@interface MetalLayer : CAMetalLayer
@property(weak) id <LayerDelegate> layerDelegate;
@end
