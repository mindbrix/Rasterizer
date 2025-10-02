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
#import <CoreText/CoreText.h>
#import "Rasterizer.hpp"
#import "RasterizerCG.hpp"


struct RasterizerCoreText {
    static void addAttributedStringToScene(CFAttributedStringRef string, CGAffineTransform ctm, CGRect clip, Ra::Scene& scene) {
        Ra::Bounds clipBounds = CGRectIsNull(clip) || CGRectIsEmpty(clip) || CGRectIsInfinite(clip) ? Ra::Bounds::huge() : RaCG::BoundsFromCGRect(clip);
        CTLineRef line = CTLineCreateWithAttributedString(string);
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
            CGColorRef cgColor = (CGColorRef)CFDictionaryGetValue(attributes, kCTForegroundColorAttributeName);
            Ra::Colorant color = RaCG::colorantFromCG(cgColor);
            
            for (int j = 0; j < count; j++) {
                CGPathRef cgPath = CTFontCreatePathForGlyph(font, glyphs[j], NULL);
                Ra::Path path;
                RaCG::writeCGPathToPath(cgPath, path);
                Ra::Transform m = RaCG::transformFromCG(CGAffineTransformTranslate(ctm, positions[j].x, positions[j].y));
                scene.addPath(path, m, color, 0, 0, & clipBounds);
                CGPathRelease(cgPath);
            }
        }
        CFRelease(line);
    }
};
