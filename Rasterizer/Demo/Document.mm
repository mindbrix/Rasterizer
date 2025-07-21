//
//  Copyright 2025 Nigel Timothy Barber
//

#import "Document.h"

@interface Document ()
@property(nonatomic, strong) NSData *pdfData;
@property(nonatomic, strong) NSData *svgData;
@end

@implementation Document


- (void)windowControllerDidLoadNib:(NSWindowController *)aController {
    [super windowControllerDidLoadNib:aController];
    
    if (self.pdfData == nil && self.svgData == nil) {
        NSURL *url = [[NSBundle mainBundle] URLForResource:@"Rasterizer Default" withExtension:@"pdf"];
        NSData *data = [NSData dataWithContentsOfURL:url];
        self.view.pdfData = self.pdfData = data;
    } else {
        self.view.pdfData = self.pdfData;
        self.view.svgData = self.svgData;
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
        self.pdfData = [NSData dataWithContentsOfURL:url];
    else if ([typeName isEqualToString:@"SVG"])
        self.svgData = [NSData dataWithContentsOfURL:url];
    return YES;
}

@end
