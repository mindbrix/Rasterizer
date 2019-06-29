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
    bool set(const void *bytes, const char *name) {
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
                        int widths[3] = { 0, 0, 0 }, glyph, width, total = 0;
                        for (int j = 0; j < 3; j++)
                            if ((glyph = stbtt_FindGlyphIndex(& info, lM_[j])) != -1)
                                stbtt_GetGlyphHMetrics(& info, glyph, & widths[j], NULL);
                        if (widths[0] && widths[1] && widths[2]) {
                            if (widths[0] == widths[1] && widths[1] == widths[2])
                                monospace = widths[0];
                            for (int j = 32; j < 128; j++)
                                if ((glyph = stbtt_FindGlyphIndex(& info, j)) != -1)
                                    stbtt_GetGlyphHMetrics(& info, glyph, & width, NULL), total += width;
                            avg = total / 96, em = widths[1], space = widths[2];
                            stbtt_GetFontVMetrics(& info, & ascent, & descent, & lineGap);
                            unitsPerEm = 1.f / stbtt_ScaleForMappingEmToPixels(& info, 1.f);
                            return true;
                        }
                    }
                }
            }
        return false;
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
    
    static Rasterizer::Bounds writeGlyphs(RasterizerFont& font, float size, Rasterizer::Colorant color, Rasterizer::Bounds bounds, bool rtl, bool single, bool right, const char *str, Rasterizer::Scene& scene) {
        Rasterizer::Bounds glyphBounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
        if (font.info.numGlyphs == 0 || font.space == 0 || str == nullptr)
            return glyphBounds;
        int i, j, begin, step, len, codepoint, glyph, l0, l1;
        const char nl = '\n', sp = ' ', tab = '\t';
        const uint8_t *utf8 = (uint8_t *)str;
        std::vector<int> glyphs, lines;
        for (step = 1, codepoint = i = 0; step; i += step) {
            if (utf8[i] == 0)
                break;
            else if (utf8[i] < 128)
                step = 1, codepoint = utf8[i];
            else if ((utf8[i] & 0xE0) == 0xC0 && (utf8[i + 1] & 0xC0) == 0x80)
                step = 2, codepoint = ((utf8[i] & ~0xE0) << 6) | (utf8[i + 1] & ~0xC0);
            else if ((utf8[i] & 0xF0) == 0xE0 && (utf8[i + 1] & 0xC0) == 0x80 && (utf8[i + 2] & 0xC0) == 0x80)
                step = 3, codepoint = (utf8[i] & ~0xF0) << 12 | (utf8[i + 1] & ~0xC0) << 6 | (utf8[i + 2] & ~0xC0);
            else if ((utf8[i] & 0xF8) == 0xF0 && (utf8[i + 1] & 0xC0) == 0x80 && (utf8[i + 2] & 0xC0) == 0x80 && (utf8[i + 3] & 0xC0) == 0x80)
                step = 4, codepoint = (utf8[i] & ~0xF8) << 18 | (utf8[i + 1] & ~0xC0) << 12 | (utf8[i + 2] & ~0xC0) << 6 | (utf8[i + 3] & ~0xC0);
            else
                assert(0);
            if (codepoint == sp || codepoint == nl || codepoint == tab)
                glyphs.emplace_back(-codepoint);
            else if ((glyph = stbtt_FindGlyphIndex(& font.info, codepoint)) && stbtt_IsGlyphEmpty(& font.info, glyph) == 0)
                glyphs.emplace_back(glyph);
        }
        float s = size / float(font.unitsPerEm), width, lineHeight, space, x, y, xs[glyphs.size()];
        for (int k = 0; k < glyphs.size(); k++) xs[k] = FLT_MAX;
        width = (bounds.ux - bounds.lx) / s, lineHeight = font.ascent - font.descent + font.lineGap;
        space = font.monospace ?: font.space ?: lineHeight * 0.166f;
        x = 0.f, i = 0, len = (int)glyphs.size(), lines.emplace_back(i);
        do {
            for (; i < len && glyphs[i] < 0; i++)
                if (glyphs[i] != -nl)
                    x += (glyphs[i] == -tab ? 4 : 1) * (rtl ? -space : space);
                else if (!single)
                    x = 0.f, lines.emplace_back(i);
            for (begin = i; i < len && glyphs[i] > 0; i++) {}
            if (rtl)
                std::reverse(& glyphs[begin], & glyphs[i]);
            int advances[i - begin], *adv = advances, total = 0;
            for (j = begin; j < i; j++, adv++) {
                *adv = 0, stbtt_GetGlyphHMetrics(& font.info, glyphs[j], adv, NULL);
                if (j < len - 1)
                    *adv += stbtt_GetGlyphKernAdvance(& font.info, glyphs[j], glyphs[j + 1]), total += *adv;
            }
            if (!single && (fabsf(x) + total > width))
                x = 0.f, lines.emplace_back(begin);
            if (rtl)
                x -= total;
            for (adv = advances, j = begin; j < i; j++, x += *adv++)
                if (!(single && (fabsf(x) + *adv > width)))
                    xs[j] = x;
            if (rtl)
                x -= total;
        } while (i < len);
        lines.emplace_back(i);
        for (y = -font.ascent, i = 0, l0 = lines[0]; i < lines.size() - 1; i++, l0 = l1, y -= lineHeight) {
            l1 = lines[i + 1];
            if (l0 != l1) {
                float dx = 0.f;
                if (rtl)
                    dx = width;
                else
                    for (int j = right ? l1 - 1 : l0; j >= 0 && j < l1; j += (right ? -1 : 1))
                        if ((x = xs[j]) != FLT_MAX) {
                            Rasterizer::Path path = font.glyphPath(glyphs[j], true);
                            dx += right ? width - (x + path.ref->bounds.ux) : -path.ref->bounds.lx;
                            break;
                        }
                for (int j = l0; j < l1; j++) 
                    if ((x = xs[j]) != FLT_MAX) {
                        Rasterizer::Path path = font.glyphPath(glyphs[j], true);
                        Rasterizer::Transform ctm(s, 0.f, 0.f, s, (x + dx) * s + bounds.lx, y * s + bounds.uy);
                        scene.addPath(path, ctm, color);
                        glyphBounds.extend(Rasterizer::Bounds(path.ref->bounds.unit(ctm)));
                    }
            }
        }
        return glyphBounds;
    }
    static void writeGlyphGrid(RasterizerFont& font, float size, Rasterizer::Colorant color, Rasterizer::Scene& scene) {
        if (font.info.numGlyphs == 0 || font.space == 0)
            return;
        float s = size / float(font.unitsPerEm);
        for (int d = ceilf(sqrtf(font.info.numGlyphs)), glyph = 0; glyph < font.info.numGlyphs; glyph++)
            if (stbtt_IsGlyphEmpty(& font.info, glyph) == 0)
                scene.addPath(font.glyphPath(glyph, false), Rasterizer::Transform(s, 0.f, 0.f, s, size * float(glyph % d), size * float(glyph / d)), color);
    }
};
