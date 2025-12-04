//
//  RasterizerUtilities.h
//  Rasterizer
//
//  Created by Nigel Barber on 27/09/2025.
//  Copyright © 2025 @mindbrix. All rights reserved.
//
#import <CoreText/CoreText.h>
#import "RasterizerCG.hpp"


struct RasterizerUtilities {
    static void screenGrabToPDF(Ra::SceneList& list, Ra::Bounds bounds) {
        NSArray *downloads = [NSFileManager.defaultManager URLsForDirectory: NSDownloadsDirectory inDomains:NSUserDomainMask];
        NSURL *fileURL = [downloads.firstObject URLByAppendingPathComponent:@"screenGrab.pdf"];
        CGRect mediaBox = RaCG::CGRectFromBounds(bounds);
        CGContextRef ctx = CGPDFContextCreateWithURL((__bridge CFURLRef)fileURL, & mediaBox, NULL);
        CGPDFContextBeginPage(ctx, NULL);
        RaCG::renderList(list, bounds, ctx);
        CGPDFContextEndPage(ctx);
        CGPDFContextClose(ctx);
        CGContextRelease(ctx);
    }
    
    static NSURL *fontURL(NSString *fontName) {
        if (fontName == nil)
            return nil;
        CTFontDescriptorRef fontRef = CTFontDescriptorCreateWithNameAndSize((__bridge CFStringRef)fontName, 1);
        NSURL *URL = (__bridge_transfer NSURL *)CTFontDescriptorCopyAttribute(fontRef, kCTFontURLAttribute);
        CFRelease(fontRef);
        return URL;
    }
};

typedef RasterizerUtilities RaUtils;
