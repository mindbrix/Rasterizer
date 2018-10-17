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
    struct AffineTransform {
        AffineTransform(float a, float b, float c, float d, float tx, float ty) : a(a), b(b), c(c), d(d), tx(tx), ty(ty) {}
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
            float w, h;
            w = ux - lx, h = uy - ly;
            AffineTransform t = { ctm.a * w, ctm.b * w, ctm.c * h, ctm.d * h,
                lx * ctm.a + ly * ctm.c + ctm.tx, lx * ctm.b + ly * ctm.d + ctm.ty };
            
            float wa = t.a < 0 ? 1 : 0, wb = t.b < 0 ? 1 : 0, wc = t.c < 0 ? 1 : 0, wd = t.d < 0 ? 1 : 0;
            return {
                t.tx + wa * t.a + wc * t.c,
                t.ty + wb * t.b + wd * t.d,
                t.tx + (1 - wa) * t.a + (1 - wc) * t.c,
                t.ty + (1 - wb) * t.b + (1 - wd) * t.d };
        }
        float lx, ly, ux, uy;
    };
    struct Span {
        Span(float x, float y, float w) : x(x), y(y), w(w) {}
        short x, y, w;
    };
    
    static void fillSpans(std::vector<Span>& spans, uint8_t *color, Bitmap bitmap) {
        for (Span& span : spans)
            memset_pattern4(bitmap.pixelAddress(span.x, span.y), color, span.w * bitmap.bpp / 8);
    }
    
    static void renderBounds(Bounds *bounds, size_t count, AffineTransform ctm, Bitmap bitmap) {
        std::vector<Span> spans;
        uint8_t red[4] = { 0, 0, 255, 255 };
        Bounds clipBounds(0, 0, bitmap.width, bitmap.height);
        
        for (size_t i = 0; i < count; i++) {
            Bounds device = bounds[i].transform(ctm);
            Bounds clipped = device.intersected(clipBounds);
            if (clipped.lx != clipped.ux || clipped.ly != clipped.uy)
                rasterizeBounds(clipped, spans);
        }
        fillSpans(spans, red, bitmap);
    }
    
    static void rasterizeBounds(Bounds bounds, std::vector<Span>& spans) {
        Bounds ibounds = bounds.integral();
        for (float y = ibounds.ly; y < ibounds.uy; y++)
            spans.emplace_back(ibounds.lx, y, ibounds.ux - ibounds.lx);
    }
};
