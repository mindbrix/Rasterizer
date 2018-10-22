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
    struct Bitmap {
        Bitmap(void *data, size_t width, size_t height, size_t rowBytes, size_t bpp) : data((uint8_t *)data), width(width), height(height), rowBytes(rowBytes), bpp(bpp), bytespp(bpp / 8) {}
        
        inline uint8_t *pixelAddress(short x, short y) { return data + rowBytes * (height - 1 - y) + x * bytespp; }
        
        uint8_t *data;
        size_t width, height, rowBytes, bpp, bytespp;
    };
    struct Bounds {
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
    struct Context {
        Context() { memset(deltas, 0, sizeof(deltas)); }
        
        float deltas[kCellsDimension * kCellsDimension];
        uint8_t mask[kCellsDimension * kCellsDimension];
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
            if (index + size > Atom::kCapacity) {
                index = 0;
                atoms.emplace_back();
            }
            atoms.back().types[index / 2] |= (uint8_t(type) << (index & 1 ? 4 : 0));
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
    struct Span {
        Span(float x, float y, float w) : x(x), y(y), w(w) {}
        short x, y, w;
    };
    
    static void writeMaskRowSSE(float *deltas, size_t w, uint8_t *mask) {
        float cover = 0;
        
        __m128 offset = _mm_setzero_ps();
        __m128 sign_mask = _mm_set1_ps(-0.);
        __m128i msk = _mm_set1_epi32(0x0c080400);
        while (w >> 2) {
            __m128 x = _mm_loadu_ps(deltas);
            x = _mm_add_ps(x, _mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(x), 4)));
            x = _mm_add_ps(x, _mm_shuffle_ps(_mm_setzero_ps(), x, 0x40));
            x = _mm_add_ps(x, offset);
            
            __m128 y = _mm_andnot_ps(sign_mask, x);
            y = _mm_min_ps(y, _mm_set1_ps(255.0));
            
            __m128i z = _mm_cvttps_epi32(y);
            z = _mm_shuffle_epi8(z, msk);
            
            _mm_store_ss((float *)mask, _mm_castsi128_ps(z));
            offset = _mm_shuffle_ps(x, x, 0xFF);
            
            w -= 4, mask += 4;
            *deltas++ = 0, *deltas++ = 0, *deltas++ = 0, *deltas++ = 0;
        }
        _mm_store_ss(& cover, offset);
        while (w--) {
            cover += *deltas, *deltas++ = 0;
            *mask++ = MIN(255, fabsf(cover));
        }
    }
    
    static void writeCellsMask(float *deltas, Bounds device, uint8_t *mask) {
        size_t w = device.ux - device.lx, h = device.uy - device.ly;
        for (size_t y = 0; y < h; y++, deltas += w, mask += w)
            writeMaskRowSSE(deltas, w, mask);
    }
    
    static void copyMaskSSE(uint32_t bgra, uint32_t *addr, size_t rowBytes, uint8_t *mask, size_t maskRowBytes, size_t w, size_t h) {
        uint32_t *dst;
        uint8_t *src, *components;
        components = (uint8_t *)& bgra;
        __m128 bgra4 = _mm_div_ps(_mm_set_ps(float(components[3]), float(components[2]), float(components[1]), float(components[0])), _mm_set1_ps(255.f));
        __m128 multiplied, m0, m1, m2, m3;
        __m128i a32, a16, a16_0, a16_1, a8;
        size_t columns;
        
        while (h--) {
            dst = addr, src = mask, columns = w;
            while (columns >> 2) {
                if (src[0] || src[1] || src[2] || src[3]) {
                    m0 = _mm_mul_ps(bgra4, _mm_set1_ps(float(src[0])));
                    m1 = _mm_mul_ps(bgra4, _mm_set1_ps(float(src[1])));
                    m2 = _mm_mul_ps(bgra4, _mm_set1_ps(float(src[2])));
                    m3 = _mm_mul_ps(bgra4, _mm_set1_ps(float(src[3])));
                    
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
                    multiplied = _mm_mul_ps(bgra4, _mm_set1_ps(float(*src)));
                    a32 = _mm_cvttps_epi32(multiplied);
                    a16 = _mm_packs_epi32(a32, a32);
                    a8 = _mm_packus_epi16(a16, a16);
                    *dst = _mm_cvtsi128_si32(a8);
                }
                src++, dst++;
            }
            addr -= rowBytes / 4;
            mask += maskRowBytes;
        }
    }
    
    static void fillMask(uint8_t *mask, Bounds device, Bounds clipped, uint32_t bgra, Bitmap bitmap) {
        size_t w = device.ux - device.lx;
        uint32_t *addr = (uint32_t *)bitmap.pixelAddress(clipped.lx, clipped.ly);
        uint8_t *maskaddr = mask + size_t(w * (clipped.ly - device.ly) + (clipped.lx - device.lx));
        copyMaskSSE(bgra, addr, bitmap.rowBytes, maskaddr, w, clipped.ux - clipped.lx, clipped.uy - clipped.ly);
    }
    
    static void fillSpans(std::vector<Span>& spans, uint32_t color, Bitmap bitmap) {
        Bounds clipBounds(0, 0, bitmap.width, bitmap.height);
        float lx, ux;
        uint8_t *addr;
        for (Span& span : spans) {
            if (span.y >= clipBounds.ly && span.y < clipBounds.uy) {
                lx = span.x, ux = lx + (span.w > 0 ? span.w : 1);
                lx = lx < clipBounds.lx ? clipBounds.lx : lx > clipBounds.ux ? clipBounds.ux : lx;
                ux = ux < clipBounds.lx ? clipBounds.lx : ux > clipBounds.ux ? clipBounds.ux : ux;
                
                if (lx != ux) {
                    addr = bitmap.pixelAddress(lx, span.y);
                    if (span.w > 0)
                        memset_pattern4(addr, & color, (ux - lx) * bitmap.bytespp);
                    else
                        memset_pattern4(addr, &color, bitmap.bytespp);
                }
            }
        }
    }
    
    static void renderPath(Context& context, Bounds bounds, Path& path, uint32_t bgra, AffineTransform ctm, Bitmap bitmap) {
        std::vector<Span> spans;
        
        Bounds clipBounds(0, 0, bitmap.width, bitmap.height);
        Bounds device = bounds.transform(ctm).integral();
        Bounds clipped = device.intersected(clipBounds);
        if (clipped.lx != clipped.ux && clipped.ly != clipped.uy) {
            if ((device.ux - device.lx) * (device.uy - device.ly) < kCellsDimension * kCellsDimension) {
                AffineTransform cellCTM = { ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx - device.lx, ctm.ty - device.ly };
                float dimension = device.ux - device.lx;
                addPath(path, cellCTM, context.deltas, dimension);
                writeCellsMask(context.deltas, device, context.mask);
                fillMask(context.mask, device, clipped, bgra, bitmap);
            } else
                rasterizeBoundingBox(clipped, spans);
        }
        fillSpans(spans, bgra, bitmap);
    }
    
    static void addPath(Path& path, AffineTransform ctm, float *deltas, float dimension) {
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
                            writeSegmentDeltas(x0, y0, sx, sy, deltas, dimension);
                        
                        x0 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y0 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        sx = x0 = x0 < 0 ? 0 : x0, sy = y0 = y0 < 0 ? 0 : y0;
                        index++;
                        break;
                    case Path::Atom::kLine:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        x1 = x1 < 0 ? 0 : x1, y1 = y1 < 0 ? 0 : y1;
                        writeSegmentDeltas(x0, y0, x1, y1, deltas, dimension);
                        x0 = x1, y0 = y1;
                        index++;
                        break;
                    case Path::Atom::kQuadratic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        x1 = x1 < 0 ? 0 : x1, y1 = y1 < 0 ? 0 : y1;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        x2 = x2 < 0 ? 0 : x2, y2 = y2 < 0 ? 0 : y2;
                        ax = x0 + x2 - x1 - x1, ay = y0 + y2 - y1 - y1;
                        a = ax * ax + ay * ay;
                        if (a < 0.1)
                            writeSegmentDeltas(x0, y0, x2, y2, deltas, dimension);
                        else if (a < 8) {
                            px0 = (x0 + x2) * 0.25 + x1 * 0.5, py0 = (y0 + y2) * 0.25 + y1 * 0.5;
                            writeSegmentDeltas(x0, y0, px0, py0, deltas, dimension);
                            writeSegmentDeltas(px0, py0, x2, y2, deltas, dimension);
                        } else {
                            count = log2f(a), dt = 1.f / count, t = 0, s = 1.f;
                            px0 = x0, py0 = y0;
                            while (--count) {
                                t += dt, s = 1.f - t;
                                px1 = x0 * s * s + x1 * 2.f * s * t + x2 * t * t, py1 = y0 * s * s + y1 * 2.f * s * t + y2 * t * t;
                                writeSegmentDeltas(px0, py0, px1, py1, deltas, dimension);
                                px0 = px1, py0 = py1;
                            }
                            writeSegmentDeltas(px0, py0, x2, y2, deltas, dimension);
                        }
                        x0 = x2, y0 = y2;
                        index += 2;
                        break;
                    case Path::Atom::kCubic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        x1 = x1 < 0 ? 0 : x1, y1 = y1 < 0 ? 0 : y1;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        x2 = x2 < 0 ? 0 : x2, y2 = y2 < 0 ? 0 : y2;
                        x3 = p[4] * ctm.a + p[5] * ctm.c + ctm.tx, y3 = p[4] * ctm.b + p[5] * ctm.d + ctm.ty;
                        x3 = x3 < 0 ? 0 : x3, y3 = y3 < 0 ? 0 : y3;
                        ax = x0 + x2 - x1 - x1, ay = y0 + y2 - y1 - y1;
                        a = ax * ax + ay * ay;
                        ax = x1 + x3 - x2 - x2, ay = y1 + y3 - y2 - y2;
                        a += ax * ax + ay * ay;
                        if (a < 0.1)
                            writeSegmentDeltas(x0, y0, x3, y3, deltas, dimension);
                        else if (a < 16) {
                            px0 = x0 * w0 + x1 * w1 + x2 * w2 + x3 * w3, py0 = y0 * w0 + y1 * w1 + y2 * w2 + y3 * w3;
                            px1 = x0 * w3 + x1 * w2 + x2 * w1 + x3 * w0, py1 = y0 * w3 + y1 * w2 + y2 * w1 + y3 * w0;
                            writeSegmentDeltas(x0, y0, px0, py0, deltas, dimension);
                            writeSegmentDeltas(px0, py0, px1, py1, deltas, dimension);
                            writeSegmentDeltas(px1, py1, x3, y3, deltas, dimension);
                        } else {
                            count = log2f(a), dt = 1.f / count, t = 0, s = 1.f;
                            px0 = x0, py0 = y0;
                            while (--count) {
                                t += dt, s = 1.f - t;
                                px1 = x0 * s * s * s + x1 * 3.f * s * s * t + x2 * 3.f * s * t * t + x3 * t * t * t;
                                py1 = y0 * s * s * s + y1 * 3.f * s * s * t + y2 * 3.f * s * t * t + y3 * t * t * t;
                                writeSegmentDeltas(px0, py0, px1, py1, deltas, dimension);
                                px0 = px1, py0 = py1;
                            }
                            writeSegmentDeltas(px0, py0, x3, y3, deltas, dimension);
                        }
                        x0 = x3, y0 = y3;
                        index += 3;
                        break;
                    case Path::Atom::kClose:
                        index++;
                        break;
                }
                type = 0xF & (atom.types[index / 2] >> (index & 1 ? 4 : 0));
            }
        }
        if (sx != FLT_MAX && (sx != x0 || sy != y0))
            writeSegmentDeltas(x0, y0, sx, sy, deltas, dimension);
    }
    
    static void writeSegmentDeltas(float x0, float y0, float x1, float y1, float *deltas, float dimension) {
        if (y0 == y1)
            return;
        float dxdy, dydx, iy0, iy1, sx0, sy0, sx1, sy1, lx, ux, ix0, ix1, cx0, cy0, cx1, cy1, cover, area, total, alpha, last, tmp, sign;
        size_t stride = size_t(dimension);
        sign = 255.5f * (y0 < y1 ? 1 : -1);
        if (sign < 0)
            tmp = x0, x0 = x1, x1 = tmp, tmp = y0, y0 = y1, y1 = tmp;
        
        dxdy = (x1 - x0) / (y1 - y0);
        dydx = 1.f / fabsf(dxdy);
        
        for (iy0 = floorf(y0), iy1 = iy0 + 1, sy0 = y0, sx0 = x0, deltas += stride * size_t(iy0);
             iy0 < y1;
             iy0 = iy1, iy1++, sy0 = sy1, sx0 = sx1, deltas += stride) {
            sy1 = y1 > iy1 ? iy1 : y1;
            sx1 = (sy1 - y0) * dxdy + x0;
            
            lx = sx0, ux = sx1;
            if (lx > ux)
                tmp = lx, lx = ux, ux = tmp;
            
            float *delta = deltas + size_t(lx);
            for (ix0 = floorf(lx), ix1 = ix0 + 1, cx0 = lx, cy0 = sy0, total = last = 0;
                 ix0 <= ux;
                 ix0 = ix1, ix1++, cx0 = cx1, cy0 = cy1, delta++) {
                cx1 = ux > ix1 ? ix1 : ux;
                cy1 = dxdy == 0 ? sy1 : (cx1 - lx) * dydx + sy0;
                cy1 = cy1 > sy1 ? sy1 : cy1;
                
                cover = (cy1 - cy0) * sign;
                area = (ix1 - (cx0 + cx1) * 0.5f);
                alpha = total + cover * area;
                total += cover;
                *delta += alpha - last;
                last = alpha;
            }
            if (ix0 < dimension)
                *delta += total - last;
        }
    }
    
    static void rasterizeBoundingBox(Bounds bounds, std::vector<Span>& spans) {
        for (float y = bounds.ly; y < bounds.uy; y++)
            spans.emplace_back(bounds.lx, y, bounds.ux - bounds.lx);
    }
};
