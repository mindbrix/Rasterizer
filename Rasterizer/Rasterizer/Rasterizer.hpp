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
    static const size_t kCellsDimension = 64;
    
    struct AffineTransform {
        AffineTransform(float a, float b, float c, float d, float tx, float ty) : a(a), b(b), c(c), d(d), tx(tx), ty(ty) {}
        
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
    struct Cell {
        float cover, area;
    };
    struct Context {
        Context() { memset(cells, 0, sizeof(cells)); }
        
        Cell cells[kCellsDimension * kCellsDimension];
        uint8_t mask[kCellsDimension * kCellsDimension];
    };
    struct Span {
        Span(float x, float y, float w) : x(x), y(y), w(w) {}
        short x, y, w;
    };
    
    static void writeMaskRowSSE(Cell *cells, size_t w, uint8_t *mask) {
        __m128 SIGNMASK = _mm_castsi128_ps(_mm_set1_epi32(0x80000000));
        __m128 covers, areas, alpha0, alpha1;
        __m128i a16, a8;
        float cover = 0, c0, c1, c2, c3, a0, a1, a2, a3;
        
        while (w >> 3) {
            cover += cells->cover, c0 = cover, a0 = cells->area, cells->area = cells->cover = 0, cells++;
            cover += cells->cover, c1 = cover, a1 = cells->area, cells->area = cells->cover = 0, cells++;
            cover += cells->cover, c2 = cover, a2 = cells->area, cells->area = cells->cover = 0, cells++;
            cover += cells->cover, c3 = cover, a3 = cells->area, cells->area = cells->cover = 0, cells++;
            covers = _mm_set_ps(c3, c2, c1, c0);
            areas = _mm_set_ps(a3, a2, a1, a0);
            alpha0 = _mm_andnot_ps(SIGNMASK, _mm_sub_ps(covers, areas));
            
            cover += cells->cover, c0 = cover, a0 = cells->area, cells->area = cells->cover = 0, cells++;
            cover += cells->cover, c1 = cover, a1 = cells->area, cells->area = cells->cover = 0, cells++;
            cover += cells->cover, c2 = cover, a2 = cells->area, cells->area = cells->cover = 0, cells++;
            cover += cells->cover, c3 = cover, a3 = cells->area, cells->area = cells->cover = 0, cells++;
            covers = _mm_set_ps(c3, c2, c1, c0);
            areas = _mm_set_ps(a3, a2, a1, a0);
            alpha1 = _mm_andnot_ps(SIGNMASK, _mm_sub_ps(covers, areas));
            
            a16 = _mm_packs_epi32(_mm_cvttps_epi32(alpha0), _mm_cvttps_epi32(alpha1));
            a8 = _mm_packus_epi16(a16, a16);
            *((uint64_t *)mask) = _mm_cvtsi128_si64(a8);
            
            mask += 8, w -= 8;
        }
        while (w--) {
            cover += cells->cover;
            *mask++ = fabsf(cover - cells->area);
            cells->area = cells->cover = 0;
            cells++;
        }
    }
    
    static void writeCellsMask(Cell *cells, Bounds device, uint8_t *mask) {
        size_t w = device.ux - device.lx, h = device.uy - device.ly;
        for (size_t y = 0; y < h; y++, cells += w, mask += w)
            writeMaskRowSSE(cells, w, mask);
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
    
    static void renderBoundingBoxes(Context& context, Bounds *bounds, size_t count, AffineTransform ctm, Bitmap bitmap) {
        Cell *cells = context.cells;
        uint8_t *mask = context.mask;
        
        std::vector<Span> spans;
        uint8_t red[4] = { 0, 0, 255, 255 };
        Bounds clipBounds(0, 0, bitmap.width, bitmap.height);
        for (size_t i = 0; i < count; i++) {
            Bounds device = bounds[i].transform(ctm).integral();
            Bounds clipped = device.intersected(clipBounds);
            if (clipped.lx != clipped.ux && clipped.ly != clipped.uy) {
                if (device.ux - device.lx < kCellsDimension && device.uy - device.ly < kCellsDimension) {
                    AffineTransform cellCTM = { ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx - device.lx, ctm.ty - device.ly };
                    AffineTransform unit = cellCTM.unit(bounds[i].lx, bounds[i].ly, bounds[i].ux, bounds[i].uy);
                    float x0 = unit.tx, y0 = unit.ty, x1 = x0 + unit.a, y1 = y0 + unit.b, x2 = x1 + unit.c, y2 = y1 + unit.d, x3 = x0 + unit.c, y3 = y0 + unit.d;
                    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0, x1 = x1 < 0 ? 0 : x1, y1 = y1 < 0 ? 0 : y1;
                    x2 = x2 < 0 ? 0 : x2, y2 = y2 < 0 ? 0 : y2, x3 = x3 < 0 ? 0 : x3, y3 = y3 < 0 ? 0 : y3;
                    float dimension = device.ux - device.lx;
                    addCellSegment(x0, y0, x1, y1, cells, dimension);
                    addCellSegment(x1, y1, x2, y2, cells, dimension);
                    addCellSegment(x2, y2, x3, y3, cells, dimension);
                    addCellSegment(x3, y3, x0, y0, cells, dimension);
                    
//                    addCellSegment(x0, y0, x1, y1, cells, dimension);
//                    addCellSegment(x1, y1, x2, y2, cells, dimension);
//                    addCellSegment(x2, y2, x3, y3, cells, dimension);
//                    addCellSegment(x3, y3, x0, y0, cells, dimension);
//                    addCellSegment(x0, y0, x1, y1, cells, dimension);
//                    addCellSegment(x1, y1, x2, y2, cells, dimension);
//                    addCellSegment(x2, y2, x3, y3, cells, dimension);
//                    addCellSegment(x3, y3, x0, y0, cells, dimension);
//                    addCellSegment(x0, y0, x1, y1, cells, dimension);
//                    addCellSegment(x1, y1, x2, y2, cells, dimension);
//                    addCellSegment(x2, y2, x3, y3, cells, dimension);
//                    addCellSegment(x3, y3, x0, y0, cells, dimension);
                                        
                    writeCellsMask(cells, device, mask);
                    fillMask(mask, device, clipped, red, bitmap);
                } else
                    rasterizeBoundingBox(clipped, spans);
            }
        }
        fillSpans(spans, red, bitmap);
        
    }
    
    static void addCellSegment(float x0, float y0, float x1, float y1, Cell *cells, float dimension) {
        if (y0 == y1)
            return;
        float dxdy, dydx, ly, uy, iy0, iy1, sx0, sy0, sx1, sy1, ix0, ix1, cx0, cy0, cx1, cy1, cover, tmp, sign;
        dxdy = (x1 - x0) / (y1 - y0);
        dydx = dxdy == 0 ? 0 : 1.0 / fabsf(dxdy);
        ly = y0 < y1 ? y0 : y1;
        uy = y0 > y1 ? y0 : y1;
        sign = 255.5f * (y0 < y1 ? 1 : -1);
        for (iy0 = floorf(ly), iy1 = iy0 + 1; iy0 < uy; iy0 = iy1, iy1++) {
            sy0 = y0 < iy0 ? iy0 : y0 > iy1 ? iy1 : y0;
            sx0 = (sy0 - y0) * dxdy + x0;
            sy1 = y1 < iy0 ? iy0 : y1 > iy1 ? iy1 : y1;
            sx1 = (sy1 - y0) * dxdy + x0;
            
            if (sign < 0)
                tmp = sy0, sy0 = sy1, sy1 = tmp;
            if (sx0 > sx1)
                tmp = sx0, sx0 = sx1, sx1 = tmp;
            Cell *cell = cells + size_t(iy0 * dimension + sx0);
            for (ix0 = floorf(sx0), ix1 = ix0 + 1, cx0 = sx0, cy0 = sy0;
                 ix0 <= sx1;
                 ix0 = ix1, ix1++, cx0 = cx1, cy0 = cy1, cell++) {
                cx1 = sx1 > ix1 ? ix1 : sx1;
                cy1 = dydx == 0 ? sy1 : (cx1 - sx0) * dydx + sy0;
                cover = (cy1 - cy0) * sign;
                cell->cover += cover;
                cell->area += cover * ((cx0 + cx1) * 0.5f - ix0);
            }
        }
    }
    
    static void rasterizeBoundingBox(Bounds bounds, std::vector<Span>& spans) {
        for (float y = bounds.ly; y < bounds.uy; y++)
            spans.emplace_back(bounds.lx, y, bounds.ux - bounds.lx);
    }
};
