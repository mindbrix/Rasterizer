//
//  Rasterizer.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 17/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import <vector>
#include <immintrin.h>

#pragma clang diagnostic ignored "-Wcomma"


struct Rasterizer {
    template<typename T, typename C>
    static void radixSort(T *in, int n, C *counts0, C *counts1, T *out) {
        T x;
        C *c, *src, *dst;
        memset(counts0, 0, sizeof(C) * 256);
        memset(counts1, 0, sizeof(C) * 256);
        int i;
        for (i = 0; i < n; i++)
            counts0[in[i] & 0xFF]++;
        
        __m128i shuffle_mask = _mm_set1_epi32(0x0F0E0F0E);
        __m128i offset, j, *src8, *end8;
        for (offset = _mm_setzero_si128(), src8 = (__m128i *)counts0, end8 = src8 + 32; src8 < end8; src8++) {
            j = _mm_loadu_si128(src8);
            j = _mm_add_epi16(j, _mm_slli_si128(j, 2));
            j = _mm_add_epi16(j, _mm_slli_si128(j, 4));
            j = _mm_add_epi16(j, _mm_slli_si128(j, 8));
            j = _mm_add_epi16(j, offset);
            _mm_storeu_si128(src8, j);
            offset = _mm_shuffle_epi8(j, shuffle_mask);
        }
//        for (src = counts0, dst = src + 1, i = 1; i < 256; i++)
//            *dst++ += *src++;

        for (i = n - 1; i >= 0; i--) {
            x = in[i];
            c = counts0 + (x & 0xFF);
            out[*c - 1] = x;
            counts1[(x >> 8) & 0xFF]++;
            (*c)--;
        }
        for (offset = _mm_setzero_si128(), src8 = (__m128i *)counts1, end8 = src8 + 32; src8 < end8; src8++) {
            j = _mm_loadu_si128(src8);
            j = _mm_add_epi16(j, _mm_slli_si128(j, 2));
            j = _mm_add_epi16(j, _mm_slli_si128(j, 4));
            j = _mm_add_epi16(j, _mm_slli_si128(j, 8));
            j = _mm_add_epi16(j, offset);
            _mm_storeu_si128(src8, j);
            offset = _mm_shuffle_epi8(j, shuffle_mask);
        }
//        for (src = counts1, dst = src + 1, i = 1; i < 256; i++)
//            *dst++ += *src++;
        for (i = n - 1; i >= 0; i--) {
            x = out[i];
            c = counts1 + ((x >> 8) & 0xFF);
            in[*c - 1] = x;
            (*c)--;
        }
    }
    static const size_t kDeltasDimension = 128;
    
    struct AffineTransform {
        AffineTransform(float a, float b, float c, float d, float tx, float ty) : a(a), b(b), c(c), d(d), tx(tx), ty(ty) {}
        
        inline AffineTransform concat(AffineTransform t) {
            return {
                t.a * a + t.b * c,            t.a * b + t.b * d,
                t.c * a + t.d * c,            t.c * b + t.d * d,
                t.tx * a + t.ty * c + tx,     t.tx * b + t.ty * d + ty
            };
        }
        inline AffineTransform unit(float lx, float ly, float ux, float uy) {
            float w = ux - lx, h = uy - ly;
            return { a * w, b * w, c * h, d * h, lx * a + ly * c + tx, lx * b + ly * d + ty };
        }
        float a, b, c, d, tx, ty;
    };
    struct Bounds {
        Bounds() {}
        Bounds(float lx, float ly, float ux, float uy) : lx(lx), ly(ly), ux(ux), uy(uy) {}
        
        Bounds integral() { return { floorf(lx), floorf(ly), ceilf(ux), ceilf(uy) }; }
        Bounds intersected(Bounds other) {
            return {
                lx < other.lx ? other.lx : lx > other.ux ? other.ux : lx,
                ly < other.ly ? other.ly : ly > other.uy ? other.uy : ly,
                ux < other.lx ? other.lx : ux > other.ux ? other.ux : ux,
                uy < other.ly ? other.ly : uy > other.uy ? other.uy : uy,
            };
        }
        inline bool isZero() { return lx == ux || ly == uy; }
        Bounds transform(AffineTransform ctm) {
            AffineTransform t = ctm.unit(lx, ly, ux, uy);
            float wa = t.a < 0 ? 1 : 0, wb = t.b < 0 ? 1 : 0, wc = t.c < 0 ? 1 : 0, wd = t.d < 0 ? 1 : 0;
            return {
                t.tx + wa * t.a + wc * t.c,
                t.ty + wb * t.b + wd * t.d,
                t.tx + (1 - wa) * t.a + (1 - wc) * t.c,
                t.ty + (1 - wb) * t.b + (1 - wd) * t.d };
        }
        float lx, ly, ux, uy;
    };
    struct Path {
        struct Atom {
            static const size_t kCapacity = 15;
            enum Type { kNull = 0, kMove, kLine, kQuadratic, kCubic, kClose };
            Atom() {
                memset(types, kNull, sizeof(types));
            }
            float       points[30];
            uint8_t     types[8];
        };
        Path() : index(Atom::kCapacity) {}
        
        float *alloc(Atom::Type type, size_t size) {
            if (index + size > Atom::kCapacity)
                index = 0, atoms.emplace_back();
            
            atoms.back().types[index / 2] |= (uint8_t(type) << ((index & 1) * 4));
            float *points = atoms.back().points + index * 2;
            index += size;
            return points;
        }
        void moveTo(float x, float y) {
            float *points = alloc(Atom::kMove, 1);
            *points++ = x, *points++ = y;
        }
        void lineTo(float x, float y) {
            float *points = alloc(Atom::kLine, 1);
            *points++ = x, *points++ = y;
        }
        void quadTo(float cx, float cy, float x, float y) {
            float *points = alloc(Atom::kQuadratic, 2);
            *points++ = cx, *points++ = cy;
            *points++ = x, *points++ = y;
        }
        void cubicTo(float cx0, float cy0, float cx1, float cy1, float x, float y) {
            float *points = alloc(Atom::kCubic, 3);
            *points++ = cx0, *points++ = cy0;
            *points++ = cx1, *points++ = cy1;
            *points++ = x, *points++ = y;
        }
        void close() {
            alloc(Atom::kClose, 1);
        }
        std::vector<Atom> atoms;
        size_t index;
    };
    struct Bitmap {
        Bitmap() {}
        Bitmap(void *data, size_t width, size_t height, size_t rowBytes, size_t bpp) : data((uint8_t *)data), width(width), height(height), rowBytes(rowBytes), bpp(bpp), bytespp(bpp / 8) {}
        
        inline uint32_t *pixelAddress(short x, short y) { return (uint32_t *)(data + rowBytes * (height - 1 - y) + x * bytespp); }
        
        uint8_t *data;
        size_t width, height, rowBytes, bpp, bytespp;
    };
    struct Scanline {
        struct Delta {
            Delta() {}
            inline Delta(float x, float delta) : x(x), delta(delta) {}
            inline bool operator< (const Delta& other) const { return x < other.x; }
            short x, delta;
        };
        Scanline() : size(0) { empty(); }
        
        void empty() { delta0 = idx = 0; }
        inline void insertDelta(float x, float delta) {
            if (idx >= size)
                deltas.resize(deltas.size() == 0 ? 8 : deltas.size() * 1.5), size = deltas.size(), base = & deltas[0];
            new (base + idx++) Delta(x, delta);
        }
        float delta0;
        size_t idx, size;
        Delta *base;
        std::vector<Delta> deltas;
    };
    struct Spanline {
        struct Span {
            Span() {}
            inline Span(float x, float w) : x(x), w(w) {}
            short x, w;
        };
        Spanline() : size(0) { empty(); }
        
        void empty() { idx = 0; }
        inline void insertSpan(float x, float w) {
            if (idx >= size)
                spans.resize(spans.size() == 0 ? 8 : spans.size() * 1.5), size = spans.size(), base = & spans[0];
            new (base + idx++) Span(x, w);
        }
        size_t idx, size;
        Span *base;
        std::vector<Span> spans;
    };
    struct Context {
        Context() { memset(deltas, 0, sizeof(deltas)); }
        
        void setBitmap(Bitmap bitmap) {
            this->bitmap = bitmap;
            if (scanlines.size() != bitmap.height)
                scanlines.resize(bitmap.height);
            if (spanlines.size() != bitmap.height)
                spanlines.resize(bitmap.height);
            clipBounds = Bounds(0, 0, bitmap.width, bitmap.height);
        }
        Bitmap bitmap;
        Bounds clipBounds;
        float deltas[kDeltasDimension * kDeltasDimension];
        uint8_t mask[kDeltasDimension * kDeltasDimension];
        std::vector<Scanline> scanlines;
        std::vector<Spanline> spanlines;
    };
    
    static void writeMaskRowSSE(float *deltas, size_t w, uint8_t *mask) {
        float cover = 0, alpha;
        
        __m128 offset = _mm_setzero_ps(), sign_mask = _mm_set1_ps(-0.);
        __m128i shuffle_mask = _mm_set1_epi32(0x0c080400);
        __m128 x, y, z;
        while (w >> 2) {
            x = _mm_loadu_ps(deltas);
            *deltas++ = 0, *deltas++ = 0, *deltas++ = 0, *deltas++ = 0;
            x = _mm_add_ps(x, _mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(x), 4)));
            x = _mm_add_ps(x, _mm_shuffle_ps(_mm_setzero_ps(), x, 0x40));
            x = _mm_add_ps(x, offset);
            y = _mm_andnot_ps(sign_mask, x);
            y = _mm_min_ps(y, _mm_set1_ps(255.0));
            z = _mm_cvttps_epi32(y);
            z = _mm_shuffle_epi8(z, shuffle_mask);
            _mm_store_ss((float *)mask, _mm_castsi128_ps(z));
            offset = _mm_shuffle_ps(x, x, 0xFF);
            w -= 4, mask += 4;
        }
        _mm_store_ss(& cover, offset);
        while (w--) {
            cover += *deltas, *deltas++ = 0;
            alpha = fabsf(cover);
            *mask++ = alpha < 255.f ? alpha : 255.f;
        }
    }
    
    static void writeDeltasToMask(float *deltas, Bounds device, uint8_t *mask) {
        size_t w = device.ux - device.lx, h = device.uy - device.ly;
        for (size_t y = 0; y < h; y++, deltas += w, mask += w)
            writeMaskRowSSE(deltas, w, mask);
    }
    
    static void writeScanlinesToSpans(std::vector<Scanline>& scanlines, Bounds device, Bounds clipped, std::vector<Spanline>& spanlines) {
        const float scale = 255.5f / 32767.f;
        float x, y, ix, cover, alpha;
        uint8_t a;
        short counts0[256], counts1[256];
        size_t idx;
        idx = clipped.ly - device.ly;
        Scanline *scanline = & scanlines[idx];
        Spanline *spanline = & spanlines[idx];
        Scanline::Delta *begin, *end, *delta;
        for (y = clipped.ly; y < clipped.uy; y++, scanline++, spanline++) {
            if (scanline->idx == 0)
                continue;
            
            begin = & scanline->deltas[0], end = begin + scanline->idx;
            if (scanline->idx > 32) {
                uint32_t mem0[scanline->idx];
                radixSort((uint32_t *)begin, int(scanline->idx), counts0, counts1, mem0);
            } else
                std::sort(begin, end);
            
            cover = scanline->delta0;
            alpha = fabsf(cover);
            a = alpha < 255.f ? alpha : 255.f;
            
            x = a ? clipped.lx : scanline->deltas[0].x;
            for (delta = begin; delta < end; delta++) {
                if (delta->x != x) {
                    alpha = fabsf(cover);
                    a = alpha < 255.f ? alpha : 255.f;
                    
                    if (a > 254)
                        spanline->insertSpan(device.lx + x, delta->x - x);
                    else if (a > 0)
                        for (ix = x; ix < delta->x; ix++)
                            spanline->insertSpan(device.lx + ix, -a);
                    x = delta->x;
                }
                cover += float(delta->delta) * scale;
            }
        }
        if (device.isZero())
            for (scanline = & scanlines[idx], y = clipped.ly; y < clipped.uy; y++, scanline++)
                scanline->empty();
        else
            for (scanline = & scanlines[0], y = device.ly; y < device.uy; y++, scanline++)
                scanline->empty();
    }
    
    static void writeMaskToBitmapSSE(uint8_t *mask, size_t maskRowBytes, size_t w, size_t h, uint32_t bgra, uint32_t *pixelAddress, size_t rowBytes) {
        uint32_t *dst;
        uint8_t *src, *components, *d;
        components = (uint8_t *)& bgra;
        __m128 bgra4 = _mm_set_ps(float(components[3]), float(components[2]), float(components[1]), float(components[0]));
        __m128 a0, a1, a2, a3, m0, m1, m2, m3, d0, d1, d2, d3;
        __m128i a32, a16, a16_0, a16_1, a8;
        size_t columns;
        
        while (h--) {
            dst = pixelAddress, src = mask, columns = w;
            while (columns >> 2) {
                if (src[0] || src[1] || src[2] || src[3]) {
                    a0 = _mm_set1_ps(float(src[0]) * 0.003921568627f);
                    a1 = _mm_set1_ps(float(src[1]) * 0.003921568627f);
                    a2 = _mm_set1_ps(float(src[2]) * 0.003921568627f);
                    a3 = _mm_set1_ps(float(src[3]) * 0.003921568627f);
                    
                    if (dst[0] == 0 && dst[1] == 0 && dst[2] == 0 && dst[3] == 0)
                        d0 = d1 = d2 = d3 = _mm_setzero_ps();
                    else {
                        d = (uint8_t *) & dst[0], d0 = _mm_set_ps(float(d[3]), float(d[2]), float(d[1]), float(d[0]));
                        d = (uint8_t *) & dst[1], d1 = _mm_set_ps(float(d[3]), float(d[2]), float(d[1]), float(d[0]));
                        d = (uint8_t *) & dst[2], d2 = _mm_set_ps(float(d[3]), float(d[2]), float(d[1]), float(d[0]));
                        d = (uint8_t *) & dst[3], d3 = _mm_set_ps(float(d[3]), float(d[2]), float(d[1]), float(d[0]));
                    }
                    m0 = _mm_add_ps(_mm_mul_ps(bgra4, a0), _mm_mul_ps(d0, _mm_sub_ps(_mm_set1_ps(1.f), a0)));
                    m1 = _mm_add_ps(_mm_mul_ps(bgra4, a1), _mm_mul_ps(d1, _mm_sub_ps(_mm_set1_ps(1.f), a1)));
                    m2 = _mm_add_ps(_mm_mul_ps(bgra4, a2), _mm_mul_ps(d2, _mm_sub_ps(_mm_set1_ps(1.f), a2)));
                    m3 = _mm_add_ps(_mm_mul_ps(bgra4, a3), _mm_mul_ps(d3, _mm_sub_ps(_mm_set1_ps(1.f), a3)));
                    
                    a16_0 = _mm_packs_epi32(_mm_cvttps_epi32(m0), _mm_cvttps_epi32(m1));
                    a16_1 = _mm_packs_epi32(_mm_cvttps_epi32(m2), _mm_cvttps_epi32(m3));
                    a8 = _mm_packus_epi16(a16_0, a16_1);
                    _mm_storeu_si128((__m128i *)dst, a8);
                }
                columns -= 4, src += 4, dst += 4;
            }
            while (columns--) {
                if (*src > 254)
                    *dst = bgra;
                else if (*src) {
                    a0 = _mm_set1_ps(float(*src) * 0.003921568627f);
                    m0 = _mm_mul_ps(bgra4, a0);
                    if (*dst) {
                        d = (uint8_t *)dst, d0 = _mm_set_ps(float(d[3]), float(d[2]), float(d[1]), float(d[0]));
                        m0 = _mm_add_ps(m0, _mm_mul_ps(d0, _mm_sub_ps(_mm_set1_ps(1.f), a0)));
                    }
                    a32 = _mm_cvttps_epi32(m0);
                    a16 = _mm_packs_epi32(a32, a32);
                    a8 = _mm_packus_epi16(a16, a16);
                    *dst = _mm_cvtsi128_si32(a8);
                }
                src++, dst++;
            }
            pixelAddress -= rowBytes / 4;
            mask += maskRowBytes;
        }
    }
    
    static void writeMaskToBitmap(uint8_t *mask, Bounds device, Bounds clipped, uint32_t bgra, Bitmap bitmap) {
        size_t maskRowBytes = device.ux - device.lx;
        size_t offset = maskRowBytes * (clipped.ly - device.ly) + (clipped.lx - device.lx);
        size_t w = clipped.ux - clipped.lx, h = clipped.uy - clipped.ly;
        writeMaskToBitmapSSE(mask + offset, maskRowBytes, w, h, bgra, bitmap.pixelAddress(clipped.lx, clipped.ly), bitmap.rowBytes);
    }
    
    static void writeSpansToBitmap(std::vector<Spanline>& spanlines, Bounds device, Bounds clipped, uint32_t color, Bitmap bitmap) {
        float y, lx, ux, src0, src1, src2, src3, alpha, dst0, dst1, dst2, dst3;
        uint8_t *components = (uint8_t *) & color;
        src0 = components[0], src1 = components[1], src2 = components[2], src3 = components[3];
        uint32_t *pixelAddress;
        Spanline *spanline = & spanlines[clipped.ly - device.ly];
        Spanline::Span *span, *end;
        for (y = clipped.ly; y < clipped.uy; y++, spanline->empty(), spanline++) {
            for (span = & spanline->spans[0], end = & spanline->spans[spanline->idx]; span < end; span++) {
                lx = span->x, ux = lx + (span->w > 0 ? span->w : 1);
                lx = lx < clipped.lx ? clipped.lx : lx > clipped.ux ? clipped.ux : lx;
                ux = ux < clipped.lx ? clipped.lx : ux > clipped.ux ? clipped.ux : ux;
                if (lx != ux) {
                    pixelAddress = bitmap.pixelAddress(lx, y);
                    if (span->w > 0)
                        memset_pattern4(pixelAddress, & color, (ux - lx) * bitmap.bytespp);
                    else {
                        alpha = float(-span->w) * 0.003921568627f;
                        components = (uint8_t *)pixelAddress;
                        if (*pixelAddress == 0)
                            *components++ = src0 * alpha, *components++ = src1 * alpha, *components++ = src2 * alpha, *components++ = src3 * alpha;
                        else {
                            dst0 = *components, *components++ = dst0 * (1.f - alpha) + src0 * alpha;
                            dst1 = *components, *components++ = dst1 * (1.f - alpha) + src1 * alpha;
                            dst2 = *components, *components++ = dst2 * (1.f - alpha) + src2 * alpha;
                            dst3 = *components, *components++ = dst3 * (1.f - alpha) + src3 * alpha;
                        }
                    }
                }
            }
        }
    }
    
    static void writePathToBitmap(Path& path, Bounds bounds, AffineTransform ctm, uint32_t bgra, Context& context) {
        Bounds device = bounds.transform(ctm).integral();
        Bounds clipped = device.intersected(context.clipBounds);
        if (!clipped.isZero()) {
            AffineTransform deltasCTM = { ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx - device.lx, ctm.ty - device.ly };
            float e = 2e-3 / sqrtf(fabsf(ctm.a * ctm.d - ctm.b * ctm.c));
            float w = bounds.ux - bounds.lx, h = bounds.uy - bounds.ly, mx = (bounds.lx + bounds.ux) * 0.5, my = (bounds.ly + bounds.uy) * 0.5;
            AffineTransform offset(1, 0, 0, 1, mx, my);
            offset = offset.concat(AffineTransform(w / (w + e), 0, 0, h / (h + e), 0, 0));
            offset = offset.concat(AffineTransform(1, 0, 0, 1, -mx, -my));
            
            if ((device.ux - device.lx) * (device.uy - device.ly) < kDeltasDimension * kDeltasDimension) {
                writePathToDeltasOrScanlines(path, deltasCTM.concat(offset), context.deltas, device.ux - device.lx, nullptr);
                writeDeltasToMask(context.deltas, device, context.mask);
                writeMaskToBitmap(context.mask, device, clipped, bgra, context.bitmap);
            } else if ((device.uy - device.ly) < context.bitmap.height) {
                writePathToDeltasOrScanlines(path, deltasCTM.concat(offset), nullptr, 0, & context.scanlines[0]);
                writeScanlinesToSpans(context.scanlines, device, clipped, context.spanlines);
                writeSpansToBitmap(context.spanlines, device, clipped, bgra, context.bitmap);
            } else {
                writeClippedPathToScanlines(path, ctm, context.clipBounds, & context.scanlines[0]);
                writeScanlinesToSpans(context.scanlines, Bounds(0, 0, 0, 0), clipped, context.spanlines);
                writeSpansToBitmap(context.spanlines, Bounds(0, 0, 0, 0), clipped, bgra, context.bitmap);
            }
        }
    }
    
    static void writeClippedPathToScanlines(Path& path, AffineTransform ctm, Bounds clipBounds, Scanline *scanlines) {
        float sx, sy, x0, y0, x1, y1, x2, y2, x3, y3, *p;
        size_t index;
        uint8_t type;
        x0 = y0 = sx = sy = FLT_MAX;
        for (Path::Atom& atom : path.atoms) {
            index = 0;
            for (type = 0xF & atom.types[0]; type != Path::Atom::kNull; type = 0xF & (atom.types[index / 2] >> ((index & 1) * 4))) {
                p = atom.points + index * 2;
                switch (type) {
                    case Path::Atom::kMove:
                        if (sx != FLT_MAX && (sx != x0 || sy != y0))
                            writeClippedSegmentToScanlines(x0, y0, sx, sy, clipBounds, scanlines);
                        sx = x0 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, sy = y0 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        index++;
                        break;
                    case Path::Atom::kLine:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        writeClippedSegmentToScanlines(x0, y0, x1, y1, clipBounds, scanlines);
                        x0 = x1, y0 = y1;
                        index++;
                        break;
                    case Path::Atom::kQuadratic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        writeClippedQuadraticToScanlines(x0, y0, x1, y1, x2, y2, clipBounds, scanlines);
                        x0 = x2, y0 = y2;
                        index += 2;
                        break;
                    case Path::Atom::kCubic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        x3 = p[4] * ctm.a + p[5] * ctm.c + ctm.tx, y3 = p[4] * ctm.b + p[5] * ctm.d + ctm.ty;
                        writeClippedCubicToScanlines(x0, y0, x1, y1, x2, y2, x3, y3, clipBounds, scanlines);
                        x0 = x3, y0 = y3;
                        index += 3;
                        break;
                    case Path::Atom::kClose:
                        index++;
                        break;
                }
            }
        }
        if (sx != FLT_MAX && (sx != x0 || sy != y0))
            writeClippedSegmentToScanlines(x0, y0, sx, sy, clipBounds, scanlines);
    }
    
    static void writeClippedSegmentToScanlines(float x0, float y0, float x1, float y1, Bounds clipBounds, Scanline *scanlines) {
        float lx, ly, ux, uy, cly, cuy, sx0, sy0, sx1, sy1, t0, t1, tmp, syt0, syt1;
        lx = x0 < x1 ? x0 : x1, ly = y0 < y1 ? y0 : y1;
        ux = x0 > x1 ? x0 : x1, uy = y0 > y1 ? y0 : y1;
        cly = ly < clipBounds.ly ? clipBounds.ly : ly > clipBounds.uy ? clipBounds.uy : ly;
        cuy = uy < clipBounds.ly ? clipBounds.ly : uy > clipBounds.uy ? clipBounds.uy : uy;
        if (cly != cuy) {
            sy0 = y0 < cly ? cly : y0 > cuy ? cuy : y0;
            sy1 = y1 < cly ? cly : y1 > cuy ? cuy : y1;
            if (x0 == x1) {
                sx0 = sx1 = x1 < clipBounds.lx ? clipBounds.lx : x1;
                writeSegmentToDeltasOrScanlines(sx0, sy0, sx1, sy1, 32767.f, nullptr, 0, scanlines);
            } else {
                sx0 = (sy0 - y0) / (y1 - y0) * (x1 - x0) + x0;
                sx1 = (sy1 - y0) / (y1 - y0) * (x1 - x0) + x0;
                t0 = (clipBounds.lx - sx0) / (sx1 - sx0);
                t0 = t0 < 0 ? 0 : t0 > 1 ? 1 : t0;
                t1 = (clipBounds.ux - sx0) / (sx1 - sx0);
                t1 = t1 < 0 ? 0 : t1 > 1 ? 1 : t1;
                if (t0 > t1)
                    tmp = t0, t0 = t1, t1 = tmp;
                syt0 = sy0 + t0 * (sy1 - sy0), syt1 = sy0 + t1 * (sy1 - sy0);
                
                if (x0 < x1) {
                    if (t0 > 0)
                        writeVerticalSegmentToScanlines(clipBounds.lx, sy0, syt0, scanlines);
                    if (t1 < 1)
                        writeVerticalSegmentToScanlines(clipBounds.ux, syt1, sy1, scanlines);
                } else if (x0 > x1) {
                    if (t1 < 1)
                        writeVerticalSegmentToScanlines(clipBounds.lx, syt1, sy1, scanlines);
                    if (t0 > 0)
                        writeVerticalSegmentToScanlines(clipBounds.ux, sy0, syt0, scanlines);
                }
                writeSegmentToDeltasOrScanlines(sx0 + t0 * (sx1 - sx0), syt0, sx0 + t1 * (sx1 - sx0), syt1, 32767.f, nullptr, 0, scanlines);
            }
        }
    }
    static void solveQuadratic(double A, double B, double C, float& t0, float& t1) {
        double disc, root, denom;
        if (fabs(A) < 1e-3)
            t0 = -C / B, t1 = FLT_MAX;
        else {
            disc = B * B - 4.0 * A * C;
            if (disc < 0)
                t0 = t1 = FLT_MAX;
            else {
                root = sqrt(disc), denom = 0.5 / A;
                t0 = (-B + root) * denom, t1 = (-B - root) * denom;
            }
        }
    }
    static void solveQuadratics(float n0, float n1, float n2, float nt0, float nt1, float *ts) {
        float A = n0 + n2 - n1 - n1, B = 2.f * (n1 - n0);
        solveQuadratic(A, B, n0 - nt0, ts[0], ts[1]);
        solveQuadratic(A, B, n0 - nt1, ts[2], ts[3]);
        for (int i = 0; i < 4; i++)
            ts[i] = ts[i] < 0 ? 0 : ts[i] > 1 ? 1 : ts[i];
        std::sort(& ts[0], & ts[4]);
    }
    static void writeClippedQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, float t0, float t1, Bounds clipBounds, bool clip, float *q) {
        float tm, s0, s1, sm, tx0, ty0, tx2, ty2, tmx, tmy, tx1, ty1;
        s0 = 1.f - t0, s1 = 1.f - t1;
        tm = (t0 + t1) * 0.5f, sm = 1.f - tm;
        tx0 = x0 * s0 * s0 + x1 * 2.f * s0 * t0 + x2 * t0 * t0;
        ty0 = y0 * s0 * s0 + y1 * 2.f * s0 * t0 + y2 * t0 * t0;
        tx1 = x0 * sm * sm + x1 * 2.f * sm * tm + x2 * tm * tm;
        ty1 = y0 * sm * sm + y1 * 2.f * sm * tm + y2 * tm * tm;
        tx2 = x0 * s1 * s1 + x1 * 2.f * s1 * t1 + x2 * t1 * t1;
        ty2 = y0 * s1 * s1 + y1 * 2.f * s1 * t1 + y2 * t1 * t1;
        tmx = (tx0 + tx2) * 0.5f, tmy = (ty0 + ty2) * 0.5f;
        tx1 = tmx + 2.f * (tx1 - tmx), ty1 = tmy + 2.f * (ty1 - tmy);
        tx0 = tx0 < clipBounds.lx ? clipBounds.lx : tx0 > clipBounds.ux ? clipBounds.ux : tx0;
        tx2 = tx2 < clipBounds.lx ? clipBounds.lx : tx2 > clipBounds.ux ? clipBounds.ux : tx2;
        ty0 = ty0 < clipBounds.ly ? clipBounds.ly : ty0 > clipBounds.uy ? clipBounds.uy : ty0;
        ty2 = ty2 < clipBounds.ly ? clipBounds.ly : ty2 > clipBounds.uy ? clipBounds.uy : ty2;
        if (clip) {
            tx1 = tx1 < clipBounds.lx ? clipBounds.lx : tx1 > clipBounds.ux ? clipBounds.ux : tx1;
            ty1 = ty1 < clipBounds.ly ? clipBounds.ly : ty1 > clipBounds.uy ? clipBounds.uy : ty1;
        }
        *q++ = tx0, *q++ = ty0, *q++ = tx1, *q++ = ty1, *q++ = tx2, *q++ = ty2;
    }
    static void writeClippedQuadraticToScanlines(float x0, float y0, float x1, float y1, float x2, float y2, Bounds clipBounds, Scanline *scanlines) {
        float lx, ly, ux, uy, cly, cuy, tys[4], txs[4], ts[6], ty0, ty1, q[6], t, s, t0, t1, mx, vx;
        size_t x, y;
        lx = x0 < x1 ? x0 : x1, ly = y0 < y1 ? y0 : y1;
        lx = lx < x2 ? lx : x2, ly = ly < y2 ? ly : y2;
        ux = x0 > x1 ? x0 : x1, uy = y0 > y1 ? y0 : y1;
        ux = ux > x2 ? ux : x2, uy = uy > y2 ? uy : y2;
        cly = ly < clipBounds.ly ? clipBounds.ly : ly > clipBounds.uy ? clipBounds.uy : ly;
        cuy = uy < clipBounds.ly ? clipBounds.ly : uy > clipBounds.uy ? clipBounds.uy : uy;
        if (cly != cuy) {
            if (lx < clipBounds.lx || ux > clipBounds.ux || ly < clipBounds.ly || uy > clipBounds.uy) {
                solveQuadratics(y0, y1, y2, clipBounds.ly, clipBounds.uy, tys);
                solveQuadratics(x0, x1, x2, clipBounds.lx, clipBounds.ux, txs);
                for (y = 0; y < 4; y += 2) {
                    ty0 = tys[y], ty1 = tys[y + 1];
                    if (ty0 != ty1) {
                        ts[0] = ty0, ts[1] = ty1;
                        for (x = 0; x < 4; x++)
                            t = txs[x], ts[x + 2] = t < ty0 ? ty0 : t > ty1 ? ty1 : t;
                        std::sort(& ts[0], & ts[6]);
                        for (x = 0; x < 5; x++) {
                            t0 = ts[x], t1 = ts[x + 1];
                            if (t0 != t1) {
                                if (x & 0x1) {
                                    writeClippedQuadratic(x0, y0, x1, y1, x2, y2, t0, t1, clipBounds, false, q);
                                    writeQuadraticToDeltasOrScanlines(q[0], q[1], q[2], q[3], q[4], q[5], 32767.f, nullptr, 0, scanlines);
                                } else {
                                    t = (t0 + t1) * 0.5f, s = 1.f - t;
                                    mx = x0 * s * s + x1 * 2.f * s * t + x2 * t * t;
                                    vx = mx <= clipBounds.lx ? clipBounds.lx : clipBounds.ux;
                                    writeClippedQuadratic(x0, y0, x1, y1, x2, y2, t0, t1, clipBounds, true, q);
                                    writeVerticalSegmentToScanlines(vx, q[1], q[3], scanlines);
                                    writeVerticalSegmentToScanlines(vx, q[3], q[5], scanlines);
                                }
                            }
                        }
                    }
                }
            } else
                writeQuadraticToDeltasOrScanlines(x0, y0, x1, y1, x2, y2, 32767.f, nullptr, 0, scanlines);
        }
    }
    static void solveCubic(float pa, float pb, float pc, float pd, float& t0, float& t1, float& t2) {
        const float limit = 1e-3;
        double a, b, c, d, p, q, q2, u1, v1, p3, discriminant, mp3, mp33, r, t, cosphi, phi, crtr, sd;
        
        a = (3.0 * pa - 6.0 * pb + 3.0 * pc), b = (-3.0 * pa + 3.0 * pb), c = pa, d = (-pa + 3.0 * pb - 3.0 * pc + pd);
        if (fabs(d) < limit) {
            solveQuadratic(a, b, c, t0, t1), t2 = FLT_MAX;
        } else {
            a /= d, b /= d, c /= d;
            p = (3.0 * b - a * a) / 3.0, p3 = p / 3.0, q = (2 * a * a * a - 9.0 * a * b + 27.0 * c) / 27.0, q2 = q / 2.0;
            discriminant = q2 * q2 + p3 * p3 * p3;
            
            if (discriminant < 0) {
                mp3 = -p/3, mp33 = mp3*mp3*mp3, r = sqrt(mp33), t = -q / (2*r), cosphi = t < -1 ? -1 : t > 1 ? 1 : t;
                phi = acos(cosphi), crtr = 2 * cbrt(fabs(r));
                t0 = crtr * cos(phi/3) - a/3;
                t1 = crtr * cos((phi+2*M_PI)/3) - a/3;
                t2 = crtr * cos((phi+4*M_PI)/3) - a/3;
            } else if (discriminant == 0) {
                u1 = q2 < 0 ? cbrt(-q2) : -cbrt(q2);
                t0 = 2*u1 - a/3;
                t1 = -u1 - a/3;
                t2 = FLT_MAX;
            } else {
                sd = sqrt(discriminant);
                u1 = cbrt(fabs(sd - q2));
                v1 = cbrt(fabs(sd + q2));
                t0 = u1 - v1 - a/3, t1 = t2 = FLT_MAX;
            }
        }
    }
    static void solveCubics(float n0, float n1, float n2, float n3, float nt0, float nt1, float *ts) {
        solveCubic(n0 - nt0, n1 - nt0, n2 - nt0, n3 - nt0, ts[0], ts[1], ts[2]);
        solveCubic(n0 - nt1, n1 - nt1, n2 - nt1, n3 - nt1, ts[3], ts[4], ts[5]);
        for (int i = 0; i < 6; i++)
            ts[i] = ts[i] < 0 ? 0 : ts[i] > 1 ? 1 : ts[i];
    }
    static void writeClippedCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float t0, float t1, Bounds clipBounds, bool visible, float *cubic) {
        const float w0 = 8.0 / 27.0, w1 = 4.0 / 9.0, w2 = 2.0 / 9.0, w3 = 1.0 / 27.0, w1r = 1.0 / w1, wa = w2 / (w1 * w1), wb = w1 * w1 / (w1 * w1 - w2 * w2);
        float ax, ay, bx, by, cx, cy;
        float tp0, tp1, tx0, ty0, tx1, ty1, tx2, ty2, tx3, ty3, A, B;
        
        cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
        cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
        tp0 = (2.f * t0 + t1) / 3.f, tp1 = (t0 + 2.f * t1) / 3.f;
        
        tx0 = ((ax * t0 + bx) * t0 + cx) * t0 + x0;
        ty0 = ((ay * t0 + by) * t0 + cy) * t0 + y0;
        tx1 = ((ax * tp0 + bx) * tp0 + cx) * tp0 + x0;
        ty1 = ((ay * tp0 + by) * tp0 + cy) * tp0 + y0;
        tx2 = ((ax * tp1 + bx) * tp1 + cx) * tp1 + x0;
        ty2 = ((ay * tp1 + by) * tp1 + cy) * tp1 + y0;
        tx3 = ((ax * t1 + bx) * t1 + cx) * t1 + x0;
        ty3 = ((ay * t1 + by) * t1 + cy) * t1 + y0;
        A = tx0 * w0 + tx3 * w3 - tx1, B = tx0 * w3 + tx3 * w0 - tx2;
        tx1 = (B * wa - A * w1r) * wb, tx2 = (-B - tx1 * w2) * w1r;
        A = ty0 * w0 + ty3 * w3 - ty1, B = ty0 * w3 + ty3 * w0 - ty2;
        ty1 = (B * wa - A * w1r) * wb, ty2 = (-B - ty1 * w2) * w1r;
        
        tx0 = tx0 < clipBounds.lx ? clipBounds.lx : tx0 > clipBounds.ux ? clipBounds.ux : tx0;
        ty0 = ty0 < clipBounds.ly ? clipBounds.ly : ty0 > clipBounds.uy ? clipBounds.uy : ty0;
        tx3 = tx3 < clipBounds.lx ? clipBounds.lx : tx3 > clipBounds.ux ? clipBounds.ux : tx3;
        ty3 = ty3 < clipBounds.ly ? clipBounds.ly : ty3 > clipBounds.uy ? clipBounds.uy : ty3;
        if (!visible) {
            tx1 = tx1 < clipBounds.lx ? clipBounds.lx : tx1 > clipBounds.ux ? clipBounds.ux : tx1;
            ty1 = ty1 < clipBounds.ly ? clipBounds.ly : ty1 > clipBounds.uy ? clipBounds.uy : ty1;
            tx2 = tx2 < clipBounds.lx ? clipBounds.lx : tx2 > clipBounds.ux ? clipBounds.ux : tx2;
            ty2 = ty2 < clipBounds.ly ? clipBounds.ly : ty2 > clipBounds.uy ? clipBounds.uy : ty2;
        }
        *cubic++ = tx0, *cubic++ = ty0, *cubic++ = tx1, *cubic++ = ty1, *cubic++ = tx2, *cubic++ = ty2, *cubic++ = tx3, *cubic++ = ty3;
    }
    static void writeClippedCubicToScanlines(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Bounds clipBounds, Scanline *scanlines) {
        float lx, ly, ux, uy, cly, cuy, ts[12], t0, t1, t, s, x, y, cubic[8];
        size_t i;
        bool visible;
        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2, ly = ly < y3 ? ly : y3;
        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2, uy = uy > y3 ? uy : y3;
        cly = ly < clipBounds.ly ? clipBounds.ly : ly > clipBounds.uy ? clipBounds.uy : ly;
        cuy = uy < clipBounds.ly ? clipBounds.ly : uy > clipBounds.uy ? clipBounds.uy : uy;
        if (cly != cuy) {
            lx = x0 < x1 ? x0 : x1, lx = lx < x2 ? lx : x2, lx = lx < x3 ? lx : x3;
            ux = x0 > x1 ? x0 : x1, ux = ux > x2 ? ux : x2, ux = ux > x3 ? ux : x3;
            if (lx < clipBounds.lx || ux > clipBounds.ux || ly < clipBounds.ly || uy > clipBounds.uy) {
                solveCubics(y0, y1, y2, y3, clipBounds.ly, clipBounds.uy, & ts[0]);
                solveCubics(x0, x1, x2, x3, clipBounds.lx, clipBounds.ux, & ts[6]);
                std::sort(& ts[0], & ts[12]);
                for (i = 0; i < 11; i++) {
                    t0 = ts[i], t1 = ts[i + 1];
                    if (t0 != t1) {
                        t = (t0 + t1) * 0.5f, s = 1.f - t;
                        y = y0 * s * s * s + y1 * 3.f * s * s * t + y2 * 3.f * s * t * t + y3 * t * t * t;
                        if (y >= clipBounds.ly && y <= clipBounds.uy) {
                            x = x0 * s * s * s + x1 * 3.f * s * s * t + x2 * 3.f * s * t * t + x3 * t * t * t;
                            visible = x >= clipBounds.lx && x <= clipBounds.ux;
                            writeClippedCubic(x0, y0, x1, y1, x2, y2, x3, y3, t0, t1, clipBounds, visible, cubic);
                            if (visible) {
                                if (fabsf(t1 - t0) < 1e-2) {
                                    writeSegmentToDeltasOrScanlines(cubic[0], cubic[1], x, y, 32767.f, nullptr, 0, scanlines);
                                    writeSegmentToDeltasOrScanlines(x, y, cubic[6], cubic[7], 32767.f, nullptr, 0, scanlines);
                                } else
                                    writeCubicToDeltasOrScanlines(cubic[0], cubic[1], cubic[2], cubic[3], cubic[4], cubic[5], cubic[6], cubic[7], 32767.f, nullptr, 0, scanlines);
                            } else {
                                writeVerticalSegmentToScanlines(cubic[0], cubic[1], cubic[3], scanlines);
                                writeVerticalSegmentToScanlines(cubic[0], cubic[3], cubic[5], scanlines);
                                writeVerticalSegmentToScanlines(cubic[0], cubic[5], cubic[7], scanlines);
                            }
                        }
                    }
                }
            } else
                writeCubicToDeltasOrScanlines(x0, y0, x1, y1, x2, y2, x3, y3, 32767.f, nullptr, 0, scanlines);
        }
    }
    
    static void writePathToDeltasOrScanlines(Path& path, AffineTransform ctm, float *deltas, size_t stride, Scanline *scanlines) {
        float sx, sy, x0, y0, x1, y1, x2, y2, x3, y3, *p, scale;
        size_t index;
        uint8_t type;
        x0 = y0 = sx = sy = FLT_MAX;
        scale = scanlines ? 32767.f : 255.5f;
        for (Path::Atom& atom : path.atoms) {
            index = 0;
            for (type = 0xF & atom.types[0]; type != Path::Atom::kNull; type = 0xF & (atom.types[index / 2] >> ((index & 1) * 4))) {
                p = atom.points + index * 2;
                switch (type) {
                    case Path::Atom::kMove:
                        if (sx != FLT_MAX && (sx != x0 || sy != y0))
                            writeSegmentToDeltasOrScanlines(x0, y0, sx, sy, scale, deltas, stride, scanlines);
                        x0 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y0 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        sx = x0, sy = y0;
                        index++;
                        break;
                    case Path::Atom::kLine:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        writeSegmentToDeltasOrScanlines(x0, y0, x1, y1, scale, deltas, stride, scanlines);
                        x0 = x1, y0 = y1;
                        index++;
                        break;
                    case Path::Atom::kQuadratic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        writeQuadraticToDeltasOrScanlines(x0, y0, x1, y1, x2, y2, scale, deltas, stride, scanlines);
                        x0 = x2, y0 = y2;
                        index += 2;
                        break;
                    case Path::Atom::kCubic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        x3 = p[4] * ctm.a + p[5] * ctm.c + ctm.tx, y3 = p[4] * ctm.b + p[5] * ctm.d + ctm.ty;
                        writeCubicToDeltasOrScanlines(x0, y0, x1, y1, x2, y2, x3, y3, scale, deltas, stride, scanlines);
                        x0 = x3, y0 = y3;
                        index += 3;
                        break;
                    case Path::Atom::kClose:
                        index++;
                        break;
                }
            }
        }
        if (sx != FLT_MAX && (sx != x0 || sy != y0))
            writeSegmentToDeltasOrScanlines(x0, y0, sx, sy, scale, deltas, stride, scanlines);
    }
    
    static void writeQuadraticToDeltasOrScanlines(float x0, float y0, float x1, float y1, float x2, float y2, float scale, float *deltas, size_t stride, Scanline *scanlines) {
        float px0, py0, px1, py1, a, dt, s, t, ax, ay;
        size_t count;
        ax = x0 + x2 - x1 - x1, ay = y0 + y2 - y1 - y1;
        a = ax * ax + ay * ay;
        if (a < 0.1)
            writeSegmentToDeltasOrScanlines(x0, y0, x2, y2, scale, deltas, stride, scanlines);
        else if (a < 8) {
            px0 = (x0 + x2) * 0.25 + x1 * 0.5, py0 = (y0 + y2) * 0.25 + y1 * 0.5;
            writeSegmentToDeltasOrScanlines(x0, y0, px0, py0, scale, deltas, stride, scanlines);
            writeSegmentToDeltasOrScanlines(px0, py0, x2, y2, scale, deltas, stride, scanlines);
        } else {
            count = 1.f + floorf(sqrtf(sqrtf(3.f * a))), dt = 1.f / count, t = 0;
            px0 = x0, py0 = y0;
            while (--count) {
                t += dt, s = 1.f - t;
                px1 = x0 * s * s + x1 * 2.f * s * t + x2 * t * t, py1 = y0 * s * s + y1 * 2.f * s * t + y2 * t * t;
                writeSegmentToDeltasOrScanlines(px0, py0, px1, py1, scale, deltas, stride, scanlines);
                px0 = px1, py0 = py1;
            }
            writeSegmentToDeltasOrScanlines(px0, py0, x2, y2, scale, deltas, stride, scanlines);
        }
    }
    
    static void writeCubicToDeltasOrScanlines(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float scale, float *deltas, size_t stride, Scanline *scanlines) {
        const float w0 = 8.0 / 27.0, w1 = 4.0 / 9.0, w2 = 2.0 / 9.0, w3 = 1.0 / 27.0;
        float px0, py0, px1, py1, a, dt, s, t, ax, ay, bx, by, cx, cy, pw0, pw1, pw2, pw3;
        size_t count;
        cx = 3.0 * (x1 - x0), bx = 3.0 * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
        cy = 3.0 * (y1 - y0), by = 3.0 * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
        a = (ax + bx) * (ax + bx) + (ay + by) * (ay + by);
        if (a < 0.1)
            writeSegmentToDeltasOrScanlines(x0, y0, x3, y3, scale, deltas, stride, scanlines);
        else if (a < 16) {
            px0 = x0 * w0 + x1 * w1 + x2 * w2 + x3 * w3, py0 = y0 * w0 + y1 * w1 + y2 * w2 + y3 * w3;
            px1 = x0 * w3 + x1 * w2 + x2 * w1 + x3 * w0, py1 = y0 * w3 + y1 * w2 + y2 * w1 + y3 * w0;
            writeSegmentToDeltasOrScanlines(x0, y0, px0, py0, scale, deltas, stride, scanlines);
            writeSegmentToDeltasOrScanlines(px0, py0, px1, py1, scale, deltas, stride, scanlines);
            writeSegmentToDeltasOrScanlines(px1, py1, x3, y3, scale, deltas, stride, scanlines);
        } else {
            count = 1.f + floorf(sqrtf(sqrtf(3.f * a))), dt = 1.f / count, t = 0;
            dt = 1.f / count, t = 0, s = 1.f;
            px0 = x0, py0 = y0;
            while (--count) {
                t += dt, s = 1.f - t;
                pw0 = s * s * s, pw1 = 3.f * s * s * t, pw2 = 3.f * s * t * t, pw3 = t * t * t;
                px1 = x0 * pw0 + x1 * pw1 + x2 * pw2 + x3 * pw3, py1 = y0 * pw0 + y1 * pw1 + y2 * pw2 + y3 * pw3;
                writeSegmentToDeltasOrScanlines(px0, py0, px1, py1, scale, deltas, stride, scanlines);
                px0 = px1, py0 = py1;
            }
            writeSegmentToDeltasOrScanlines(px0, py0, x3, y3, scale, deltas, stride, scanlines);
        }
    }
    
    static void writeVerticalSegmentToScanlines(float x, float y0, float y1, Scanline *scanlines) {
        if (y0 == y1)
            return;
        float ly, uy, iy0, iy1, sy0, sy1, ix0, ix1;
        Scanline *scanline;
        ly = y0 < y1 ? y0 : y1;
        uy = y0 > y1 ? y0 : y1;
        ix0 = floorf(x), ix1 = ix0 + 1.f;
        for (iy0 = floorf(ly), iy1 = iy0 + 1, scanline = scanlines + size_t(iy0); iy0 < uy; iy0 = iy1, iy1++, scanline++) {
            sy0 = y0 < iy0 ? iy0 : y0 > iy1 ? iy1 : y0;
            sy1 = y1 < iy0 ? iy0 : y1 > iy1 ? iy1 : y1;
            if (x == 0)
                scanline->delta0 += 255.5f * (sy1 - sy0);
            else
                scanline->insertDelta(ix0, (sy1 - sy0) * 32767.f * (ix1 - x));
        }
    }
    
    static void writeSegmentToDeltasOrScanlines(float x0, float y0, float x1, float y1, float scale, float *deltas, size_t stride, Scanline *scanlines) {
        if (y0 == y1)
            return;
        float tmp, dxdy, iy0, iy1, *deltasRow, sx0, sy0, sx1, sy1, lx, ux, ix0, ix1, dydx, cx0, cy0, cx1, cy1, cover, area, total, alpha, last, *delta;
        Scanline *scanline;
        size_t ily;
        scale = copysign(scale, y1 - y0);
        if (scale < 0)
            tmp = x0, x0 = x1, x1 = tmp, tmp = y0, y0 = y1, y1 = tmp;
        dxdy = (x1 - x0) / (y1 - y0);
        
        for (ily = iy0 = floorf(y0), iy1 = iy0 + 1, sy0 = y0, sx0 = x0, deltasRow = deltas + stride * ily, scanline = scanlines + ily;
             iy0 < y1;
             iy0 = iy1, iy1++, sy0 = sy1, sx0 = sx1, deltasRow += stride, scanline++) {
            sy1 = y1 > iy1 ? iy1 : y1;
            sx1 = (sy1 - y0) * dxdy + x0;
            
            lx = sx0 < sx1 ? sx0 : sx1;
            ux = sx0 > sx1 ? sx0 : sx1;
            ix0 = floorf(lx), ix1 = ix0 + 1;
            if (lx >= ix0 && ux <= ix1) {
                cover = (sy1 - sy0) * scale;
                area = (ix1 - (ux + lx) * 0.5f);
                alpha = cover * area;
                total = cover;
                if (scanlines) {
                    scanline->insertDelta(ix0, alpha);
                    scanline->insertDelta(ix1, total - alpha);
                } else {
                    delta = deltasRow + size_t(ix0);
                    *delta++ += alpha;
                    if (ix1 < stride)
                        *delta += total - alpha;
                }
            } else {
                dydx = 1.f / fabsf(dxdy);
                cx0 = lx, cy0 = sy0;
                cx1 = ux < ix1 ? ux : ix1;
                cy1 = ux == lx ? sy1 : (cx1 - lx) * dydx + sy0;
                for (total = last = 0, delta = deltasRow + size_t(ix0);
                     ix0 <= ux;
                     ix0 = ix1, ix1++, cx0 = cx1, cx1 = ux < ix1 ? ux : ix1, cy0 = cy1, cy1 += dydx, cy1 = cy1 < sy1 ? cy1 : sy1, delta++) {
                    
                    cover = (cy1 - cy0) * scale;
                    area = (ix1 - (cx0 + cx1) * 0.5f);
                    alpha = total + cover * area;
                    total += cover;
                    if (scanlines)
                        scanline->insertDelta(ix0, alpha - last);
                    else
                        *delta += alpha - last;
                    last = alpha;
                }
                if (scanlines)
                    scanline->insertDelta(ix0, total - last);
                else {
                    if (ix0 < stride)
                        *delta += total - last;
                }
            }
        }
    }
};
