//
//  TestDocument.h
//  Rasterizer
//
//  Created by Nigel Barber on 08/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "Rasterizer-Swift.h"

NS_ASSUME_NONNULL_BEGIN

@interface TestDocument : NSDocument
@property (weak) IBOutlet SwiftDemoView *view;

@end

NS_ASSUME_NONNULL_END
