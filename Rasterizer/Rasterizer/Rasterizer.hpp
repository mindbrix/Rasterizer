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
    struct Span {
        Span(float x, float y, float w) : x(x), y(y), w(w) {}
        short x, y, w;
    };
    
    static void writeCellsMask(Cell *cells, Bounds device, uint8_t *mask) {
        size_t w = device.ux - device.lx, h = device.uy - device.ly, x, y;
        float cover, alpha;
        for (y = 0; y < h; y++) {
            for (cover = 0, x = 0; x < w; x++, cells++, mask++) {
                cover += cells->cover;
                alpha = fabsf(cover - cells->area);
                cells->cover = cells->area = 0;
                *mask = alpha * 255.5f;
            }
        }
    }
    static void fillMask(uint8_t *mask, Bounds device, Bounds clipped, uint8_t *color, Bitmap bitmap) {
        float px, py, w, h;//, r, g, b, a, w, h, alpha;
        w = device.ux - device.lx, h = device.uy - device.ly;
        uint8_t *cover;
        uint32_t pixel = *((uint32_t *)color), *addr;
        simd_float4 bgra = { float(color[0]), float(color[1]), float(color[2]), float(color[3]) };
        bgra /= 255.f;
        
       // r = color[2] / 255.f, g = color[1] / 255.f, b = color[0] / 255.f, a = color[3] / 255.f;
        
        for (py = clipped.ly; py < clipped.uy; py++) {
            cover = & mask[size_t((py - device.ly) * w + (clipped.lx - device.lx))];
            addr = (uint32_t *)bitmap.pixelAddress(clipped.lx, py);
            for (px = clipped.lx; px < clipped.ux; px++, cover++, addr++) {
                if (*cover) {
                    if (*cover > 254)
                        *addr = pixel;
                    else {
                        *((simd_uchar4 *)addr) = simd_uchar(bgra * float(*cover));
                        
//                        alpha = cover;
//                        *addr = (uint32_t(b * alpha)) | (uint32_t(g * alpha) << 8) | (uint32_t(r * alpha) << 16) | (uint32_t(a * alpha) << 24);
                    }
                }
            }
        }
    }
    
    static void fillSpans(std::vector<Span>& spans, uint8_t *color, Bitmap bitmap) {
        for (Span& span : spans)
            memset_pattern4(bitmap.pixelAddress(span.x, span.y), color, span.w * bitmap.bytespp);
    }
    
    static void renderBoundingBoxes(Bounds *bounds, size_t count, AffineTransform ctm, Bitmap bitmap) {
        Cell cells[kCellsDimension * kCellsDimension];
        uint8_t mask[kCellsDimension * kCellsDimension];
        
        memset(cells, 0, sizeof(cells));
        
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
        sign = y0 < y1 ? 1 : -1;
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
