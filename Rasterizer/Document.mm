//
//  Document.m
//  Rasterizer
//
//  Created by Nigel Barber on 16/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "Document.h"

@interface Document ()
@property(nonatomic, strong) NSData *pdfData;
@property(nonatomic, strong) NSData *svgData;
@end

@implementation Document

- (instancetype)init {
    self = [super init];
    if (self) {
        // Add your subclass-specific initialization here.
    }
    return self;
}

- (void)windowControllerDidLoadNib:(NSWindowController *)aController {
    [super windowControllerDidLoadNib:aController];
    self.view.pdfData = self.pdfData;
    self.view.svgData = self.svgData;
}

- (NSString *)windowNibName {
    // Override returning the nib file name of the document
    // If you need to use a subclass of NSWindowController or if your document supports multiple NSWindowControllers, you should remove this method and override -makeWindowControllers instead.
    return @"Document";
}


- (NSData *)dataOfType:(NSString *)typeName error:(NSError **)outError {
    return nil;
}

- (BOOL)readFromURL:(NSURL *)url ofType:(NSString *)typeName error:(NSError **)outError {
    if ([typeName isEqualToString:@"PDF"])
        self.pdfData = [NSData dataWithContentsOfURL:url];
    else if ([typeName isEqualToString:@"SVG"])
        self.svgData = [NSData dataWithContentsOfURL:url];
    return YES;
}

@end
