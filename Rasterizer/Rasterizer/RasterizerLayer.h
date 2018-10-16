//
//  RasterizerLayer.h
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import <QuartzCore/QuartzCore.h>

@protocol RasterizerLayerDelegate <NSObject>
- (void)writeBitmap:(CGContextRef)ctx forLayer:(CALayer *)layer;
@end


@interface RasterizerLayer : CALayer
@property(assign) CGColorSpaceRef colorSpace;
@property(weak) id <RasterizerLayerDelegate> layerDelegate;
@end
