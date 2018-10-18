//
//  Rasterizer.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 17/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import <vector>

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
        Bitmap(void *data, size_t width, size_t height, size_t rowBytes, size_t bpp)
        : data((uint8_t *)data), width(width), height(height), rowBytes(rowBytes), bpp(bpp), bytespp(bpp / 8) {}
        inline uint8_t *pixelAddress(short x, short y) {
            return data + rowBytes * (height - 1 - y) + x * bytespp;
        }
        uint8_t *data;
        size_t width, height, rowBytes, bpp, bytespp;
    };
    struct Bounds {
        Bounds(float lx, float ly, float ux, float uy) : lx(lx), ly(ly), ux(ux), uy(uy) {}
        Bounds integral() {
            return { floorf(lx), floorf(ly), ceilf(ux), ceilf(uy) };
        }
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
        float area, cover;
    };
    struct Span {
        Span(float x, float y, float w) : x(x), y(y), w(w) {}
        short x, y, w;
    };
    
    static void fillCells(Cell *cells, Bounds device, Bounds clipped, uint8_t *color, Bitmap bitmap) {
        float x, y, w, h, cover, area, alpha, px, py;
        w = device.ux - device.lx, h = device.uy - device.ly;
        for (y = 0; y < h; y++) {
            Cell *cell = cells + size_t(y) * kCellsDimension;
            cover = 0;
            for (x = 0; x < w; x++, cell++) {
                cover += cell->cover;
                area = cell->area;
                alpha = cover - area;
                cell->cover = cell->area = 0;
                
                if (fabs(alpha) > 1e-3) {
                    px = x + device.lx, py = y + device.ly;
                    if (px >= clipped.lx && px < clipped.ux && py >= clipped.ly && py < clipped.uy) {
                        memset_pattern4(bitmap.pixelAddress(px, py), color, bitmap.bytespp);
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
        memset(cells, 0, sizeof(cells));
        
        std::vector<Span> spans;
        uint8_t red[4] = { 0, 0, 255, 255 };
        Bounds clipBounds(0, 0, bitmap.width, bitmap.height);
        for (size_t i = 0; i < count; i++) {
            assert(clipBounds.lx >= 0);
            Bounds device = bounds[i].transform(ctm).integral();
            Bounds clipped = device.intersected(clipBounds);
            if (clipped.lx != clipped.ux && clipped.ly != clipped.uy) {
                if (device.ux - device.lx < kCellsDimension && device.uy - device.ly < kCellsDimension) {
                    AffineTransform cellCTM = { ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx - device.lx, ctm.ty - device.ly };
                    AffineTransform unit = cellCTM.unit(bounds[i].lx, bounds[i].ly, bounds[i].ux, bounds[i].uy);
                    float x0 = unit.tx, y0 = unit.ty, x1 = x0 + unit.a, y1 = y0 + unit.b, x2 = x1 + unit.c, y2 = y1 + unit.d, x3 = x0 + unit.c, y3 = y0 + unit.d;
                    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0, x1 = x1 < 0 ? 0 : x1, y1 = y1 < 0 ? 0 : y1;
                    x2 = x2 < 0 ? 0 : x2, y2 = y2 < 0 ? 0 : y2, x3 = x3 < 0 ? 0 : x3, y3 = y3 < 0 ? 0 : y3;
                    addCellSegment(x0, y0, x1, y1, cells);
                    addCellSegment(x1, y1, x2, y2, cells);
                    addCellSegment(x2, y2, x3, y3, cells);
                    addCellSegment(x3, y3, x0, y0, cells);

                    fillCells(cells, device, clipped, red, bitmap);
                } else
                    rasterizeBoundingBox(clipped, spans);
            }
        }
        fillSpans(spans, red, bitmap);
    }
    
    static void addCellSegment(float x0, float y0, float x1, float y1, Cell *cells) {
        if (y0 == y1)
            return;
        float dxdy, dydx, ly, uy, iy0, iy1, sx0, sy0, sx1, sy1, slx, sux, ix0, ix1, cx0, cy0, cx1, cy1, cover, area;
        dxdy = (x1 - x0) / (y1 - y0);
        dydx = dxdy == 0 ? 0 : 1.0 / dxdy;
        ly = y0 < y1 ? y0 : y1;
        uy = y0 > y1 ? y0 : y1;
        for (iy0 = floorf(ly), iy1 = iy0 + 1; iy0 < uy; iy0 = iy1, iy1++) {
            sy0 = y0 < iy0 ? iy0 : y0 > iy1 ? iy1 : y0;
            sx0 = (sy0 - y0) * dxdy + x0;
            sy1 = y1 < iy0 ? iy0 : y1 > iy1 ? iy1 : y1;
            sx1 = (sy1 - y0) * dxdy + x0;
            
            slx = sx0 < sx1 ? sx0 : sx1;
            sux = sx0 > sx1 ? sx0 : sx1;
            Cell *cell = cells + size_t(iy0) * kCellsDimension + size_t(slx);
            for (ix0 = floorf(slx), ix1 = ix0 + 1; ix0 <= sux; ix0 = ix1, ix1++, cell++) {
                cx0 = sx0 < ix0 ? ix0 : sx0 > ix1 ? ix1 : sx0;
                cy0 = dydx == 0 ? sy0 : (cx0 - sx0) * dydx + sy0;
                cx1 = sx1 < ix0 ? ix0 : sx1 > ix1 ? ix1 : sx1;
                cy1 = dydx == 0 ? sy1 : (cx1 - sx0) * dydx + sy0;
                
                cover = cy1 - cy0;
                area = cover * (cx0 - ix0 + cx1 - ix0) * 0.5;
                cell->cover += cover;
                cell->area += area;
            }
        }
    }
    static void rasterizeBoundingBox(Bounds bounds, std::vector<Span>& spans) {
        assert(bounds.lx >= 0);
        for (float y = bounds.ly; y < bounds.uy; y++)
            spans.emplace_back(bounds.lx, y, bounds.ux - bounds.lx);
    }
};
