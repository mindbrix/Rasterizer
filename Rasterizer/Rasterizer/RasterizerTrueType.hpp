//
//  RasterizerTrueType.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 04/01/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import <unordered_map>
#import "Rasterizer.hpp"
#import "stb_truetype.h"

struct RasterizerTrueType {
    struct Font {
        Font() { empty(); }
        void empty() { monospace = space = ascent = descent = lineGap = 0, bzero(& info, sizeof(info)), cache.clear(); }
        int set(const void *bytes, const char *name) {
            const unsigned char *ttf_buffer = (const unsigned char *)bytes;
            int numfonts = stbtt_GetNumberOfFonts(ttf_buffer), numchars = (int)strlen(name), length, offset;
            empty();
            for (int i = 0; i < numfonts; i++)
                if ((offset = stbtt_GetFontOffsetForIndex(ttf_buffer, i)) != -1) {
                    stbtt_InitFont(& info, ttf_buffer, offset);
                    if (info.numGlyphs) {
                        const char *n = stbtt_GetFontNameString(& info, & length, STBTT_PLATFORM_ID_MAC, 0, 0, 6);
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
                            stbtt_GetFontVMetrics(& info, & ascent, & descent, & lineGap);
                            return 1;
                        }
                    }
                }
            return 0;
        }
        Rasterizer::Path glyphPath(int glyph, bool cacheable) {
            if (cacheable) {
                auto it = cache.find(glyph);
                if (it != cache.end())
                    return it->second;
            }
            Rasterizer::Path path;
            stbtt_vertex *vertices, *vertex;
            int i, nverts = stbtt_GetGlyphShape(& info, glyph, & vertices);
            if (nverts) {
                for (vertex = vertices, i = 0; i < nverts; i++, vertex++)
                    switch (vertex->type) {
                        case STBTT_vmove:
                            path.ref->moveTo(vertex->x, vertex->y);
                            break;
                        case STBTT_vline:
                            path.ref->lineTo(vertex->x, vertex->y);
                            break;
                        case STBTT_vcurve:
                            path.ref->quadTo(vertex->cx, vertex->cy, vertex->x, vertex->y);
                            break;
                        case STBTT_vcubic:
                            path.ref->cubicTo(vertex->cx, vertex->cy, vertex->cx1, vertex->cy1, vertex->x, vertex->y);
                            break;
                    }
                stbtt_FreeShape(& info, vertices);
                if (cacheable)
                    cache.emplace(glyph, path);
                path.ref->isGlyph = true;
            }
            return path;
        }
        std::unordered_map<int, Rasterizer::Path> cache;
        int monospace, space, ascent, descent, lineGap;
        stbtt_fontinfo info;
    };
    
    static Rasterizer::Bounds writeGlyphs(Font& font, float size, Rasterizer::Colorant color, Rasterizer::Bounds bounds, bool left, const char *str, Rasterizer::Scene& scene) {
        Rasterizer::Bounds glyphBounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
        if (font.info.numGlyphs == 0)
            return glyphBounds;
        int i, j, begin, step, len, codepoint;
        const char nl = '\n', sp = ' ', tab = '\t';
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
                glyphs.emplace_back(codepoint == sp || codepoint == nl || codepoint == tab ? -codepoint : stbtt_FindGlyphIndex(& font.info, codepoint));
        }
        float s, width, height, lineHeight, space, beginx, x, y;
        s = stbtt_ScaleForMappingEmToPixels(& font.info, size);
        width = (bounds.ux - bounds.lx) / s, height = font.ascent - font.descent, lineHeight = height + font.lineGap;
        space = font.monospace ?: font.space ?: lineHeight * 0.166f;
        x = beginx = left ? width : 0, y = i = 0;
        len = (int)glyphs.size();
        do {
            while (i < len && glyphs[i] < 0) {
                if (glyphs[i] == -nl)
                    x = beginx, y -= lineHeight;
                else
                    x += (glyphs[i] == -tab ? 4 : 1) * (left ? -space : space);
                i++;
            }
            begin = i;
            while (i < len && glyphs[i] >= 0)
                i++;
            int advances[i - begin], *advance = advances, total = 0, leftSideBearing;
            bzero(advances, sizeof(advances));
            for (j = begin; j < i; j++, advance++)
                if (glyphs[j] > 0 && stbtt_IsGlyphEmpty(& font.info, glyphs[j]) == 0) {
                    stbtt_GetGlyphHMetrics(& font.info, glyphs[j], advance, & leftSideBearing);
                    if (j < len - 1)
                        *advance += stbtt_GetGlyphKernAdvance(& font.info, glyphs[j], glyphs[j + 1]), total += *advance;
                }
            if (!left && x + total > width)
                x = beginx, y -= lineHeight;
            for (advance = advances, j = begin; j < i; j++, advance++)
                if (*advance) {
                    Rasterizer::Path path = font.glyphPath(glyphs[j], true);
                    if (x == beginx) {
                        if (left)
                            x += *advance - path.ref->bounds.ux;
                        else
                            x += -path.ref->bounds.lx;
                    }
                    if (left)
                        x -= *advance;
                    Rasterizer::Transform ctm(s, 0, 0, s, x * s + bounds.lx, (y - height) * s + bounds.uy);
                    if (scene.addPath(path, ctm, color)) {
                        Rasterizer::Bounds user(path.ref->bounds.unit(ctm));
                        glyphBounds.extend(user.lx, user.ly), glyphBounds.extend(user.ux, user.uy);
                    }
                    if (!left)
                        x += *advance;
                }
        } while (i < len);
        return glyphBounds;
    }
    static void writeGlyphGrid(Font& font, float size, Rasterizer::Colorant color, Rasterizer::Scene& scene) {
        if (font.info.numGlyphs == 0)
            return;
        int d = ceilf(sqrtf((float)font.info.numGlyphs));
        float s = stbtt_ScaleForMappingEmToPixels(& font.info, size);
        for (int glyph = 0; glyph < font.info.numGlyphs; glyph++)
            if (stbtt_IsGlyphEmpty(& font.info, glyph) == 0)
                scene.addPath(font.glyphPath(glyph, false), Rasterizer::Transform(s, 0, 0, s, size * float(glyph % d), size * float(glyph / d)), color);
    }
};
