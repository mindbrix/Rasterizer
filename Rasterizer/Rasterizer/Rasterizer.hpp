//
//  Rasterizer.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 17/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import <vector>
#import <simd/simd.h>

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
    struct Span {
        Span(float x, float y, float w) : x(x), y(y), w(w) {}
        short x, y, w;
    };
    
    static void writeMaskRow(float *deltas, size_t w, uint8_t *mask) {
        float cover = 0;
        while (w--) {
            cover += *deltas, *deltas++ = 0;
            *mask++ = fabsf(cover);
        }
    }
    
    static void writeCellsMask(float *deltas, Bounds device, uint8_t *mask) {
        size_t w = device.ux - device.lx, h = device.uy - device.ly;
        for (size_t y = 0; y < h; y++, deltas += w, mask += w)
            writeMaskRow(deltas, w, mask);
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
    
    static void fillMask(uint8_t *mask, Bounds device, Bounds clipped, uint8_t *color, Bitmap bitmap) {
        size_t w = device.ux - device.lx;
        uint32_t bgra = *((uint32_t *)color);
        uint32_t *addr = (uint32_t *)bitmap.pixelAddress(clipped.lx, clipped.ly);
        uint8_t *maskaddr = mask + size_t(w * (clipped.ly - device.ly) + (clipped.lx - device.lx));
        copyMaskSSE(bgra, addr, bitmap.rowBytes, maskaddr, w, clipped.ux - clipped.lx, clipped.uy - clipped.ly);
    }
    
    static void fillSpans(std::vector<Span>& spans, uint8_t *color, Bitmap bitmap) {
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
                        memset_pattern4(addr, color, (ux - lx) * bitmap.bytespp);
                    else
                        memset_pattern4(addr, color, bitmap.bytespp);
                }
            }
        }
    }
    
    static void renderPolygons(Context& context, Bounds bounds, std::vector<std::vector<float>>& polygons, uint8_t *red, AffineTransform ctm, Bitmap bitmap) {
        float *deltas = context.deltas;
        uint8_t *mask = context.mask;
        std::vector<Span> spans;
        
        Bounds clipBounds(0, 0, bitmap.width, bitmap.height);
        Bounds device = bounds.transform(ctm).integral();
        Bounds clipped = device.intersected(clipBounds);
        if (clipped.lx != clipped.ux && clipped.ly != clipped.uy) {
            if (device.ux - device.lx < kCellsDimension && device.uy - device.ly < kCellsDimension) {
                AffineTransform cellCTM = { ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx - device.lx, ctm.ty - device.ly };
                float dimension = device.ux - device.lx;
                for (std::vector<float>& polygon : polygons)
                    addPolygon(& polygon[0], polygon.size() / 2, cellCTM, deltas, dimension);
                
                writeCellsMask(deltas, device, mask);
                fillMask(mask, device, clipped, red, bitmap);
            } else
                rasterizeBoundingBox(clipped, spans);
        }
        fillSpans(spans, red, bitmap);
    }
    
    static void addPolygon(float *points, size_t npoints, AffineTransform ctm, float *deltas, float dimension) {
        float x0, y0, x1, y1, *p;
        size_t i;
        
        p = & points[npoints * 2 - 2];
        x0 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y0 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
        
        for (p = points, i = 0; i < npoints; i++, p += 2, x0 = x1, y0 = y1) {
            x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
            addCellSegment(x0, y0, x1, y1, deltas, dimension);
        }
    }
    
    static inline void addCellSegment(float x0, float y0, float x1, float y1, float *deltas, float dimension) {
        if (y0 == y1)
            return;
        float dxdy, dydx, iy0, iy1, sx0, sy0, sx1, sy1, lx, ux, ix0, ix1, cx0, cy0, cx1, cy1, cover, area, total, alpha, last, tmp, sign;
        size_t stride = size_t(dimension);
        sign = 255.5f * (y0 < y1 ? 1 : -1);
        if (sign < 0)
            tmp = x0, x0 = x1, x1 = tmp, tmp = y0, y0 = y1, y1 = tmp;
        
        dxdy = (x1 - x0) / (y1 - y0);
        dydx = dxdy == 0 ? 0 : 1.0 / fabsf(dxdy);
        
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
                cy1 = dydx == 0 ? sy1 : (cx1 - lx) * dydx + sy0;
                
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
