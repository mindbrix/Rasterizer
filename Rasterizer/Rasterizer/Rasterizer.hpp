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
    static const size_t kCellsDimension = 256;
    
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
            Delta(float x, float y, float delta) : x(x), y(y), delta(delta) {}
            
            inline bool operator< (const Delta& other) const { return x < other.x || (x == other.x && y < other.y); }
            short x, y;
            float delta;
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
        float deltas[kCellsDimension * kCellsDimension];
        uint8_t mask[kCellsDimension * kCellsDimension];
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
        float x, y, ix, cover, alpha;
        uint8_t a;
        Scanline *scanline = & scanlines[clipped.ly - device.ly];
        Scanline::Delta *begin, *end, *delta;
        for (y = clipped.ly; y < clipped.uy; y++, scanline++) {
            begin = & scanline->deltas[0], end = & scanline->deltas[scanline->idx];
            std::sort(begin, end);
            
            if (scanline->idx == 0) {
                if (scanline->delta0)
                    spans.emplace_back(clipped.lx, y, clipped.ux - clipped.lx);
            } else {
                cover = scanline->delta0;
                x = scanline->deltas[0].x;
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
                    cover += delta->delta;
                }
            }
        }
        scanline = & scanlines[0];
        for (y = device.ly; y < device.uy; y++, scanline++)
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
        if (clipped.lx != clipped.ux && clipped.ly != clipped.uy) {
            AffineTransform deltasCTM = { ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx - device.lx, ctm.ty - device.ly };
            float e = 2e-3 / sqrtf(fabsf(ctm.a * ctm.d - ctm.b * ctm.c));
            float w = bounds.ux - bounds.lx, h = bounds.uy - bounds.ly, mx = (bounds.lx + bounds.ux) * 0.5, my = (bounds.ly + bounds.uy) * 0.5;
            AffineTransform offset(1, 0, 0, 1, mx, my);
            offset = offset.concat(AffineTransform(w / (w + e), 0, 0, h / (h + e), 0, 0));
            offset = offset.concat(AffineTransform(1, 0, 0, 1, -mx, -my));
            
            if ((device.ux - device.lx) * (device.uy - device.ly) < kCellsDimension * kCellsDimension) {
                writePathToDeltasOrScanlines(path, deltasCTM.concat(offset), context.deltas, device.ux - device.lx, nullptr);
                writeDeltasToMask(context.deltas, device, context.mask);
                writeMaskToBitmap(context.mask, device, clipped, bgra, context.bitmap);
            } else if ((device.uy - device.ly) < context.bitmap.height) {
                writePathToDeltasOrScanlines(path, deltasCTM.concat(offset), nullptr, 0, & context.scanlines[0]);
                writeScanlinesToSpans(context.scanlines, device, clipped, context.spans);
                writeSpansToBitmap(context.spans, bgra, context.bitmap);
            }
        }
    }
    
    static void writePathToDeltasOrScanlines(Path& path, AffineTransform ctm, float *deltas, size_t stride, Scanline *scanlines) {
        const float w0 = 8.0 / 27.0, w1 = 4.0 / 9.0, w2 = 2.0 / 9.0, w3 = 1.0 / 27.0;
        float sx, sy, x0, y0, x1, y1, x2, y2, x3, y3, px0, py0, px1, py1, a, *p, dt, s, t, ax, ay;
        size_t index, count;
        x0 = y0 = sx = sy = FLT_MAX;
        for (Path::Atom& atom : path.atoms) {
            index = 0;
            auto type = 0xF & atom.types[0];
            while (type) {
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
                        x0 = x2, y0 = y2;
                        index += 2;
                        break;
                    case Path::Atom::kCubic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        x3 = p[4] * ctm.a + p[5] * ctm.c + ctm.tx, y3 = p[4] * ctm.b + p[5] * ctm.d + ctm.ty;
                        ax = x0 + x2 - x1 - x1, ay = y0 + y2 - y1 - y1;
                        a = ax * ax + ay * ay;
                        ax = x1 + x3 - x2 - x2, ay = y1 + y3 - y2 - y2;
                        a += ax * ax + ay * ay;
                        if (a < 0.1)
                            writeSegmentToDeltasOrScanlines(x0, y0, x3, y3, deltas, stride, scanlines);
                        else if (a < 16) {
                            px0 = x0 * w0 + x1 * w1 + x2 * w2 + x3 * w3, py0 = y0 * w0 + y1 * w1 + y2 * w2 + y3 * w3;
                            px1 = x0 * w3 + x1 * w2 + x2 * w1 + x3 * w0, py1 = y0 * w3 + y1 * w2 + y2 * w1 + y3 * w0;
                            writeSegmentToDeltasOrScanlines(x0, y0, px0, py0, deltas, stride, scanlines);
                            writeSegmentToDeltasOrScanlines(px0, py0, px1, py1, deltas, stride, scanlines);
                            writeSegmentToDeltasOrScanlines(px1, py1, x3, y3, deltas, stride, scanlines);
                        } else {
                            count = log2f(a), dt = 1.f / count, t = 0, s = 1.f;
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
                        x0 = x3, y0 = y3;
                        index += 3;
                        break;
                    case Path::Atom::kClose:
                        index++;
                        break;
                }
                type = 0xF & (atom.types[index / 2] >> ((index & 1) * 4));
            }
        }
        if (sx != FLT_MAX && (sx != x0 || sy != y0))
            writeSegmentToDeltasOrScanlines(x0, y0, sx, sy, deltas, stride, scanlines);
    }
    
    static void writeSegmentToDeltasOrScanlines(float x0, float y0, float x1, float y1, float *deltas, size_t stride, Scanline *scanlines) {
        if (y0 == y1)
            return;
        float dxdy, dydx, iy0, iy1, *deltasRow, sx0, sy0, sx1, sy1, lx, ux, ix0, ix1, cx0, cy0, cx1, cy1, cover, area, total, alpha, last, tmp, sign, *delta;
        Scanline *scanline;
        sign = 255.5f * (y0 < y1 ? 1 : -1);
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
