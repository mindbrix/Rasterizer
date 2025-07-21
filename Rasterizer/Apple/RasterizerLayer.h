//
//  Copyright 2025 Nigel Timothy Barber
//

#import <QuartzCore/QuartzCore.h>
#import "RasterizerCG.hpp"


@protocol LayerDelegate <NSObject>
- (CGColorSpaceRef)writeBuffer:(Ra::Buffer *)buffer forLayer:(CALayer *)layer;
@end


@interface RasterizerLayer : CAMetalLayer
@property(weak) id <LayerDelegate> layerDelegate;
@end
