//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//  SPDX-License-Identifier: MIT
//

#import <Cocoa/Cocoa.h>
#import "DemoView.h"

@interface Document : NSDocument

@property(nonatomic, strong) IBOutlet DemoView *view;

@end

