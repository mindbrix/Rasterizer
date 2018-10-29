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
    template<typename T>
    static void radixSort(T *in, short *counts, T *out, int n, int shift) {
        T x;
        memset(counts, 0, sizeof(short) * 256);
        for (int i = 0; i < n; i++)
            counts[(in[i] >> shift) & 0xFF]++;
        for (int i = 1; i < 256; i++)
            counts[i] += counts[i - 1];
        for (int i = n - 1; i >= 0; i--) {
            x = in[i];
            out[counts[(x >> shift) & 0xFF] - 1] = in[i];
            counts[(x >> shift) & 0xFF]--;
        }
    }
    static const size_t kDeltasDimension = 64;
    
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
            Delta(float x, float y, float delta) : x(x), delta(delta * 32767.f) {}
            inline bool operator< (const Delta& other) const { return x < other.x; }
            short x, delta;
        };
        Scanline() { empty(); }
        
        void empty() { delta0 = idx = 0; }
        inline Delta *alloc() {
            if (idx >= deltas.size())
                deltas.resize(deltas.size() == 0 ? 8 : deltas.size() * 1.5);
            return & deltas[idx++];
        }
        float delta0;
        size_t idx;
        std::vector<Delta> deltas;
    };
    struct Span {
        Span() {}
        Span(float x, float y, float w) : x(x), y(y), w(w) {}
        short x, y, w;
    };
    struct Context {
        Context() { memset(deltas, 0, sizeof(deltas)); }
        
        void setBitmap(Bitmap bitmap) {
            this->bitmap = bitmap;
            if (scanlines.size() != bitmap.height)
                scanlines.resize(bitmap.height);
        }
        Bitmap bitmap;
        float deltas[kDeltasDimension * kDeltasDimension];
        uint8_t mask[kDeltasDimension * kDeltasDimension];
        std::vector<Scanline> scanlines;
        std::vector<Span> spans;
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
    
    static void writeScanlinesToSpans(std::vector<Scanline>& scanlines, Bounds device, Bounds clipped, std::vector<Span>& spans) {
        const float scale = 255.5f / 32767.f;
        float x, y, ix, cover, alpha;
        uint8_t a;
        Scanline *scanline = & scanlines[clipped.ly - device.ly];
        Scanline::Delta *begin, *end, *delta;
        for (y = clipped.ly; y < clipped.uy; y++, scanline++) {
            begin = & scanline->deltas[0], end = & scanline->deltas[scanline->idx];

            int n = int(end - begin);
            if (n > 64) {
                short counts[256];
                uint32_t mem0[n];
                radixSort((uint32_t *)begin, counts, mem0, n, 0);
                radixSort(mem0, counts, (uint32_t *)begin, n, 8);
            } else
                std::sort(begin, end);
            
            if (scanline->idx == 0) {
                if (scanline->delta0)
                    spans.emplace_back(clipped.lx, y, clipped.ux - clipped.lx);
            } else {
                cover = scanline->delta0;
                alpha = fabsf(cover);
                a = alpha < 255.f ? alpha : 255.f;
                
                x = a ? clipped.lx : scanline->deltas[0].x;
                for (delta = begin; delta < end; delta++) {
                    if (delta->x != x) {
                        alpha = fabsf(cover);
                        a = alpha < 255.f ? alpha : 255.f;
                        
                        if (a > 254)
                            spans.emplace_back(device.lx + x, y, delta->x - x);
                        else if (a > 0)
                            for (ix = x; ix < delta->x; ix++)
                                spans.emplace_back(device.lx + ix, y, -a);
                        x = delta->x;
                    }
                    cover += float(delta->delta) * scale;
                }
            }
        }
        if (device.isZero())
            for (scanline = & scanlines[clipped.ly - device.ly], y = clipped.ly; y < clipped.uy; y++, scanline++)
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
    
    static void writeSpansToBitmap(std::vector<Span>& spans, uint32_t color, Bitmap bitmap) {
        Bounds clipBounds(0, 0, bitmap.width, bitmap.height);
        float lx, ux, src0, src1, src2, src3, alpha, dst0, dst1, dst2, dst3;
        uint8_t *components = (uint8_t *) & color;
        src0 = components[0], src1 = components[1], src2 = components[2], src3 = components[3];
        uint32_t *pixelAddress;
        for (Span& span : spans) {
            if (span.y >= clipBounds.ly && span.y < clipBounds.uy) {
                lx = span.x, ux = lx + (span.w > 0 ? span.w : 1);
                lx = lx < clipBounds.lx ? clipBounds.lx : lx > clipBounds.ux ? clipBounds.ux : lx;
                ux = ux < clipBounds.lx ? clipBounds.lx : ux > clipBounds.ux ? clipBounds.ux : ux;
                
                if (lx != ux) {
                    pixelAddress = bitmap.pixelAddress(lx, span.y);
                    if (span.w > 0)
                        memset_pattern4(pixelAddress, & color, (ux - lx) * bitmap.bytespp);
                    else {
                        alpha = float(-span.w) * 0.003921568627f;
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
        spans.resize(0);
    }
    
    static void writePathToBitmap(Path& path, Bounds bounds, AffineTransform ctm, uint32_t bgra, Context& context) {
        Bounds clipBounds(0, 0, context.bitmap.width, context.bitmap.height);
        Bounds device = bounds.transform(ctm).integral();
        Bounds clipped = device.intersected(clipBounds);
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
                writeScanlinesToSpans(context.scanlines, device, clipped, context.spans);
                writeSpansToBitmap(context.spans, bgra, context.bitmap);
            } else {
                writeClippedPathToScanlines(path, ctm, clipBounds, & context.scanlines[0]);
                writeScanlinesToSpans(context.scanlines, Bounds(0, 0, 0, 0), clipped, context.spans);
                writeSpansToBitmap(context.spans, bgra, context.bitmap);
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
                writeSegmentToDeltasOrScanlines(sx0, sy0, sx1, sy1, nullptr, 0, scanlines);
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
                        writeDelta0sToScanlines(sy0, syt0, scanlines);
                    if (t1 < 1)
                        writeSegmentToDeltasOrScanlines(clipBounds.ux, syt1, clipBounds.ux, sy1, nullptr, 0, scanlines);
                } else if (x0 > x1) {
                    if (t1 < 1)
                        writeDelta0sToScanlines(syt1, sy1, scanlines);
                    if (t0 > 0)
                        writeSegmentToDeltasOrScanlines(clipBounds.ux, sy0, clipBounds.ux, syt0, nullptr, 0, scanlines);
                }
                writeSegmentToDeltasOrScanlines(sx0 + t0 * (sx1 - sx0), syt0, sx0 + t1 * (sx1 - sx0), syt1, nullptr, 0, scanlines);
            }
        }
    }
    static void solveQuadratic(float A, float B, float C, float& t0, float& t1) {
        if (fabsf(A) < 1e-3) {
            t0 = -C / B, t1 = FLT_MAX;
        } else {
            float discriminant, sqrtDiscriminant, denominator;
            discriminant = B * B - 4.0f * A * C;
            if (discriminant < 0)
                t0 = t1 = FLT_MAX;
            else {
                sqrtDiscriminant = sqrtf(fabsf(discriminant));
                denominator = 1.0f / (2.0f * A);
                t0 = (-B + sqrtDiscriminant) * denominator, t1 = (-B - sqrtDiscriminant) * denominator;
            }
        }
    }
    static void solveQuadratic(float n0, float n1, float n2, float nt0, float nt1, float *ts) {
        float A = n0 + n2 - n1 - n1, B = 2.f * (n1 - n0);
        solveQuadratic(A, B, n0 - nt0, ts[0], ts[1]);
        solveQuadratic(A, B, n0 - nt1, ts[2], ts[3]);
        for (int i = 0; i < 4; i++)
            ts[i] = ts[i] < 0 ? 0 : ts[i] > 1 ? 1 : ts[i];
        std::sort(& ts[0], & ts[4]);
        if (ts[0] == ts[1])
            std::swap(ts[0], ts[2]), std::swap(ts[1], ts[3]);
    }
    static void writeClippedQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, float t0, float t1, Bounds clipBounds, bool clip, float *q) {
        assert(t0 < t1);
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
        float lx, ly, ux, uy, cly, cuy, tys[4], txs[4], ty0, ty1, ty2, q[6], tx0, tx1;
        size_t x, y;
        lx = x0 < x1 ? x0 : x1, ly = y0 < y1 ? y0 : y1;
        lx = lx < x2 ? lx : x2, ly = ly < y2 ? ly : y2;
        ux = x0 > x1 ? x0 : x1, uy = y0 > y1 ? y0 : y1;
        ux = ux > x2 ? ux : x2, uy = uy > y2 ? uy : y2;
        cly = ly < clipBounds.ly ? clipBounds.ly : ly > clipBounds.uy ? clipBounds.uy : ly;
        cuy = uy < clipBounds.ly ? clipBounds.ly : uy > clipBounds.uy ? clipBounds.uy : uy;
        if (cly != cuy) {
            if (lx < clipBounds.lx || ux > clipBounds.ux || ly < clipBounds.ly || uy > clipBounds.uy) {
                solveQuadratic(y0, y1, y2, clipBounds.ly, clipBounds.uy, tys);
                solveQuadratic(x0, x1, x2, clipBounds.lx, clipBounds.ux, txs);
                if (txs[0] == txs[1]) {
                    ty0 = y0 < cly ? cly : y0 > cuy ? cuy : y0;
                    ty1 = y1 < cly ? cly : y1 > cuy ? cuy : y1;
                    ty2 = y2 < cly ? cly : y2 > cuy ? cuy : y2;
                    if (lx > clipBounds.ux) {
                        writeSegmentToDeltasOrScanlines(clipBounds.ux, ty0, clipBounds.ux, ty1, nullptr, 0, scanlines);
                        writeSegmentToDeltasOrScanlines(clipBounds.ux, ty1, clipBounds.ux, ty2, nullptr, 0, scanlines);
                    } else {
                        writeDelta0sToScanlines(ty0, ty1, scanlines);
                        writeDelta0sToScanlines(ty1, ty2, scanlines);
                    }
                } else {
                    for (y = 0; y < 4; y += 2) {
                        ty0 = tys[y], ty1 = tys[y + 1];
                        if (ty0 != ty1) {
                            tx0 = txs[0], tx0 = tx0 < ty0 ? ty0 : tx0 > ty1 ? ty1 : tx0;
                            tx1 = txs[1], tx1 = tx1 < ty0 ? ty0 : tx1 > ty1 ? ty1 : tx1;
                            if (ty0 < tx0) {
                                writeClippedQuadratic(x0, y0, x1, y1, x2, y2, ty0, tx0, clipBounds, true, q);
                                writeSegmentToDeltasOrScanlines(q[0], q[1], q[0], q[3], nullptr, 0, scanlines);
                                writeSegmentToDeltasOrScanlines(q[0], q[3], q[0], q[5], nullptr, 0, scanlines);
                            }
                            for (x = 2; x < 4 && tx0 != tx1; tx0 = tx1, tx1 = txs[x++], tx1 = tx1 < ty0 ? ty0 : tx1 > ty1 ? ty1 : tx1) {
                                if ((x & 1) == 0) {
                                    writeClippedQuadratic(x0, y0, x1, y1, x2, y2, tx0, tx1, clipBounds, false, q);
                                    writeQuadraticToDeltasOrScanlines(q[0], q[1], q[2], q[3], q[4], q[5], nullptr, 0, scanlines);
                                } else if (tx0 < tx1) {
                                    writeClippedQuadratic(x0, y0, x1, y1, x2, y2, tx0, tx1, clipBounds, true, q);
                                    writeQuadraticToDeltasOrScanlines(q[0], q[1], q[0], q[3], q[0], q[5], nullptr, 0, scanlines);
                                }
                            }
                            if (tx1 && ty1 > tx1) {
                                writeClippedQuadratic(x0, y0, x1, y1, x2, y2, tx1, ty1, clipBounds, true, q);
                                writeSegmentToDeltasOrScanlines(q[0], q[1], q[0], q[3], nullptr, 0, scanlines);
                                writeSegmentToDeltasOrScanlines(q[0], q[3], q[0], q[5], nullptr, 0, scanlines);
                            }
                        }
                    }
                }
            } else
                writeQuadraticToDeltasOrScanlines(x0, y0, x1, y1, x2, y2, nullptr, 0, scanlines);
        }
    }
    static void writeClippedCubicToScanlines(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Bounds clipBounds, Scanline *scanlines) {
        writeClippedSegmentToScanlines(x0, y0, x1, y1, clipBounds, scanlines);
        writeClippedSegmentToScanlines(x1, y1, x2, y2, clipBounds, scanlines);
        writeClippedSegmentToScanlines(x2, y2, x3, y3, clipBounds, scanlines);
    }
    
    static void writePathToDeltasOrScanlines(Path& path, AffineTransform ctm, float *deltas, size_t stride, Scanline *scanlines) {
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
                            writeSegmentToDeltasOrScanlines(x0, y0, sx, sy, deltas, stride, scanlines);
                        x0 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y0 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        sx = x0, sy = y0;
                        index++;
                        break;
                    case Path::Atom::kLine:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        writeSegmentToDeltasOrScanlines(x0, y0, x1, y1, deltas, stride, scanlines);
                        x0 = x1, y0 = y1;
                        index++;
                        break;
                    case Path::Atom::kQuadratic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        writeQuadraticToDeltasOrScanlines(x0, y0, x1, y1, x2, y2, deltas, stride, scanlines);
                        x0 = x2, y0 = y2;
                        index += 2;
                        break;
                    case Path::Atom::kCubic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        x3 = p[4] * ctm.a + p[5] * ctm.c + ctm.tx, y3 = p[4] * ctm.b + p[5] * ctm.d + ctm.ty;
                        writeCubicToDeltasOrScanlines(x0, y0, x1, y1, x2, y2, x3, y3, deltas, stride, scanlines);
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
            writeSegmentToDeltasOrScanlines(x0, y0, sx, sy, deltas, stride, scanlines);
    }
    
    static void writeQuadraticToDeltasOrScanlines(float x0, float y0, float x1, float y1, float x2, float y2, float *deltas, size_t stride, Scanline *scanlines) {
        float px0, py0, px1, py1, a, dt, s, t, ax, ay;
        size_t count;
        ax = x0 + x2 - x1 - x1, ay = y0 + y2 - y1 - y1;
        a = ax * ax + ay * ay;
        if (a < 0.1)
            writeSegmentToDeltasOrScanlines(x0, y0, x2, y2, deltas, stride, scanlines);
        else if (a < 8) {
            px0 = (x0 + x2) * 0.25 + x1 * 0.5, py0 = (y0 + y2) * 0.25 + y1 * 0.5;
            writeSegmentToDeltasOrScanlines(x0, y0, px0, py0, deltas, stride, scanlines);
            writeSegmentToDeltasOrScanlines(px0, py0, x2, y2, deltas, stride, scanlines);
        } else {
            count = log2f(a), dt = 1.f / count, t = 0, s = 1.f;
            px0 = x0, py0 = y0;
            while (--count) {
                t += dt, s = 1.f - t;
                px1 = x0 * s * s + x1 * 2.f * s * t + x2 * t * t, py1 = y0 * s * s + y1 * 2.f * s * t + y2 * t * t;
                writeSegmentToDeltasOrScanlines(px0, py0, px1, py1, deltas, stride, scanlines);
                px0 = px1, py0 = py1;
            }
            writeSegmentToDeltasOrScanlines(px0, py0, x2, y2, deltas, stride, scanlines);
        }
    }
    
    static void writeCubicToDeltasOrScanlines(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float *deltas, size_t stride, Scanline *scanlines) {
        const float w0 = 8.0 / 27.0, w1 = 4.0 / 9.0, w2 = 2.0 / 9.0, w3 = 1.0 / 27.0;
        float px0, py0, px1, py1, a, dt, s, t, ax, ay, bx, by, cx, cy;
        size_t count;
        cx = 3.0 * (x1 - x0), bx = 3.0 * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
        cy = 3.0 * (y1 - y0), by = 3.0 * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
        a = (ax + bx) * (ax + bx) + (ay + by) * (ay + by);
        if (a < 0.1)
            writeSegmentToDeltasOrScanlines(x0, y0, x3, y3, deltas, stride, scanlines);
        else if (a < 16) {
            px0 = x0 * w0 + x1 * w1 + x2 * w2 + x3 * w3, py0 = y0 * w0 + y1 * w1 + y2 * w2 + y3 * w3;
            px1 = x0 * w3 + x1 * w2 + x2 * w1 + x3 * w0, py1 = y0 * w3 + y1 * w2 + y2 * w1 + y3 * w0;
            writeSegmentToDeltasOrScanlines(x0, y0, px0, py0, deltas, stride, scanlines);
            writeSegmentToDeltasOrScanlines(px0, py0, px1, py1, deltas, stride, scanlines);
            writeSegmentToDeltasOrScanlines(px1, py1, x3, y3, deltas, stride, scanlines);
        } else {
            count = sqrtf(a) * 0.25f + 2.f;
            dt = 1.f / count, t = 0, s = 1.f;
            px0 = x0, py0 = y0;
            while (--count) {
                t += dt, s = 1.f - t;
                px1 = x0 * s * s * s + x1 * 3.f * s * s * t + x2 * 3.f * s * t * t + x3 * t * t * t;
                py1 = y0 * s * s * s + y1 * 3.f * s * s * t + y2 * 3.f * s * t * t + y3 * t * t * t;
                writeSegmentToDeltasOrScanlines(px0, py0, px1, py1, deltas, stride, scanlines);
                px0 = px1, py0 = py1;
            }
            writeSegmentToDeltasOrScanlines(px0, py0, x3, y3, deltas, stride, scanlines);
        }
    }
    
    static void writeDelta0sToScanlines(float y0, float y1, Scanline *scanlines) {
        if (y0 == y1)
            return;
        float ly, uy, iy0, iy1, sy0, sy1;
        Scanline *scanline;
        ly = y0 < y1 ? y0 : y1;
        uy = y0 > y1 ? y0 : y1;
        for (iy0 = floorf(ly), iy1 = iy0 + 1, scanline = scanlines + size_t(iy0); iy0 < uy; iy0 = iy1, iy1++, scanline++) {
            sy0 = y0 < iy0 ? iy0 : y0 > iy1 ? iy1 : y0;
            sy1 = y1 < iy0 ? iy0 : y1 > iy1 ? iy1 : y1;
            scanline->delta0 += 255.5f * (sy1 - sy0);
        }
    }
    static void writeSegmentToDeltasOrScanlines(float x0, float y0, float x1, float y1, float *deltas, size_t stride, Scanline *scanlines) {
        if (y0 == y1)
            return;
        float dxdy, dydx, iy0, iy1, *deltasRow, sx0, sy0, sx1, sy1, lx, ux, ix0, ix1, cx0, cy0, cx1, cy1, cover, area, total, alpha, last, tmp, sign, *delta;
        Scanline *scanline;
        sign = (scanlines ? 1.f : 255.5f) * (y0 < y1 ? 1 : -1);
        if (sign < 0)
            tmp = x0, x0 = x1, x1 = tmp, tmp = y0, y0 = y1, y1 = tmp;
        dxdy = (x1 - x0) / (y1 - y0);
        
        for (iy0 = floorf(y0), iy1 = iy0 + 1, sy0 = y0, sx0 = x0, deltasRow = deltas + stride * size_t(iy0), scanline = scanlines + size_t(iy0);
             iy0 < y1;
             iy0 = iy1, iy1++, sy0 = sy1, sx0 = sx1, deltasRow += stride, scanline++) {
            sy1 = y1 > iy1 ? iy1 : y1;
            sx1 = (sy1 - y0) * dxdy + x0;
            sx1 = sx1 < 0 ? 0 : sx1;
            
            lx = sx0, ux = sx1;
            if (lx > ux)
                tmp = lx, lx = ux, ux = tmp;
            
            for (ix0 = floorf(lx), ix1 = ix0 + 1, cx0 = lx, cy0 = sy0, total = last = 0, delta = deltasRow + size_t(ix0), dydx = (sy1 - sy0) / (ux - lx);
                 ix0 <= ux;
                 ix0 = ix1, ix1++, cx0 = cx1, cy0 = cy1, delta++) {
                cx1 = ux > ix1 ? ix1 : ux;
                cy1 = ux == lx ? sy1 : (cx1 - lx) * dydx + sy0;
                
                cover = (cy1 - cy0) * sign;
                area = (ix1 - (cx0 + cx1) * 0.5f);
                alpha = total + cover * area;
                total += cover;
                if (scanlines)
                    new (scanline->alloc()) Scanline::Delta(ix0, iy0, alpha - last);
                else
                    *delta += alpha - last;
                last = alpha;
            }
            if (scanlines)
                new (scanline->alloc()) Scanline::Delta(ix0, iy0, total - last);
            else {
                if (ix0 < stride)
                    *delta += total - last;
            }
        }
    }
};
