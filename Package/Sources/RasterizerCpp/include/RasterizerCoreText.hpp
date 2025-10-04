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
#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>
#import "Rasterizer.hpp"
#import "RasterizerCG.hpp"


struct RasterizerCoreText {
    static CGColorRef GetCGColor(CFDictionaryRef attributes, CFStringRef platformName, CFStringRef ctName) {
        CGColorRef cgColor = NULL;
        NSColor *nsColor = (__bridge NSColor *)CFDictionaryGetValue(attributes, platformName);
        if (nsColor != nil)
            cgColor = nsColor.CGColor;
        else {
            cgColor = (CGColorRef)CFDictionaryGetValue(attributes, ctName);
        }
        return cgColor;
    }
    
    static void addCTLineToScene(CTLineRef line, CGPoint origin, CGAffineTransform ctm, CGRect clip, Ra::Scene& scene) {
        Ra::Bounds clipBounds = CGRectIsNull(clip) || CGRectIsEmpty(clip) || CGRectIsInfinite(clip) ? Ra::Bounds::huge() : RaCG::BoundsFromCGRect(clip);
        CFArrayRef glyphRuns = CTLineGetGlyphRuns(line);
        for (int i = 0; i < CFArrayGetCount(glyphRuns); i++) {
            CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(glyphRuns, i);
            CFIndex count = CTRunGetGlyphCount(run);
            CGGlyph glyphs[count];
            CTRunGetGlyphs(run, CFRangeMake(0, count), glyphs);
            CGPoint positions[count];
            CTRunGetPositions(run, CFRangeMake(0, count), positions);
            CFDictionaryRef attributes = CTRunGetAttributes(run);
            CTFontRef font = (CTFontRef)CFDictionaryGetValue(attributes, kCTFontAttributeName);
            CGColorRef cgColor = GetCGColor(attributes, CFSTR("NSColor"), kCTForegroundColorAttributeName);
            CGColorRef cgBackgroundColor = GetCGColor(attributes, CFSTR("NSBackgroundColor"), kCTBackgroundColorAttributeName);
            
            if (cgBackgroundColor) {
                CGRect bounds = CTRunGetImageBounds(run, NULL, CFRangeMake(0, 0));
                Ra::Path bgPath;
                bgPath->addBounds(RaCG::BoundsFromCGRect(bounds));
                Ra::Colorant bgColor = RaCG::colorantFromCG(cgBackgroundColor);
                Ra::Transform m = RaCG::transformFromCG(CGAffineTransformTranslate(ctm, origin.x, origin.y));
                scene.addPath(bgPath, m, bgColor, 0, 0, & clipBounds);
            }
            
            Ra::Colorant color = RaCG::colorantFromCG(cgColor);
            for (int j = 0; j < count; j++) {
                CGPathRef cgPath = CTFontCreatePathForGlyph(font, glyphs[j], NULL);
                Ra::Path path;
                RaCG::writeCGPathToPath(cgPath, path);
                Ra::Transform m = RaCG::transformFromCG(CGAffineTransformTranslate(ctm, origin.x + positions[j].x, origin.y + positions[j].y));
                scene.addPath(path, m, color, 0, 0, & clipBounds);
                CGPathRelease(cgPath);
            }
        }
    }
    static void addTextToScene(CFAttributedStringRef string, CGAffineTransform ctm, CGRect clip, Ra::Scene& scene) {
        CTLineRef line = CTLineCreateWithAttributedString(string);
        addCTLineToScene(line, CGPointZero, ctm, clip, scene);
        CFRelease(line);
    }
    
    static void addTextToSceneInRect(CFAttributedStringRef string, CGRect rect, CGAffineTransform ctm, CGRect clip, Ra::Scene& scene) {
        CTFramesetterRef framesetter = CTFramesetterCreateWithAttributedString(string);
        CGPathRef rectPath = CGPathCreateWithRect(rect, NULL);
        CTFrameRef frame = CTFramesetterCreateFrame(framesetter, CFRangeMake(0, 0), rectPath, NULL);
        CFArrayRef lines = CTFrameGetLines(frame);
        CFIndex lineCount = CFArrayGetCount(lines);
        CGPoint origins[lineCount];
        CTFrameGetLineOrigins(frame, CFRangeMake(0, 0), origins);
        for (int i = 0; i < lineCount; i++) {
            CTLineRef line = (CTLineRef)CFArrayGetValueAtIndex(lines, i);
            addCTLineToScene(line, origins[i], ctm, clip, scene);
        }
        CFRelease(frame);
        CGPathRelease(rectPath);
        CFRelease(framesetter);
    }
};
