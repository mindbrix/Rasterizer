//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//
//  This software is provided 'as-is', without any express or implied
//  warranty. In no event will the authors be held liable for any damages
//  arising from the use of this software.
//
//  Permission is granted to anyone to use this software for personal use
//  (for a commercial licence please contact the author), and to alter it and
//  redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
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
