//
//  RasterizerText.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 04/01/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//

#import "Rasterizer.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#import "stb_truetype.h"

struct RasterizerText {
    struct Font {
        Font() { bzero(& font, sizeof(font)); }
        int init(const void *bytes) {
            const unsigned char *ttf_buffer = (const unsigned char *)bytes;
            return stbtt_InitFont(& font, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0));
        }
        stbtt_fontinfo font;
    };
    
    static void writeGlyphGrid(NSString *fontName, RasterizerCoreGraphics::Scene& scene) {
        CTFontDescriptorRef fontRef = CTFontDescriptorCreateWithNameAndSize ((__bridge CFStringRef)fontName, 1);
        CFURLRef url = (CFURLRef)CTFontDescriptorCopyAttribute(fontRef, kCTFontURLAttribute);
        CFRelease(fontRef);
        NSData *data = [NSData dataWithContentsOfURL:(__bridge NSURL *)url];
        CFRelease(url);
        uint8_t bgra[4] = { 0, 0, 0, 255 };
        RasterizerText::Font font;
        font.init(data.bytes);
        for (int glyph = 0; glyph < font.font.numGlyphs; glyph++) {
            if (stbtt_IsGlyphEmpty(& font.font, glyph) == 0) {
                scene.bgras.emplace_back(*((uint32_t *)bgra));
                scene.paths.emplace_back();
                writeGlyphPath(font, glyph, scene.paths.back());
                scene.ctms.emplace_back(1, 0, 0, 1, 0, 0);
            }
        }
    }
    
    static void writeGlyphPath(Font& font, int glyph, Rasterizer::Path& path) {
        stbtt_vertex *vertices, *vertex;
        int nverts, i;
        nverts = stbtt_GetGlyphShape(& font.font, glyph, & vertices);
        if (nverts) {
            for (vertex = vertices, i = 0; i < nverts; i++, vertex++) {
                switch (vertex->type) {
                    case STBTT_vmove:
                        path.sequence->moveTo(vertex->x, vertex->y);
                        break;
                    case STBTT_vline:
                        path.sequence->lineTo(vertex->x, vertex->y);
                        break;
                    case STBTT_vcurve:
                        path.sequence->quadTo(vertex->x, vertex->y, vertex->cx, vertex->cy);
                        break;
                    case STBTT_vcubic:
                        path.sequence->cubicTo(vertex->x, vertex->y, vertex->cx, vertex->cy, vertex->cx1, vertex->cy1);
                        break;
                }
            }
            stbtt_FreeShape(& font.font, vertices);
        }
    }
};
