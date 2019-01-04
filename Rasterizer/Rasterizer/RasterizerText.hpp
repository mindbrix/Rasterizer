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
            int offset = stbtt_GetFontOffsetForIndex(ttf_buffer, 0);
            if (offset == -1)
                return offset;
            return stbtt_InitFont(& font, ttf_buffer, offset);
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
        int flx, fly, fux, fuy, fw, fh, fdim, fsize = 32, d, ix, iy;
        RasterizerText::Font font;
        font.init(data.bytes);
        stbtt_GetFontBoundingBox(& font.font, & flx, & fly, & fux, & fuy);
        fw = fux - flx, fh = fuy - fly, fdim = fw < fh ? fw : fh;
        d = ceilf(sqrtf((float)font.font.numGlyphs));
        Rasterizer::AffineTransform scale = { float(fsize) / float(fdim), 0, 0, float(fsize) / float(fdim), 0, 0 };
        for (int glyph = 0; glyph < font.font.numGlyphs; glyph++) {
            ix = glyph % d, iy = glyph / d;
            Rasterizer::AffineTransform CTM = { 1, 0, 0, 1, float(ix * (fux - flx)), float(iy * (fuy - fly)) };
            CTM = scale.concat(CTM);
            if (stbtt_IsGlyphEmpty(& font.font, glyph) == 0) {
                scene.bgras.emplace_back(*((uint32_t *)bgra));
                scene.paths.emplace_back();
                writeGlyphPath(font, glyph, scene.paths.back());
                scene.ctms.emplace_back(CTM);
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
                        path.sequence->quadTo(vertex->cx, vertex->cy, vertex->x, vertex->y);
                        break;
                    case STBTT_vcubic:
                        path.sequence->cubicTo(vertex->cx, vertex->cy, vertex->cx1, vertex->cy1, vertex->x, vertex->y);
                        break;
                }
            }
            stbtt_FreeShape(& font.font, vertices);
        }
    }
};
