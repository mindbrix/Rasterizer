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
        Font() { bzero(& info, sizeof(info)); }
        int init(const void *bytes) {
            const unsigned char *ttf_buffer = (const unsigned char *)bytes;
            int offset = stbtt_GetFontOffsetForIndex(ttf_buffer, 0);
            if (offset == -1)
                return 0;
            return stbtt_InitFont(& info, ttf_buffer, offset);
        }
        stbtt_fontinfo info;
    };
    
    static void writeGlyphGrid(const void *bytes, RasterizerCoreGraphics::Scene& scene) {
        uint8_t bgra[4] = { 0, 0, 0, 255 };
        int flx, fly, fux, fuy, fw, fh, fdim, fsize = 32, d, ix, iy;
        RasterizerText::Font font;
        if (font.init(bytes) != 0) {
            stbtt_GetFontBoundingBox(& font.info, & flx, & fly, & fux, & fuy);
            fw = fux - flx, fh = fuy - fly, fdim = fw < fh ? fw : fh;
            d = ceilf(sqrtf((float)font.info.numGlyphs));
            float s = float(fsize) / float(fdim);
            for (int glyph = 0; glyph < font.info.numGlyphs; glyph++)
                if (stbtt_IsGlyphEmpty(& font.info, glyph) == 0) {
                    ix = glyph % d, iy = glyph / d;
                    Rasterizer::AffineTransform CTM = { s, 0, 0, s, s * float(ix * fdim), s * float(iy * fdim) };
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
        nverts = stbtt_GetGlyphShape(& font.info, glyph, & vertices);
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
            stbtt_FreeShape(& font.info, vertices);
        }
    }
};
