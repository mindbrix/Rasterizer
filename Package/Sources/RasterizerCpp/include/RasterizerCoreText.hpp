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

typedef std::map<CFIndex, Ra::Path> GlyphCache;

struct RasterizerCoreText {
    static Ra::Bounds addCStringToSceneInRect(const char *string, const char *fontName, float fontSize, Ra::Color color, Ra::Bounds rect, Ra::Transform ctm, Ra::Bounds clip, Ra::SceneRef& scene) {
        CGColorRef cgColor = RaCG::CGColorCreateFromColor(color);
        CFStringRef cfString = CFStringCreateWithCString(kCFAllocatorDefault, string, kCFStringEncodingUTF8);
        CFDictionaryRef attributes = createAttributes(fontName, fontSize, cgColor);
        CFAttributedStringRef attr = CFAttributedStringCreate(kCFAllocatorDefault, cfString, attributes);
        GlyphCache cache;
        CGRect bounds = addTextToSceneInRect(attr, RaCG::CGRectFromBounds(rect), RaCG::CGFromTransform(ctm), clip.isNull() ? CGRectNull : RaCG::CGRectFromBounds(clip), scene, cache);
        CGColorRelease(cgColor);
        CFRelease(cfString);
        CFRelease(attributes);
        CFRelease(attr);
        return RaCG::BoundsFromCGRect(bounds);
    }

    static CGRect addTextToSceneInRect(CFAttributedStringRef string, CGRect rect, CGAffineTransform ctm, CGRect clip, Ra::SceneRef& scene, GlyphCache& cache) {
        CTFramesetterRef framesetter = CTFramesetterCreateWithAttributedString(string);
        CGPathRef rectPath = CGPathCreateWithRect(rect, NULL);
        CTFrameRef frame = CTFramesetterCreateFrame(framesetter, CFRangeMake(0, 0), rectPath, NULL);
        CGRect bounds = addFrameToScene(frame, NULL, ctm, clip, scene, cache);
        CFRelease(frame);
        CGPathRelease(rectPath);
        CFRelease(framesetter);
        return bounds;
    }
    
    static CGRect addFrameToScene(CTFrameRef frame, NSArray<NSValue *> *excludes, CGAffineTransform ctm, CGRect clip, Ra::SceneRef& scene, GlyphCache& cache) {
        CGRect bounds = CGRectNull;
        CGRect rect = CGPathGetBoundingBox(CTFrameGetPath(frame));
        CFArrayRef lines = CTFrameGetLines(frame);
        CFIndex lineCount = CFArrayGetCount(lines);
        Ra::Vector<CGPoint> origins(lineCount);
        CTFrameGetLineOrigins(frame, CFRangeMake(0, 0), & origins[0]);
        for (int i = 0; i < lineCount; i++) {
            CGFloat tx = rect.origin.x + origins[i].x;
            CGFloat ty = rect.origin.y + origins[i].y;
            CGAffineTransform m = CGAffineTransformTranslate(ctm, tx, ty);
            CTLineRef line = (CTLineRef)CFArrayGetValueAtIndex(lines, i);
            CGRect runBounds = addCTLineToScene(line, excludes, m, clip, scene, cache);
            bounds = CGRectUnion(bounds, CGRectApplyAffineTransform(runBounds, CGAffineTransformMakeTranslation(tx, ty)));
        }
        return bounds;
    }
    
    static CGRect addCTLineToScene(CTLineRef line, NSArray<NSValue *> *excludes, CGAffineTransform ctm, CGRect clip, Ra::SceneRef& scene, GlyphCache& cache) {
        Ra::Bounds clipBounds = CGRectIsNull(clip) || CGRectIsEmpty(clip) || CGRectIsInfinite(clip) ? Ra::Bounds::huge() : RaCG::BoundsFromCGRect(clip);
        CGRect bounds = CGRectNull;
        CFArrayRef glyphRuns = CTLineGetGlyphRuns(line);
        for (int i = 0; i < CFArrayGetCount(glyphRuns); i++) {
            CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(glyphRuns, i);
            CGRect imageBounds = CTRunGetImageBounds(run, NULL, CFRangeMake(0, 0));
            bounds = CGRectUnion(bounds, imageBounds);
            
            BOOL exclude = false;
            NSUInteger location = CTRunGetStringRange(run).location;
            for (NSValue *value in excludes) {
               NSRange rng = value.rangeValue;
                if (location >= rng.location && location < rng.location + rng.length) {
                    exclude = YES;
                    break;
                }
            }
            if (exclude)
                continue;
            
            CFIndex count = CTRunGetGlyphCount(run);
            Ra::Vector<CGPoint> positions(count);
            Ra::Vector<CGGlyph> glyphs(count);
            CTRunGetGlyphs(run, CFRangeMake(0, count), & glyphs[0]);
            CTRunGetPositions(run, CFRangeMake(0, count), & positions[0]);
            CFDictionaryRef attributes = CTRunGetAttributes(run);
            CTFontRef font = (CTFontRef)CFDictionaryGetValue(attributes, kCTFontAttributeName);
            CFIndex hash = CFHash(font);
            CGColorRef cgColor = GetCGColor(attributes, CFSTR("NSColor"), kCTForegroundColorAttributeName);
            CGColorRef cgBackgroundColor = GetCGColor(attributes, CFSTR("NSBackgroundColor"), kCTBackgroundColorAttributeName);
            
            if (cgBackgroundColor) {
                Ra::Path bgPath;
                bgPath->addBounds(RaCG::BoundsFromCGRect(imageBounds));
                Ra::Color bgColor = RaCG::colorFromCG(cgBackgroundColor);
                Ra::Transform m = RaCG::transformFromCG(ctm);
                scene->addPath(bgPath, m, bgColor, 0, 0, & clipBounds);
            }
            Ra::Color color = RaCG::colorFromCG(cgColor);
            for (int j = 0; j < count; j++) {
                Ra::Transform m = RaCG::transformFromCG(CGAffineTransformTranslate(ctm, positions[j].x, positions[j].y));
                auto key = hash + glyphs[j];
                auto it = cache.find(key);
                if (it != cache.end())
                    scene->addPath(it->second, m, color, 0, 0, & clipBounds);
                else {
                    Ra::Path path;
                    CGPathRef cgPath = CTFontCreatePathForGlyph(font, glyphs[j], NULL);
                    RaCG::writeCGPathToPath(cgPath, path);
                    CGPathRelease(cgPath);
                    cache.emplace(key, path);
                    scene->addPath(path, m, color, 0, 0, & clipBounds);
                }
            }
        }
        return bounds;
    }
    
    static CGColorRef GetCGColor(CFDictionaryRef attributes, CFStringRef platformName, CFStringRef ctName) {
        return GetCGColor(attributes, platformName) ?: GetCGColor(attributes, ctName);
    }
    static CGColorRef GetCGColor(CFDictionaryRef attributes, CFStringRef name) {
        const CFTypeRef value = CFDictionaryGetValue(attributes, name);
        if (value == NULL)
            return NULL;
        if (CFGetTypeID(value) == CGColorGetTypeID())
            return (CGColorRef)value;
        NSColor *nsColor = (__bridge NSColor *)value;
        if ([nsColor isKindOfClass: [NSColor class]])
             return nsColor.CGColor;
        return NULL;
    }
    
    static CTFontRef createFont(const char *fontName, float fontSize) {
        CFStringRef cfFontName = CFStringCreateWithCString(kCFAllocatorDefault, fontName, kCFStringEncodingUTF8);
        CTFontRef ctFont = CTFontCreateWithName(cfFontName, fontSize, NULL);
        CFRelease(cfFontName);
        return ctFont;
    }
    static CFDictionaryRef createAttributes(const char *fontName, float fontSize, CGColorRef color){
        CTFontRef ctFont = createFont(fontName, fontSize);
        const void *keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
        const void *values[] = { ctFont, color };
        CFDictionaryRef attributes = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2, NULL, &kCFTypeDictionaryValueCallBacks);
        CFRelease(ctFont);
        return attributes;
    }
    
    static float lineHeightFor(const char *fontName, float size) {
        CFStringRef cfFontName = CFStringCreateWithCString(kCFAllocatorDefault, fontName, kCFStringEncodingUTF8);
        CTFontRef ctFont = CTFontCreateWithName(cfFontName, size, NULL);
        CGFloat ascent = CTFontGetAscent(ctFont);
        CGFloat descent = CTFontGetDescent(ctFont);
        CGFloat leading = CTFontGetLeading(ctFont);
        CFRelease(ctFont);
        CFRelease(cfFontName);
        return floor(ascent + 0.5) + floor(descent + 0.5) + floor(fmax(0, leading) + 0.5);
    }
    
    static Ra::SceneRef writeGlyphGrid(const char *fontName, float lineHeight, Ra::Color color) {
        Ra::SceneRef scene;
        float scale = lineHeight * lineHeight / lineHeightFor(fontName, lineHeight);
        CFStringRef cfFontName = CFStringCreateWithCString(kCFAllocatorDefault, fontName, kCFStringEncodingUTF8);
        CTFontRef ctFont = CTFontCreateWithName(cfFontName, scale, NULL);
        CFIndex glyphCount = CTFontGetGlyphCount(ctFont);
        if (glyphCount) {
            for (int d = ceilf(sqrtf(glyphCount)), glyph = 0; glyph <glyphCount; glyph++) {
                CGPathRef cgPath = CTFontCreatePathForGlyph(ctFont, glyph, NULL);
                Ra::Path path;
                RaCG::writeCGPathToPath(cgPath, path);
                scene->addPath(path, Ra::Transform(1, 0.f, 0.f, 1, lineHeight * float(glyph % d), lineHeight * float(glyph / d)), color, 0.f, 0);
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
