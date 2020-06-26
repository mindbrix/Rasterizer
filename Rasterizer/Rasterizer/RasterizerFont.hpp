//
//  RasterizerFont.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 04/01/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import <fcntl.h>
#import <sys/stat.h>
#import <unordered_map>
#import "Rasterizer.hpp"
#import "stb_truetype.h"

struct RasterizerFont {
    static const char nl = '\n', sp = ' ', tab = '\t';
    RasterizerFont() { empty(); }
    void empty() { monospace = avg = em = space = ascent = descent = lineGap = unitsPerEm = 0, bzero(& info, sizeof(info)), cache.clear(); }
    bool isEmpty() { return info.numGlyphs == 0 || space == 0; }
    bool load(const char *filename, const char *name) {
        int fd;  struct stat st;
        if ((fd = open(filename, O_RDONLY)) == -1 || fstat(fd, & st) == -1)
            return false;
        empty(), bytes->resize(st.st_size), read(fd, bytes->addr, st.st_size), close(fd);
        
        const unsigned char *ttf_buffer = (const unsigned char *)bytes->addr;
        int numfonts = stbtt_GetNumberOfFonts(ttf_buffer), numchars = (int)strlen(name), length, offset;
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
                            spaceGlyph = stbtt_FindGlyphIndex(& info, ' ');
                            return true;
                        }
                    }
                }
            }
        return false;
    }
    Ra::Path glyphPath(int glyph, bool cacheable) {
        if (cacheable) {
            auto it = cache.find(glyph);
            if (it != cache.end())
                return it->second;
        }
        Ra::Path path;
        stbtt_vertex *v;
        int i, mcount = 0, lcount = 0, qcount = 0, ccount = 0, nverts = stbtt_GetGlyphShape(& info, glyph, & v);
        if (nverts) {
            for (i = 0; i < nverts; i++)
                switch (v[i].type) {
                    case STBTT_vmove:
                        mcount++;
                        break;
                    case STBTT_vline:
                        lcount++;
                        break;
                    case STBTT_vcurve:
                        qcount++;
                        break;
                    case STBTT_vcubic:
                        ccount++;
                        break;
                }
            path->prepare(mcount, lcount, qcount, ccount, 0);
            for (i = 0; i < nverts; i++)
                switch (v[i].type) {
                    case STBTT_vmove:
                        path.ref->moveTo(v[i].x, v[i].y);
                        break;
                    case STBTT_vline:
                        path.ref->lineTo(v[i].x, v[i].y);
                        break;
                    case STBTT_vcurve:
                        path.ref->quadTo(v[i].cx, v[i].cy, v[i].x, v[i].y);
                        break;
                    case STBTT_vcubic:
                        path.ref->cubicTo(v[i].cx, v[i].cy, v[i].cx1, v[i].cy1, v[i].x, v[i].y);
                        break;
                }
            stbtt_FreeShape(& info, v);
            if (cacheable)
                cache.emplace(glyph, path);
        }
        return path;
    }
    void writeGlyphs(uint8_t *utf8, std::vector<int>& glyphs) {
        for (int glyph = 0, step = 1, codepoint = 0, i = 0; step; i += step) {
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
            else if ((glyph = stbtt_FindGlyphIndex(& info, codepoint)) && stbtt_IsGlyphEmpty(& info, glyph) == 0)
                glyphs.emplace_back(glyph);
        }
    }
    Ra::Ref<Ra::Memory<uint8_t>> bytes;
    std::unordered_map<int, Ra::Path> cache;
    int refCount, monospace, avg, em, space, ascent, descent, lineGap, unitsPerEm, spaceGlyph;
    stbtt_fontinfo info;
    
    static Ra::Bounds layoutGlyphs(RasterizerFont& font, float size, Ra::Colorant color, Ra::Bounds bounds, bool rtl, bool single, bool right, const char *str, Ra::Scene& scene) {
        if (font.isEmpty() || str == nullptr)
            return { 0.f, 0.f, 0.f, 0.f };
        Ra::Bounds glyphBounds;
        int end, i, j, begin, len, l0, l1;
        std::vector<int> glyphs, lines;
        font.writeGlyphs((uint8_t *)str, glyphs);
        float s = size / float(font.unitsPerEm), width, lineHeight, space, x, y, xs[glyphs.size()];
        for (int k = 0; k < glyphs.size(); k++) xs[k] = FLT_MAX;
        width = (bounds.ux - bounds.lx) / s, lineHeight = font.ascent - font.descent + font.lineGap;
        space = font.monospace ?: font.space ?: lineHeight * 0.166f;
        x = 0.f, end = 0, len = (int)glyphs.size(), lines.emplace_back(end);
        do {
            for (; end < len && glyphs[end] < 0; end++)
                if (glyphs[end] != -RasterizerFont::nl)
                    x += (glyphs[end] == -RasterizerFont::tab ? 4 : 1) * (rtl ? -space : space);
                else if (!single)
                    x = 0.f, lines.emplace_back(end);
            for (begin = end; end < len && glyphs[end] > 0; end++) {}
            if (rtl)
                std::reverse(& glyphs[begin], & glyphs[end]);
            int advances[end - begin], *adv = advances, wx = 0, x1, wux = -INT_MAX;
            for (j = begin; j < end; j++, wx += *adv++) {
                stbtt_GetGlyphHMetrics(& font.info, glyphs[j], adv, NULL);
                if (j < end - 1)
                    *adv += stbtt_GetGlyphKernAdvance(& font.info, glyphs[j], glyphs[j + 1]);
                stbtt_GetGlyphBox(& font.info, glyphs[j], nullptr, nullptr, & x1, nullptr);
                x1 += wx, wux = wux > x1 ? wux : x1;
            }
            if (!single && (fabsf(x) + wux > width))
                x = 0.f, lines.emplace_back(begin);
            if (rtl)
                x -= wux;
            for (adv = advances, j = begin; j < end; j++, x += *adv++)
                if (!(single && (fabsf(x) + *adv > width)))
                    xs[j] = x;
            if (rtl)
                x -= wux;
        } while (end < len);
        lines.emplace_back(end);
        for (y = -font.ascent, i = 0; i < lines.size() - 1; i++, y -= lineHeight)
            if ((l0 = lines[i]) != (l1 = lines[i + 1])) {
                float dx = 0.f;
                if (rtl)
                    dx = width;
                else
                    for (int j = right ? l1 - 1 : l0; j >= 0 && j < l1; j += (right ? -1 : 1))
                        if ((x = xs[j]) != FLT_MAX) {
                            Ra::Path path = font.glyphPath(glyphs[j], true);
                            dx += right ? width - (x + path.ref->bounds.ux) : -path.ref->bounds.lx;
                            break;
                        }
                for (int j = l0; j < l1; j++) 
                    if ((x = xs[j]) != FLT_MAX) {
                        Ra::Path path = font.glyphPath(glyphs[j], true);
                        Ra::Transform ctm(s, 0.f, 0.f, s, (x + dx) * s + bounds.lx, y * s + bounds.uy);
                        scene.addPath(path, ctm, color, 0.f, 0);
                        glyphBounds.extend(Ra::Bounds(path.ref->bounds.unit(ctm)));
                    }
            }
        return glyphBounds;
    }
    static void writeGlyphGrid(RasterizerFont& font, float size, Ra::Colorant color, Ra::Scene& scene) {
        if (font.isEmpty())
            return;
        float s = size / float(font.unitsPerEm);
        for (int d = ceilf(sqrtf(font.info.numGlyphs)), glyph = 0; glyph < font.info.numGlyphs; glyph++)
            if (stbtt_IsGlyphEmpty(& font.info, glyph) == 0)
                scene.addPath(font.glyphPath(glyph, false), Ra::Transform(s, 0.f, 0.f, s, size * float(glyph % d), size * float(glyph / d)), color, 0.f, 0);
    }
    static void layoutGlyphsOnArc(Ra::Scene& glyphs, float cx, float cy, float r, float theta, Ra::Scene& scene) {
        Ra::Path path;  Ra::Transform m, ctm;  Ra::Bounds b;  float lx = 0.f, bx, by, rot, px, py;
        for (int i = 0; i < glyphs.count; i++) {
            path = glyphs.paths[i], m = glyphs.ctms[i], b = Ra::Bounds(path->bounds.unit(m));
            lx = i == 0 ? b.lx : lx;
            bx = 0.5f * (b.lx + b.ux), by = m.ty, rot = theta - (bx - lx) / r;
            px = cx + r * cosf(rot), py = cy + r * sinf(rot);
            ctm = m.concat(Ra::Transform::rst(rot - 0.5 * M_PI), bx, by), ctm.tx += px - bx, ctm.ty += py - by;
            scene.addPath(path, ctm, glyphs.colors[i], 0.f, 0);
        }
    }
};
