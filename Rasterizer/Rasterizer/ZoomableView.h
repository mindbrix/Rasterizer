//
//  ZoomableView.h
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import <Cocoa/Cocoa.h>


@interface ZoomableView : NSView

@property(nonatomic) CGAffineTransform CTM;
- (void)toggleTimer;

@end
