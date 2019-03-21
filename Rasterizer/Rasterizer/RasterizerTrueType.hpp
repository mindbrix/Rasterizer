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
        void empty() { monospace = space = 0.f, bzero(& info, sizeof(info)), cache.clear(); }
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
                if (cacheable) {
                    cache.emplace(glyph, path);
                }
            }
            path.ref->isGlyph = true;
            return path;
        }
        std::unordered_map<int, Rasterizer::Path> cache;
        float monospace, space;
        stbtt_fontinfo info;
    };
    
    static Rasterizer::Path writeGlyphs(Font& font, float size, uint8_t *bgra, Rasterizer::Bounds bounds, bool left, const char *str,
                            std::vector<Rasterizer::Colorant>& bgras,
                            std::vector<Rasterizer::Transform>& ctms,
                            std::vector<Rasterizer::Path>& paths) {
        if (font.info.numGlyphs == 0)
            return Rasterizer::Path();
        int i, j, begin, step, len, codepoint;
        const char nl = '\n', sp = ' ', tab = '\t';
        const int NL = -1, SP = -2, TAB = -3;
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
                glyphs.emplace_back(codepoint == sp ? SP : codepoint == nl ? NL : codepoint == tab ? TAB : stbtt_FindGlyphIndex(& font.info, codepoint));
        }
        int base, ascent, descent, lineGap, leftSideBearing;
        stbtt_GetFontVMetrics(& font.info, & ascent, & descent, & lineGap);
        float s, width, height, lineHeight, space, beginx, x, y;
        s = stbtt_ScaleForMappingEmToPixels(& font.info, size);
        width = (bounds.ux - bounds.lx) / s;
        height = ascent - descent, lineHeight = height + lineGap;
        space = font.monospace ?: font.space ?: lineHeight * 0.166f;
        beginx = left ? width : 0;
        base = (int)paths.size();
        len = (int)glyphs.size();
        x = beginx;
        y = i = 0;
        do {
            while (i < len && (glyphs[i] == SP || glyphs[i] == NL || glyphs[i] == TAB)) {
                if (glyphs[i] == NL)
                    x = beginx, y -= lineHeight;
                else
                    x += (glyphs[i] == TAB ? 4 : 1) * (left ? -space : space);
                i++;
            }
            begin = i;
            while (i < len && glyphs[i] != SP && glyphs[i] != NL && glyphs[i] != TAB)
                i++;
            int advances[i - begin], *advance = advances, total = 0;
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
                    bgras.emplace_back(bgra);
                    paths.emplace_back(font.glyphPath(glyphs[j], true));
                    Rasterizer::Bounds gb = paths.back().ref->bounds;
                    if (x == beginx) {
                        if (left)
                            x += *advance - gb.ux;
                        else
                            x += -gb.lx;
                    }
                    if (left)
                        x -= *advance;
                    ctms.emplace_back(s, 0, 0, s, x * s + bounds.lx, (y - height) * s + bounds.uy);
                    if (!left)
                        x += *advance;
                }
        } while (i < len);
        
        Rasterizer::Path shapes;
        shapes.ref->addShapes(paths.size() - base);
        if (0)
            memset(shapes.ref->circles, 0x01, shapes.ref->shapesCount * sizeof(bool));
        Rasterizer::Transform *dst = shapes.ref->shapes;
        float lx = FLT_MAX, ly = FLT_MAX, ux = -FLT_MAX, uy = -FLT_MAX;
        for (i = base; i < paths.size(); i++, dst++) {
            *dst = paths[i].ref->bounds.unit(ctms[i]);
            Rasterizer::Bounds b(*dst);
            lx = lx < b.lx ? lx : b.lx, ly = ly < b.ly ? ly : b.ly;
            ux = ux > b.ux ? ux : b.ux, uy = uy > b.uy ? uy : b.uy;
        }
        shapes.ref->bounds = Rasterizer::Bounds(lx, ly, ux, uy);
        return shapes;
    }
    static void writeGlyphGrid(Font& font, float size, uint8_t *bgra, Rasterizer::Scene& scene) {
        if (font.info.numGlyphs == 0)
            return;
        Rasterizer::Colorant color(bgra);
        int d = ceilf(sqrtf((float)font.info.numGlyphs));
        float s = stbtt_ScaleForMappingEmToPixels(& font.info, size);
        for (int glyph = 0; glyph < font.info.numGlyphs; glyph++)
            if (stbtt_IsGlyphEmpty(& font.info, glyph) == 0)
                scene.addPath(font.glyphPath(glyph, false), Rasterizer::Transform(s, 0, 0, s, size * float(glyph % d), size * float(glyph / d)), color);
    }
};
