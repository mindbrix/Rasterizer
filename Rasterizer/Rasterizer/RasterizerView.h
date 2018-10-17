//
//  RasterizerView.h
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "ZoomableView.h"

@interface RasterizerView : ZoomableView

@property(nonatomic) IBOutlet NSTextField *rasterizerLabel;

- (IBAction)toggleRasterizer:(id)sender;

@end
