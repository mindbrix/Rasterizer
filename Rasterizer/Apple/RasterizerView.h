//
//  RasterizerView.h
//  Rasterizer
//
//  Created by Nigel Barber on 29/11/2023.
//  Copyright © 2023 @mindbrix. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "Rasterizer.hpp"

@protocol ListDelegate <NSObject>
- (Ra::DrawList)getList: (float)width height:(float) height;
@end

@interface RasterizerView : NSView

@property(weak) id <ListDelegate> listDelegate;
@property(nonatomic) bool useCG;


@end
