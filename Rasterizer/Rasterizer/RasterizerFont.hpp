//
//  RasterizerFont.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 04/01/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import <unordered_map>
#import "Rasterizer.hpp"
#import "stb_truetype.h"

struct RasterizerFont {
    RasterizerFont() { empty(); }
    void empty() { monospace = avg = em = space = ascent = descent = lineGap = unitsPerEm = 0, bzero(& info, sizeof(info)), cache.clear(); }
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
                        const char* lM_ =  "lM ";
                        int widths[3] = { 0, 0, 0 }, glyph, leftSideBearing, width, total = 0;
                        for (int j = 0; j < 3; j++)
                            if ((glyph = stbtt_FindGlyphIndex(& info, lM_[j])) != -1)
                                stbtt_GetGlyphHMetrics(& info, glyph, & widths[j], & leftSideBearing);
                        if (widths[0] && widths[1] && widths[2]) {
                            if (widths[0] == widths[1] && widths[1] == widths[2])
                                monospace = widths[0];
                            for (int j = 32; j < 128; j++)
                                if ((glyph = stbtt_FindGlyphIndex(& info, j)) != -1)
                                    stbtt_GetGlyphHMetrics(& info, glyph, & width, & leftSideBearing), total += width;
                            avg = total / 96, em = widths[1], space = widths[2];
                            stbtt_GetFontVMetrics(& info, & ascent, & descent, & lineGap);
                            unitsPerEm = 1.f / stbtt_ScaleForMappingEmToPixels(& info, 1.f);
                            return 1;
                        }
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
    int monospace, avg, em, space, ascent, descent, lineGap, unitsPerEm;
    stbtt_fontinfo info;
    
    static Rasterizer::Bounds writeGlyphs(RasterizerFont& font, float size, Rasterizer::Colorant color, Rasterizer::Bounds bounds, bool rtol, bool single, bool right, const char *str, Rasterizer::Scene& scene) {
        Rasterizer::Bounds glyphBounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
        if (font.info.numGlyphs == 0 || font.space == 0 || str == nullptr)
            return glyphBounds;
        int i, j, begin, step, len, codepoint, glyph;
        const char nl = '\n', sp = ' ', tab = '\t';
        const uint8_t *utf8 = (uint8_t *)str;
        std::vector<int> glyphs, lines;
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
            if (step) {
                if (codepoint == sp || codepoint == nl || codepoint == tab)
                    glyphs.emplace_back(-codepoint);
                else if ((glyph = stbtt_FindGlyphIndex(& font.info, codepoint)) && stbtt_IsGlyphEmpty(& font.info, glyph) == 0)
                    glyphs.emplace_back(glyph);
            }
        }
        float s = size / float(font.unitsPerEm), width, lineHeight, space, beginx, x, y;
        width = (bounds.ux - bounds.lx) / s, lineHeight = font.ascent - font.descent + font.lineGap;
        space = font.monospace ?: font.space ?: lineHeight * 0.166f;
        x = beginx = rtol ? width : 0.f, y = -font.ascent, i = 0;
        len = (int)glyphs.size(), lines.emplace_back(int(scene.paths.size()));
        do {
            for (; i < len && glyphs[i] < 0; i++)
                if (glyphs[i] == -nl) {
                    if (!single)
                        x = beginx, y -= lineHeight, lines.emplace_back(int(scene.paths.size()));
                } else
                    x += (glyphs[i] == -tab ? 4 : 1) * (rtol ? -space : space);
            begin = i;
            for (; i < len && glyphs[i] > 0; i++) {}
            int advances[i - begin], *advance = advances, total = 0, leftSideBearing;
            bzero(advances, sizeof(advances));
            if (rtol)
                std::reverse(& glyphs[begin], & glyphs[i]);
            for (j = begin; j < i; j++, advance++) {
                stbtt_GetGlyphHMetrics(& font.info, glyphs[j], advance, & leftSideBearing);
                if (j < len - 1)
                    *advance += stbtt_GetGlyphKernAdvance(& font.info, glyphs[j], glyphs[j + 1]), total += *advance;
            }
            if (!single && ((!rtol && x + total > width) || (rtol && x - total < 0.f)))
                x = beginx, y -= lineHeight, lines.emplace_back(int(scene.paths.size()));
            if (rtol)
                x -= total;
            for (advance = advances, j = begin; j < i; j++, x += *advance, advance++) {
                Rasterizer::Path path = font.glyphPath(glyphs[j], true);
                bool skip = single && ((!rtol && x + *advance > width) || (rtol && x < 0.f));
                if (!skip && path.ref->isDrawable()) {
                    Rasterizer::Transform ctm(s, 0.f, 0.f, s, x * s + bounds.lx, y * s + bounds.uy);
                    scene.addPath(path, ctm, color);
                    Rasterizer::Bounds user(path.ref->bounds.unit(ctm));
                    glyphBounds.extend(user.lx, user.ly), glyphBounds.extend(user.ux, user.uy);
                }
            }
            if (rtol)
                x -= total;
        } while (i < len);
        lines.emplace_back(int(scene.paths.size()));
        for (int i = 0, l0 = lines[0], l1 = lines[1]; i < lines.size() - 1; i++, l0 = l1, l1 = lines[i + 1])
            if (l0 != l1) {
                float ux = scene.paths[l1 - 1].ref->bounds.ux * s + scene.ctms[l1 - 1].tx;
                float dx = right ? bounds.ux - ux : -scene.paths[l0].ref->bounds.lx * s;
                for (int j = l0; j < l1; j++)
                    scene.ctms[j].tx += dx;
            }
        return glyphBounds;
    }
    static void writeGlyphGrid(RasterizerFont& font, float size, Rasterizer::Colorant color, Rasterizer::Scene& scene) {
        if (font.info.numGlyphs == 0 || font.space == 0)
            return;
        int d = ceilf(sqrtf((float)font.info.numGlyphs));
        float s = size / float(font.unitsPerEm);
        for (int glyph = 0; glyph < font.info.numGlyphs; glyph++)
            if (stbtt_IsGlyphEmpty(& font.info, glyph) == 0)
                scene.addPath(font.glyphPath(glyph, false), Rasterizer::Transform(s, 0.f, 0.f, s, size * float(glyph % d), size * float(glyph / d)), color);
    }
};
