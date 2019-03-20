//
//  RasterizerView.h
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface RasterizerView : NSView

@property(nonatomic, strong) NSData *svgData;
@property(nonatomic) IBOutlet NSTextField *rasterizerLabel;

@end
