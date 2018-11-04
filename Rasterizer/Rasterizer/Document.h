//
//  Document.h
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "RasterizerView.h"

@interface Document : NSDocument

@property(nonatomic, strong) IBOutlet RasterizerView *view;

@end

