//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//  SPDX-License-Identifier: MIT
//

#import <QuartzCore/QuartzCore.h>
#import "RasterizerRenderer.hpp"


@protocol LayerDelegate <NSObject>
- (void)writeBuffer:(RenderBuffer *)buffer forLayer:(CAMetalLayer *)layer;
@end


@interface RasterizerLayer : CAMetalLayer
@property(weak) id <LayerDelegate> layerDelegate;
@end
