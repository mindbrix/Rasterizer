//
//  RasterizerTrueType.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 04/01/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//

#import "Rasterizer.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#import "stb_truetype.h"

struct RasterizerTrueType {
    struct Font {
        Font() { empty(); }
		void empty() { monospace = space = 0.f, bzero(& info, sizeof(info)); }
        int set(const void *bytes, const char *name) {
			const unsigned char *ttf_buffer = (const unsigned char *)bytes;
			int numfonts = stbtt_GetNumberOfFonts(ttf_buffer);
			size_t numchars = strlen(name);
			int length;
			const char *n;
			empty();
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
							if (widths[0] == 0 && widths[1] == widths[2])
								return 0;
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
			stbtt_vertex *vertices, *vertex;
			int i, nverts = stbtt_GetGlyphShape(& info, glyph, & vertices);
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
				stbtt_FreeShape(& info, vertices);
			}
			return path;
		}
		float monospace, space;
        stbtt_fontinfo info;
    };
	
	static void writeGlyphs(Font& font, float size, uint8_t *bgra, Rasterizer::Bounds bounds, const char *str,
							std::vector<uint32_t>& bgras,
							std::vector<Rasterizer::AffineTransform>& ctms,
							std::vector<Rasterizer::Path>& paths) {
        if (font.info.numGlyphs == 0)
            return;
        const char nl = '\n', sp = ' ';
		int len = (int)strlen(str), i, ascent, descent, lineGap, advanceWidth, leftSideBearing;
        stbtt_GetFontVMetrics(& font.info, & ascent, & descent, & lineGap);
		float s, width, height, lineHeight, space, x, y;
		s = stbtt_ScaleForMappingEmToPixels(& font.info, size);
		width = (bounds.ux - bounds.lx) / s;
		height = ascent - descent, lineHeight = height + lineGap;
		space = font.monospace ?: font.space ?: lineHeight * 0.166f;
		
		for (x = y = i = 0; i < len; i++) {
			if (str[i] == nl)
                x = 0, y -= lineHeight;
            else {
				if (str[i] == sp) {
					if (x > width)
						x = 0, y -= lineHeight;
					else
						x += space;
				}
				else {
					int glyph = stbtt_FindGlyphIndex(& font.info, str[i]);
					if (glyph != -1 && stbtt_IsGlyphEmpty(& font.info, glyph) == 0) {
						bgras.emplace_back(*((uint32_t *)bgra));
						stbtt_GetGlyphHMetrics(& font.info, glyph, & advanceWidth, & leftSideBearing);
						ctms.emplace_back(s, 0, 0, s, x * s + bounds.lx, (y - height) * s + bounds.uy);
						x += advanceWidth;
						paths.emplace_back(font.glyphPath(glyph));
					}
				}
            }
		}
	}
	static void writeGlyphGrid(Font& font, float size, uint8_t *bgra,
							   std::vector<uint32_t>& bgras,
							   std::vector<Rasterizer::AffineTransform>& ctms,
							   std::vector<Rasterizer::Path>& paths) {
		if (font.info.numGlyphs == 0)
			return;
		int d = ceilf(sqrtf((float)font.info.numGlyphs));
		float s = stbtt_ScaleForMappingEmToPixels(& font.info, size);
		for (int glyph = 0; glyph < font.info.numGlyphs; glyph++)
			if (stbtt_IsGlyphEmpty(& font.info, glyph) == 0) {
				bgras.emplace_back(*((uint32_t *)bgra));
				ctms.emplace_back(s, 0, 0, s, size * float(glyph % d), size * float(glyph / d));
				paths.emplace_back(font.glyphPath(glyph));
			}
	}
};
