//
//  RasterizerTrueType.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 04/01/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//

#import <unordered_map>
#import "Rasterizer.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#import "stb_truetype.h"

static const uint32_t crc32Table[256] = {
    0x00000000L, 0xF26B8303L, 0xE13B70F7L, 0x1350F3F4L,
    0xC79A971FL, 0x35F1141CL, 0x26A1E7E8L, 0xD4CA64EBL,
    0x8AD958CFL, 0x78B2DBCCL, 0x6BE22838L, 0x9989AB3BL,
    0x4D43CFD0L, 0xBF284CD3L, 0xAC78BF27L, 0x5E133C24L,
    0x105EC76FL, 0xE235446CL, 0xF165B798L, 0x030E349BL,
    0xD7C45070L, 0x25AFD373L, 0x36FF2087L, 0xC494A384L,
    0x9A879FA0L, 0x68EC1CA3L, 0x7BBCEF57L, 0x89D76C54L,
    0x5D1D08BFL, 0xAF768BBCL, 0xBC267848L, 0x4E4DFB4BL,
    0x20BD8EDEL, 0xD2D60DDDL, 0xC186FE29L, 0x33ED7D2AL,
    0xE72719C1L, 0x154C9AC2L, 0x061C6936L, 0xF477EA35L,
    0xAA64D611L, 0x580F5512L, 0x4B5FA6E6L, 0xB93425E5L,
    0x6DFE410EL, 0x9F95C20DL, 0x8CC531F9L, 0x7EAEB2FAL,
    0x30E349B1L, 0xC288CAB2L, 0xD1D83946L, 0x23B3BA45L,
    0xF779DEAEL, 0x05125DADL, 0x1642AE59L, 0xE4292D5AL,
    0xBA3A117EL, 0x4851927DL, 0x5B016189L, 0xA96AE28AL,
    0x7DA08661L, 0x8FCB0562L, 0x9C9BF696L, 0x6EF07595L,
    0x417B1DBCL, 0xB3109EBFL, 0xA0406D4BL, 0x522BEE48L,
    0x86E18AA3L, 0x748A09A0L, 0x67DAFA54L, 0x95B17957L,
    0xCBA24573L, 0x39C9C670L, 0x2A993584L, 0xD8F2B687L,
    0x0C38D26CL, 0xFE53516FL, 0xED03A29BL, 0x1F682198L,
    0x5125DAD3L, 0xA34E59D0L, 0xB01EAA24L, 0x42752927L,
    0x96BF4DCCL, 0x64D4CECFL, 0x77843D3BL, 0x85EFBE38L,
    0xDBFC821CL, 0x2997011FL, 0x3AC7F2EBL, 0xC8AC71E8L,
    0x1C661503L, 0xEE0D9600L, 0xFD5D65F4L, 0x0F36E6F7L,
    0x61C69362L, 0x93AD1061L, 0x80FDE395L, 0x72966096L,
    0xA65C047DL, 0x5437877EL, 0x4767748AL, 0xB50CF789L,
    0xEB1FCBADL, 0x197448AEL, 0x0A24BB5AL, 0xF84F3859L,
    0x2C855CB2L, 0xDEEEDFB1L, 0xCDBE2C45L, 0x3FD5AF46L,
    0x7198540DL, 0x83F3D70EL, 0x90A324FAL, 0x62C8A7F9L,
    0xB602C312L, 0x44694011L, 0x5739B3E5L, 0xA55230E6L,
    0xFB410CC2L, 0x092A8FC1L, 0x1A7A7C35L, 0xE811FF36L,
    0x3CDB9BDDL, 0xCEB018DEL, 0xDDE0EB2AL, 0x2F8B6829L,
    0x82F63B78L, 0x709DB87BL, 0x63CD4B8FL, 0x91A6C88CL,
    0x456CAC67L, 0xB7072F64L, 0xA457DC90L, 0x563C5F93L,
    0x082F63B7L, 0xFA44E0B4L, 0xE9141340L, 0x1B7F9043L,
    0xCFB5F4A8L, 0x3DDE77ABL, 0x2E8E845FL, 0xDCE5075CL,
    0x92A8FC17L, 0x60C37F14L, 0x73938CE0L, 0x81F80FE3L,
    0x55326B08L, 0xA759E80BL, 0xB4091BFFL, 0x466298FCL,
    0x1871A4D8L, 0xEA1A27DBL, 0xF94AD42FL, 0x0B21572CL,
    0xDFEB33C7L, 0x2D80B0C4L, 0x3ED04330L, 0xCCBBC033L,
    0xA24BB5A6L, 0x502036A5L, 0x4370C551L, 0xB11B4652L,
    0x65D122B9L, 0x97BAA1BAL, 0x84EA524EL, 0x7681D14DL,
    0x2892ED69L, 0xDAF96E6AL, 0xC9A99D9EL, 0x3BC21E9DL,
    0xEF087A76L, 0x1D63F975L, 0x0E330A81L, 0xFC588982L,
    0xB21572C9L, 0x407EF1CAL, 0x532E023EL, 0xA145813DL,
    0x758FE5D6L, 0x87E466D5L, 0x94B49521L, 0x66DF1622L,
    0x38CC2A06L, 0xCAA7A905L, 0xD9F75AF1L, 0x2B9CD9F2L,
    0xFF56BD19L, 0x0D3D3E1AL, 0x1E6DCDEEL, 0xEC064EEDL,
    0xC38D26C4L, 0x31E6A5C7L, 0x22B65633L, 0xD0DDD530L,
    0x0417B1DBL, 0xF67C32D8L, 0xE52CC12CL, 0x1747422FL,
    0x49547E0BL, 0xBB3FFD08L, 0xA86F0EFCL, 0x5A048DFFL,
    0x8ECEE914L, 0x7CA56A17L, 0x6FF599E3L, 0x9D9E1AE0L,
    0xD3D3E1ABL, 0x21B862A8L, 0x32E8915CL, 0xC083125FL,
    0x144976B4L, 0xE622F5B7L, 0xF5720643L, 0x07198540L,
    0x590AB964L, 0xAB613A67L, 0xB831C993L, 0x4A5A4A90L,
    0x9E902E7BL, 0x6CFBAD78L, 0x7FAB5E8CL, 0x8DC0DD8FL,
    0xE330A81AL, 0x115B2B19L, 0x020BD8EDL, 0xF0605BEEL,
    0x24AA3F05L, 0xD6C1BC06L, 0xC5914FF2L, 0x37FACCF1L,
    0x69E9F0D5L, 0x9B8273D6L, 0x88D28022L, 0x7AB90321L,
    0xAE7367CAL, 0x5C18E4C9L, 0x4F48173DL, 0xBD23943EL,
    0xF36E6F75L, 0x0105EC76L, 0x12551F82L, 0xE03E9C81L,
    0x34F4F86AL, 0xC69F7B69L, 0xD5CF889DL, 0x27A40B9EL,
    0x79B737BAL, 0x8BDCB4B9L, 0x988C474DL, 0x6AE7C44EL,
    0xBE2DA0A5L, 0x4C4623A6L, 0x5F16D052L, 0xAD7D5351L
};

static uint32_t crc32(uint32_t crc, const void *buf, size_t size) {
    const uint8_t *p = (uint8_t *)buf;
    while (size--)
        crc = crc32Table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return crc;
}

struct RasterizerTrueType {
    struct Font {
        Font() { empty(); }
        void empty() { monospace = space = 0.f, bzero(& info, sizeof(info)), cache.clear(), crc = 0; }
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
                            crc = crc32(info.numGlyphs, n, length * sizeof(*n));
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
                    path.ref->hash = (size_t(crc) << 32) | glyph;
                }
            }
            return path;
        }
        std::unordered_map<int, Rasterizer::Path> cache;
        uint32_t crc;
        float monospace, space;
        stbtt_fontinfo info;
    };
    
    static Rasterizer::Path writeGlyphs(Font& font, float size, uint8_t *bgra, Rasterizer::Bounds bounds, bool left, const char *str,
                            std::vector<uint32_t>& bgras,
                            std::vector<Rasterizer::AffineTransform>& ctms,
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
                    bgras.emplace_back(*((uint32_t *)bgra));
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
        shapes.ref->allocShapes(paths.size() - base);
        if (0)
            memset(shapes.ref->circles, 0x01, shapes.ref->shapesCount * sizeof(bool));
        Rasterizer::AffineTransform *dst = shapes.ref->shapes;
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
                paths.emplace_back(font.glyphPath(glyph, false));
            }
    }
};
