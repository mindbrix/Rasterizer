//
//  Copyright 2025 Nigel Timothy Barber
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
