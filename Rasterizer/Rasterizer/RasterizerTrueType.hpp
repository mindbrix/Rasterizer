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
        int i, j, idx, step, len, codepoint;
		const char nl = '\n', sp = ' ';
		const int NL = -1, SP = -2;
		const uint8_t *utf8 = (uint8_t *)str;
		std::vector<int> glyphs;
		for (step = 1, codepoint = i = 0; step; i += step) {
            if (utf8[i] < 128)
                step = 1, codepoint = utf8[i];
            else if ((utf8[i] & 0xE0) == 0xC0 && (utf8[i + 1] & 0xC0) == 0x80)
                step = 2, codepoint = ((utf8[i] & ~0xE0) << 6) | (utf8[i + 1] & ~0xC0);
            else if ((utf8[i] & 0xF0) == 0xE0 && (utf8[i + 1] & 0xC0) == 0x80 && (utf8[i + 2] & 0xC0) == 0x80)
                step = 3, codepoint = (utf8[i] & ~0xF0) << 12 | (utf8[i + 1] & ~0xC0) << 6 | (utf8[i + 2] & ~0xC0);
            else if ((utf8[i] & 0xF8) == 0xF0 && (utf8[i + 1] & 0xC0) == 0x80 && (utf8[i + 2] & 0xC0) == 0x80 && (utf8[i + 3] & 0xC0) == 0x80)
                step = 4, codepoint = (utf8[i] & ~0xF8) << 18 | (utf8[i + 1] & ~0xC0) << 12 | (utf8[i + 2] & ~0xC0) << 6 | (utf8[i + 3] & ~0xC0);
            else
                assert(0);
            for (j = 0; step && j < step; j++)
                if (utf8[i + j] == 0)
                    step = 0;
            if (step)
                glyphs.emplace_back(codepoint == sp ? SP : codepoint == nl ? NL : stbtt_FindGlyphIndex(& font.info, codepoint));
        }
		int ascent, descent, lineGap, leftSideBearing;
		stbtt_GetFontVMetrics(& font.info, & ascent, & descent, & lineGap);
		float s, width, height, lineHeight, space, x, y;
		s = stbtt_ScaleForMappingEmToPixels(& font.info, size);
		width = (bounds.ux - bounds.lx) / s;
		height = ascent - descent, lineHeight = height + lineGap;
		space = font.monospace ?: font.space ?: lineHeight * 0.166f;
        len = (int)glyphs.size();
		x = y = i = 0;
		do {
			while (i < len && (glyphs[i] == SP || glyphs[i] == NL)) {
				if (glyphs[i] == NL)
					x = 0, y -= lineHeight;
				else
					x += space;
				i++;
			}
			idx = i;
			while (i < len && glyphs[i] != SP && glyphs[i] != NL)
				i++;
			int advances[i - idx], *advance = advances, total = 0;
			bzero(advances, sizeof(advances));
			for (j = idx; j < i; j++, advance++)
				if (glyphs[j] > 0 && stbtt_IsGlyphEmpty(& font.info, glyphs[j]) == 0) {
					stbtt_GetGlyphHMetrics(& font.info, glyphs[j], advance, & leftSideBearing);
					if (j < len - 1)
						*advance += stbtt_GetGlyphKernAdvance(& font.info, glyphs[j], glyphs[j + 1]), total += *advance;
				}
			if (x + total > width)
				x = 0, y -= lineHeight;
			for (advance = advances, j = idx; j < i; j++, advance++)
				if (*advance) {
					bgras.emplace_back(*((uint32_t *)bgra));
					ctms.emplace_back(s, 0, 0, s, x * s + bounds.lx, (y - height) * s + bounds.uy);
					x += *advance;
					paths.emplace_back(font.glyphPath(glyphs[j]));
				}
		} while (i < len);
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
