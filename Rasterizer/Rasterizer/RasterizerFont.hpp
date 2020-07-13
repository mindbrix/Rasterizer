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
    void empty() { monospace = space = ascent = descent = lineGap = unitsPerEm = 0, bzero(& info, sizeof(info)), cache.clear(); }
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
                        int l = 0, M = 0, spc = 0;
                        stbtt_GetCodepointHMetrics(& info, 'l', & l, NULL);
                        stbtt_GetCodepointHMetrics(& info, 'M', & M, NULL);
                        stbtt_GetCodepointHMetrics(& info, ' ', & spc, NULL);
                        if (l && M && spc) {
                            space = spc, monospace = l == M && M == spc ? spc : 0;
                            stbtt_GetFontVMetrics(& info, & ascent, & descent, & lineGap);
                            unitsPerEm = ceilf(1.f / stbtt_ScaleForMappingEmToPixels(& info, 1.f));
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
    int refCount, monospace, space, ascent, descent, lineGap, unitsPerEm;
    stbtt_fontinfo info;
    
    static Ra::Bounds layoutColumns(RasterizerFont& font, float emSize, Ra::Colorant color, Ra::Bounds bounds, int *colWidths, int colCount, Ra::Row<size_t>& indices, Ra::Row<char>& strings, Ra::Scene& scene) {
        int idx;
        float emWidth = emSize * floorf((bounds.ux - bounds.lx) / emSize), lx = 0.f, ux = 0.f, uy = bounds.uy;
        for (idx = 0; idx < indices.end; idx++, lx = ux) {
            ux = lx + emSize * colWidths[idx % colCount], ux = ux < emWidth ? ux : emWidth;
            if (lx != ux) {
                Ra::Bounds b = { bounds.lx + lx, -FLT_MAX, bounds.lx + ux, uy };
                layoutGlyphs(font, emSize, color, b, false, true, false, strings.base + indices.base[idx], scene);
            }
            if (colCount - idx % colCount == 1)
                lx = ux = 0.f, uy -= emSize / float(font.unitsPerEm) * (font.ascent - font.descent + font.lineGap);
        }
        return { 0.f, 0.f, 0.f, 0.f };
    }
    static Ra::Bounds layoutGlyphs(RasterizerFont& font, float emSize, Ra::Colorant color, Ra::Bounds bounds, bool rtl, bool single, bool right, const char *str, Ra::Scene& scene) {
        if (font.isEmpty() || str == nullptr)
            return { 0.f, 0.f, 0.f, 0.f };
        Ra::Bounds glyphBounds;
        std::vector<int> glyphs;  font.writeGlyphs((uint8_t *)str, glyphs);
        float scale = emSize / float(font.unitsPerEm);
        int width, lineHeight, end, x, y, len, l0, i, begin, xs[glyphs.size()];
        width = ceilf((bounds.ux - bounds.lx) / scale), lineHeight = font.ascent - font.descent + font.lineGap;
        x = end = l0 = 0, y = -lineHeight, len = (int)glyphs.size();
        do {
            for (; end < len && glyphs[end] < 0; end++) {
                if (glyphs[end] != -RasterizerFont::nl)
                    x += (glyphs[end] == -RasterizerFont::tab ? 4 : 1) * (rtl ? -font.space : font.space);
                else if (!single) {
                    glyphBounds.extend(writeLine(font, scale, color, bounds, & glyphs[0], l0, end, xs, y, rtl, right, scene));
                    x = 0, y -= lineHeight, l0 = end + 1;
                }
            }
            for (begin = end; end < len && glyphs[end] > 0; end++) {}
            if (rtl)
                std::reverse(& glyphs[begin], & glyphs[end]);
            int advances[end - begin], *adv, x0 = 0, x1, wux = -INT_MAX;
            for (adv = advances, i = begin; i < end; i++, x0 += *adv++) {
                stbtt_GetGlyphHMetrics(& font.info, glyphs[i], adv, NULL);
                if (i < end - 1)
                    *adv += stbtt_GetGlyphKernAdvance(& font.info, glyphs[i], glyphs[i + 1]);
                stbtt_GetGlyphBox(& font.info, glyphs[i], nullptr, nullptr, & x1, nullptr);
                x1 += x0, wux = wux > x1 ? wux : x1;
            }
            if (!single && abs(x) + wux > width) {
                glyphBounds.extend(writeLine(font, scale, color, bounds, & glyphs[0], l0, begin, xs, y, rtl, right, scene));
                x = 0, y -= lineHeight, l0 = begin;
            }
            x1 = rtl ? x - x0 : x;
            for (adv = advances, i = begin; i < end; i++, x1 += *adv++) {
                xs[i] = x1;
                if (single && abs(x1) + *adv > width)
                    glyphs[i] = 0;
            }
            x = rtl ? x - x0 : x + x0;
        } while (end < len);
        glyphBounds.extend(writeLine(font, scale, color, bounds, & glyphs[0], l0, end, xs, y, rtl, right, scene));
        return glyphBounds;
    }
    static Ra::Bounds writeLine(RasterizerFont& font, float scale, Ra::Colorant color, Ra::Bounds bounds, int *glyphs, int l0, int l1, int *xs, int y, bool rtl, bool right, Ra::Scene& scene) {
        Ra::Bounds glyphBounds;
        int dx = 0, width = ceilf((bounds.ux - bounds.lx) / scale);
        if (rtl)
            dx = width;
        else
            for (int j = right ? l1 - 1 : l0; j >= 0 && j < l1; j += (right ? -1 : 1))
                if (glyphs[j] > 0) {
                    Ra::Path path = font.glyphPath(glyphs[j], true);
                    dx += right ? width - (xs[j] + path.ref->bounds.ux) : 0;
                    break;
                }
        for (int j = l0; j < l1; j++)
            if (glyphs[j] > 0) {
                Ra::Path path = font.glyphPath(glyphs[j], true);
                Ra::Transform ctm(scale, 0.f, 0.f, scale, (xs[j] + dx) * scale + bounds.lx, y * scale + bounds.uy);
                scene.addPath(path, ctm, color, 0.f, 0);
                glyphBounds.extend(Ra::Bounds(path.ref->bounds.unit(ctm)));
            }
        return glyphBounds;
    }
    static void writeGlyphGrid(RasterizerFont& font, float emSize, Ra::Colorant color, Ra::Scene& scene) {
        if (font.isEmpty())
            return;
        float scale = emSize / float(font.unitsPerEm);
        for (int d = ceilf(sqrtf(font.info.numGlyphs)), glyph = 0; glyph < font.info.numGlyphs; glyph++)
            if (stbtt_IsGlyphEmpty(& font.info, glyph) == 0)
                scene.addPath(font.glyphPath(glyph, false), Ra::Transform(scale, 0.f, 0.f, scale, emSize * float(glyph % d), emSize * float(glyph / d)), color, 0.f, 0);
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
