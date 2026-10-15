//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//  SPDX-License-Identifier: MIT
//

#import "RasterizerAPI.h"

#if TARGET_OS_OSX
#import <Cocoa/Cocoa.h>
@interface RasterizerView : NSView
#elif TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
@interface RasterizerView : UIView
#endif
@property(weak) id <RASceneListDelegate> listDelegate;
@property(nonatomic) bool useCG;
@end
