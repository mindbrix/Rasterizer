//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//  SPDX-License-Identifier: MIT
//

#import <Cocoa/Cocoa.h>
#import "RasterizerView.h"

@interface DemoView : RasterizerView

@property(nonatomic, strong) NSURL *pdfUrl;
@property(nonatomic, strong) NSURL *svgUrl;

@end
