//
//  RasterizerView.mm
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "RasterizerView.h"
#import "RasterizerLayer.h"

@interface RasterizerView ()

@property(nonatomic) RasterizerLayer *rasterizerLayer;

@end

@implementation RasterizerView

- (nullable instancetype)initWithCoder:(NSCoder *)decoder {
    self = [super initWithCoder:decoder];
    if (! self)
        return nil;
    
    [self setWantsLayer:YES];
    [self setLayer:[RasterizerLayer layer]];
    self.layer.backgroundColor = CGColorGetConstantColor(kCGColorWhite);
    return self;
}


- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];
    
    // Drawing code here.
}

#pragma mark - Properties

-(RasterizerLayer *)rasterizerLayer {
    return (RasterizerLayer *)self.layer;
}

@end
