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
    static CGRect boundsForLine(CTLineRef line) {
        return CTLineGetBoundsWithOptions(line, kCTLineBoundsUseOpticalBounds);
    }
    
    static CGRect boundsForString(CFAttributedStringRef string) {
        CTLineRef line = CTLineCreateWithAttributedString(string);
        CGRect bounds = boundsForLine(line);
        CFRelease(line);
        return bounds;
    }
                                     
    static CGRect addTextLineToScene(CFAttributedStringRef string, CGAffineTransform ctm, CGRect clip, Ra::SceneRef& scene) {
        CTLineRef line = CTLineCreateWithAttributedString(string);
        addCTLineToScene(line, ctm, clip, scene);
        CGRect bounds = boundsForLine(line);
        CFRelease(line);
        return bounds;
    }
    
    static CGRect addTextToSceneInRect(CFAttributedStringRef string, CGRect rect, CGAffineTransform ctm, CGRect clip, Ra::SceneRef& scene) {
        Ra::SceneRef glyphs;
        CTFramesetterRef framesetter = CTFramesetterCreateWithAttributedString(string);
        CGPathRef rectPath = CGPathCreateWithRect(rect, NULL);
        CTFrameRef frame = CTFramesetterCreateFrame(framesetter, CFRangeMake(0, 0), rectPath, NULL);
        CFArrayRef lines = CTFrameGetLines(frame);
        CFIndex lineCount = CFArrayGetCount(lines);
        Ra::Vector<CGPoint> origins(lineCount);
        CTFrameGetLineOrigins(frame, CFRangeMake(0, 0), & origins[0]);
        for (int i = 0; i < lineCount; i++) {
            CGAffineTransform m = CGAffineTransformTranslate(ctm,
                rect.origin.x + origins[i].x,
                rect.origin.y + origins[i].y);
            CTLineRef line = (CTLineRef)CFArrayGetValueAtIndex(lines, i);
            addCTLineToScene(line, m, clip, glyphs);
        }
        scene->appendScene(*glyphs.ptr);
        CFRelease(frame);
        CGPathRelease(rectPath);
        CFRelease(framesetter);
        return RaCG::CGRectFromBounds(glyphs->bounds());
    }
    
    static Ra::Bounds addCStringToSceneInRect(const char *string, const char *fontName, float fontSize, Ra::Color color, Ra::Bounds rect, Ra::Transform ctm, Ra::Bounds clip, Ra::SceneRef& scene) {
        CGColorRef cgColor = RaCG::CGColorCreateFromColor(color);
        CFAttributedStringRef attr = createAttributedString(string, fontName, fontSize, cgColor);
        CGRect bounds = addTextToSceneInRect(attr, RaCG::CGRectFromBounds(rect), RaCG::CGFromTransform(ctm), clip.isNull() ? CGRectNull : RaCG::CGRectFromBounds(clip), scene);
        CGColorRelease(cgColor);
        CFRelease(attr);
        return RaCG::BoundsFromCGRect(bounds);
    }

    static void addCTLineToScene(CTLineRef line, CGAffineTransform ctm, CGRect clip, Ra::SceneRef& scene) {
        Ra::Bounds clipBounds = CGRectIsNull(clip) || CGRectIsEmpty(clip) || CGRectIsInfinite(clip) ? Ra::Bounds::huge() : RaCG::BoundsFromCGRect(clip);
        CFArrayRef glyphRuns = CTLineGetGlyphRuns(line);
        for (int i = 0; i < CFArrayGetCount(glyphRuns); i++) {
            CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(glyphRuns, i);
            CFIndex count = CTRunGetGlyphCount(run);
            Ra::Vector<CGPoint> positions(count);
            Ra::Vector<CGGlyph> glyphs(count);
            CTRunGetGlyphs(run, CFRangeMake(0, count), & glyphs[0]);
            CTRunGetPositions(run, CFRangeMake(0, count), & positions[0]);
            CFDictionaryRef attributes = CTRunGetAttributes(run);
            CTFontRef font = (CTFontRef)CFDictionaryGetValue(attributes, kCTFontAttributeName);
            CGColorRef cgColor = GetCGColor(attributes, CFSTR("NSColor"), kCTForegroundColorAttributeName);
            CGColorRef cgBackgroundColor = GetCGColor(attributes, CFSTR("NSBackgroundColor"), kCTBackgroundColorAttributeName);
            
            if (cgBackgroundColor) {
                CGRect bounds = CTRunGetImageBounds(run, NULL, CFRangeMake(0, 0));
                Ra::Path bgPath;
                bgPath->addBounds(RaCG::BoundsFromCGRect(bounds));
                Ra::Color bgColor = RaCG::colorFromCG(cgBackgroundColor);
                Ra::Transform m = RaCG::transformFromCG(ctm);
                scene->addPath(bgPath, m, bgColor, 0, 0, & clipBounds);
            }
            
            Ra::Color color = RaCG::colorFromCG(cgColor);
            for (int j = 0; j < count; j++) {
                CGPathRef cgPath = CTFontCreatePathForGlyph(font, glyphs[j], NULL);
                Ra::Path path;
                RaCG::writeCGPathToPath(cgPath, path);
                Ra::Transform m = RaCG::transformFromCG(CGAffineTransformTranslate(ctm, positions[j].x, positions[j].y));
                scene->addPath(path, m, color, 0, 0, & clipBounds);
                CGPathRelease(cgPath);
            }
        }
    }
    
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
    
    static CFAttributedStringRef createAttributedString(const char *string, const char *fontName, float fontSize, CGColorRef color) {
        CFStringRef cfString = CFStringCreateWithCString(kCFAllocatorDefault, string, kCFStringEncodingUTF8);
        CFStringRef cfFontName = CFStringCreateWithCString(kCFAllocatorDefault, fontName, kCFStringEncodingUTF8);
        CTFontRef ctFont = CTFontCreateWithName(cfFontName, fontSize, NULL);
        const void *keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
        const void *values[] = { ctFont, color };
        CFDictionaryRef attributes = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2, NULL, &kCFTypeDictionaryValueCallBacks);
        CFAttributedStringRef attrString = CFAttributedStringCreate(kCFAllocatorDefault, cfString, attributes);
        CFRelease(attributes);
        CFRelease(ctFont);
        CFRelease(cfString);
        CFRelease(cfFontName);
        return attrString;
    }

    static float fontSizeForLineHeight(const char *fontName, float height) {
        CFStringRef cfFontName = CFStringCreateWithCString(kCFAllocatorDefault, fontName, kCFStringEncodingUTF8);
        CTFontRef ctFont = CTFontCreateWithName(cfFontName, height, NULL);
        CGFloat ascent = CTFontGetAscent(ctFont);
        CGFloat descent = CTFontGetDescent(ctFont);
        CGFloat leading = floor(fmax(0, CTFontGetLeading(ctFont)) + 0.5);
        CGFloat lineHeight = floor(ascent + 0.5) + floor(descent + 0.5) + leading;
        CGFloat ascenderDelta = leading > 0 ? 0 : floor(0.2 * lineHeight + 0.5);
        CGFloat defaultLineHeight = lineHeight + ascenderDelta;
        CFRelease(ctFont);
        CFRelease(cfFontName);
        return height * height / defaultLineHeight;
    }
    
    static Ra::SceneRef writeGlyphGrid(const char *fontName, float lineHeight, Ra::Color color) {
        Ra::SceneRef scene;
        CFStringRef cfFontName = CFStringCreateWithCString(kCFAllocatorDefault, fontName, kCFStringEncodingUTF8);
        CTFontRef ctFont = CTFontCreateWithName(cfFontName, 1, NULL);
        CFIndex glyphCount = CTFontGetGlyphCount(ctFont);
        float scale = fontSizeForLineHeight(fontName, lineHeight);
        
        if (glyphCount) {
            for (int d = ceilf(sqrtf(glyphCount)), glyph = 0; glyph <glyphCount; glyph++) {
                CGPathRef cgPath = CTFontCreatePathForGlyph(ctFont, glyph, NULL);
                Ra::Path path;
                RaCG::writeCGPathToPath(cgPath, path);
                scene->addPath(path, Ra::Transform(scale, 0.f, 0.f, scale, scale * float(glyph % d), scale * float(glyph / d)), color, 0.f, 0);
                CGPathRelease(cgPath);
            }
        }
        CFRelease(ctFont);
        CFRelease(cfFontName);
        return scene;
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

typedef RasterizerCoreText RaCT;
