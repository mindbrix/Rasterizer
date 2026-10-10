//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//  SPDX-License-Identifier: MIT
//

#import "Document.h"

@interface Document ()
@property(nonatomic, strong) NSURL *pdfUrl;
@property(nonatomic, strong) NSURL *svgUrl;
@end

@implementation Document


- (void)windowControllerDidLoadNib:(NSWindowController *)aController {
    [super windowControllerDidLoadNib:aController];
    
    if (self.svgUrl == nil && self.pdfUrl == nil) {
        NSURL *url = [[NSBundle mainBundle] URLForResource:@"Rasterizer Default" withExtension:@"pdf"];
        self.view.pdfUrl = self.pdfUrl = url;
    } else {
        self.view.pdfUrl = self.pdfUrl;
        self.view.svgUrl = self.svgUrl;
    }
}

- (NSString *)windowNibName {
    return @"Document";
}


- (NSData *)dataOfType:(NSString *)typeName error:(NSError **)outError {
    return nil;
}

- (BOOL)readFromURL:(NSURL *)url ofType:(NSString *)typeName error:(NSError **)outError {
    if ([typeName isEqualToString:@"PDF"])
        self.pdfUrl = url;
    else if ([typeName isEqualToString:@"SVG"])
        self.svgUrl = url;
    return YES;
}

@end
