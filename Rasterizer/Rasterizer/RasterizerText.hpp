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
        Font() { monospace = space = 0.f, bzero(& info, sizeof(info)); }
        int init(const void *bytes, const char *name) {
            const unsigned char *ttf_buffer = (const unsigned char *)bytes;
			int numfonts = stbtt_GetNumberOfFonts(ttf_buffer);
			size_t numchars = strlen(name);
			int length;
			const char *n;
			for (int i = 0; i < numfonts; i++) {
				int offset = stbtt_GetFontOffsetForIndex(ttf_buffer, i);
				if (offset != -1) {
					stbtt_InitFont(& info, ttf_buffer, offset);
					if (info.numGlyphs) {
						n = stbtt_GetFontNameString(& info, & length, STBTT_PLATFORM_ID_MAC, 0, 0, 6);
						if (n != NULL && length == numchars && memcmp(name, n, length) == 0) {
							const char* lw_ =  "lW ";
							int widths[3] = { 0, 0, 0 }, glyph, leftSideBearing;
							for (int j = 0; j < 3; j++)
								if ((glyph = stbtt_FindGlyphIndex(& info, lw_[j])) != -1)
									stbtt_GetGlyphHMetrics(& info, glyph, & widths[j], & leftSideBearing);
							if (widths[0] == widths[1] && widths[1] == widths[2])
								monospace = widths[0];
                            space = widths[2];
							return 1;
						}
					}
				}
			}
			return 0;
        }
		Rasterizer::Path glyphPath(int glyph) {
			Rasterizer::Path path;
			writeGlyphPath(*this, glyph, path);
			return path;
		}
		float monospace, space;
        stbtt_fontinfo info;
    };
	
	static void writeGlyphs(Font& font, float size, uint8_t *bgra, Rasterizer::Bounds bounds, const char *str, RasterizerCoreGraphics::Scene& scene) {
        if (font.info.numGlyphs == 0)
            return;
        char nl = '\n', sp = ' ';
		int d = 80;
		size_t len = strlen(str), i, lines = 1;
        for (i = 0; i < len; i++)
            if (str[i] == nl || (i && i % d == 0))
                lines++;
        int ascent, descent, lineGap, advanceWidth, leftSideBearing;
        stbtt_GetFontVMetrics(& font.info, & ascent, & descent, & lineGap);
        float height = ascent - descent, lineHeight = height + lineGap;
        float space = font.monospace ? font.monospace : font.space ? font.space : lineHeight * 0.166f;
        float s = size / height;
		float x = 0, y = bounds.uy / s - height;
        for (i = 0; i < len; i++) {
			char c = str[i];
            if (c == nl)
                x = 0, y -= lineHeight;
            else {
				if (i && i % d == 0)
					x = 0, y -= lineHeight;
				if (c == sp)
					x += space;
				else {
					int glyph = stbtt_FindGlyphIndex(& font.info, c);
					if (glyph != -1 && stbtt_IsGlyphEmpty(& font.info, glyph) == 0) {
						stbtt_GetGlyphHMetrics(& font.info, glyph, & advanceWidth, & leftSideBearing);
						scene.ctms.emplace_back(s, 0, 0, s, x * s, y * s);
						x += advanceWidth;
						scene.bgras.emplace_back(*((uint32_t *)bgra));
						scene.paths.emplace_back(font.glyphPath(glyph));
					}
				}
            }
		}
	}
	static void writeGlyphGrid(Font& font, float size, uint8_t *bgra, RasterizerCoreGraphics::Scene& scene) {
		if (font.info.numGlyphs == 0)
			return;
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
        int i, nverts = stbtt_GetGlyphShape(& font.info, glyph, & vertices);
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
