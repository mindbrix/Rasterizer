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
        int init(const void *bytes, const char *name) {
            const unsigned char *ttf_buffer = (const unsigned char *)bytes;
			int numfonts = stbtt_GetNumberOfFonts(ttf_buffer);
			size_t numchars = strlen(name);
			int length;
			const char *n;
			for (int i = 0; i < numfonts; i++) {
				stbtt_InitFont(& info, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, i));
				n = stbtt_GetFontNameString(& info, & length, STBTT_PLATFORM_ID_MAC, 0, 0, 6);
				if (n != NULL && length == numchars && memcmp(name, n, length) == 0)
					return 1;
			}
			return 0;
        }
		Rasterizer::Path glyphPath(int glyph) {
			Rasterizer::Path path;
			writeGlyphPath(*this, glyph, path);
			return path;
		}
        stbtt_fontinfo info;
    };
	
	static void writeGlyphs(Font& font, float size, uint8_t *bgra, const char *str, RasterizerCoreGraphics::Scene& scene) {
		int flx, fly, fux, fuy, fw, fh, fdim, d = 16;
		stbtt_GetFontBoundingBox(& font.info, & flx, & fly, & fux, & fuy);
        int ascent, descent, lineGap, advanceWidth, leftSideBearing;
        stbtt_GetFontVMetrics(& font.info, & ascent, & descent, & lineGap);
		fw = fux - flx, fh = fuy - fly, fdim = fw < fh ? fw : fh;
		float s = size / float(fdim);
		size_t len = strlen(str), i;
		for (i = 0; i < len; i++) {
			char c = str[i];
			int glyph = stbtt_FindGlyphIndex(& font.info, c);
			if (glyph != -1 && stbtt_IsGlyphEmpty(& font.info, glyph) == 0) {
                stbtt_GetGlyphHMetrics(& font.info, glyph, & advanceWidth, & leftSideBearing);
				scene.bgras.emplace_back(*((uint32_t *)bgra));
				scene.paths.emplace_back(font.glyphPath(glyph));
				scene.ctms.emplace_back(s * 1.333, 0, 0, s * 1.333, size * float(i % d), size * float(i / d));
			}
		}
	}
	static void writeGlyphGrid(Font& font, float size, uint8_t *bgra, RasterizerCoreGraphics::Scene& scene) {
		int flx, fly, fux, fuy, fw, fh, fdim, d;
		stbtt_GetFontBoundingBox(& font.info, & flx, & fly, & fux, & fuy);
		fw = fux - flx, fh = fuy - fly, fdim = fw < fh ? fw : fh;
		d = ceilf(sqrtf((float)font.info.numGlyphs));
		float s = size / float(fdim);
		for (int glyph = 0; glyph < font.info.numGlyphs; glyph++)
			if (stbtt_IsGlyphEmpty(& font.info, glyph) == 0) {
				scene.bgras.emplace_back(*((uint32_t *)bgra));
				scene.paths.emplace_back(font.glyphPath(glyph));
				scene.ctms.emplace_back(s * 1.333, 0, 0, s * 1.333, size * float(glyph % d), size * float(glyph / d));
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
