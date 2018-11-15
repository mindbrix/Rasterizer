//
//  Rasterizer.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 17/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#define RASTERIZER_SIMD 1

#ifdef RASTERIZER_SIMD
#include <immintrin.h>
#endif

#import <vector>
#pragma clang diagnostic ignored "-Wcomma"

struct Rasterizer {
    static const size_t kDeltasDimension = 128;
    static constexpr float kFloatOffset = 5e-2;
    
    struct AffineTransform {
        AffineTransform() {}
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
            Atom() { memset(types, kNull, sizeof(types)); }
            float       points[30];
            uint8_t     types[8];
        };
        Path() : index(Atom::kCapacity), px(0), py(0), bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX) {}
        
        float *alloc(Atom::Type type, size_t size) {
            if (index + size > Atom::kCapacity)
                index = 0, atoms.emplace_back();
            atoms.back().types[index / 2] |= (uint8_t(type) << ((index & 1) * 4));
            index += size;
            return atoms.back().points + (index - size) * 2;
        }
        void moveTo(float x, float y) {
            float *points = alloc(Atom::kMove, 1);
            bounds.lx = bounds.lx < x ? bounds.lx : x, bounds.ly = bounds.ly < y ? bounds.ly : y;
            bounds.ux = bounds.ux > x ? bounds.ux : x, bounds.uy = bounds.uy > y ? bounds.uy : y;
            px = *points++ = x, py = *points++ = y;
        }
        void lineTo(float x, float y) {
            if (px != x || py != y) {
                float *points = alloc(Atom::kLine, 1);
                bounds.lx = bounds.lx < x ? bounds.lx : x, bounds.ly = bounds.ly < y ? bounds.ly : y;
                bounds.ux = bounds.ux > x ? bounds.ux : x, bounds.uy = bounds.uy > y ? bounds.uy : y;
                px = *points++ = x, py = *points++ = y;
            }
        }
        void quadTo(float cx, float cy, float x, float y) {
            float ax, ay, bx, by, det, dot;
            ax = cx - px, ay = cy - py, bx = x - cx, by = y - cy;
            det = ax * by - ay * bx, dot = ax * bx + ay * by;
            if (fabsf(det) < 1e-6f) {
                if (dot < 0.f && det)
                    lineTo((px + x) * 0.25f + cx * 0.5f, (py + y) * 0.25f + cy * 0.5f);
                lineTo(x, y);
            } else {
                float *points = alloc(Atom::kQuadratic, 2);
                bounds.lx = bounds.lx < cx ? bounds.lx : cx, bounds.ly = bounds.ly < cy ? bounds.ly : cy;
                bounds.ux = bounds.ux > cx ? bounds.ux : cx, bounds.uy = bounds.uy > cy ? bounds.uy : cy;
                *points++ = cx, *points++ = cy;
                bounds.lx = bounds.lx < x ? bounds.lx : x, bounds.ly = bounds.ly < y ? bounds.ly : y;
                bounds.ux = bounds.ux > x ? bounds.ux : x, bounds.uy = bounds.uy > y ? bounds.uy : y;
                px = *points++ = x, py = *points++ = y;
            }
        }
        void cubicTo(float cx0, float cy0, float cx1, float cy1, float x, float y) {
            float dx, dy;
            dx = 3.f * (cx0 - cx1) - px + x, dy = 3.f * (cy0 - cy1) - py + y;
            if (dx * dx + dy * dy < 1e-6f)
                quadTo((3.f * (cx0 + cx1) - px - x) * 0.25f, (3.f * (cy0 + cy1) - py - y) * 0.25f, x, y);
            else {
                float *points = alloc(Atom::kCubic, 3);
                bounds.lx = bounds.lx < cx0 ? bounds.lx : cx0, bounds.ly = bounds.ly < cy0 ? bounds.ly : cy0;
                bounds.ux = bounds.ux > cx0 ? bounds.ux : cx0, bounds.uy = bounds.uy > cy0 ? bounds.uy : cy0;
                *points++ = cx0, *points++ = cy0;
                bounds.lx = bounds.lx < cx1 ? bounds.lx : cx1, bounds.ly = bounds.ly < cy1 ? bounds.ly : cy1;
                bounds.ux = bounds.ux > cx1 ? bounds.ux : cx1, bounds.uy = bounds.uy > cy1 ? bounds.uy : cy1;
                *points++ = cx1, *points++ = cy1;
                bounds.lx = bounds.lx < x ? bounds.lx : x, bounds.ly = bounds.ly < y ? bounds.ly : y;
                bounds.ux = bounds.ux > x ? bounds.ux : x, bounds.uy = bounds.uy > y ? bounds.uy : y;
                px = *points++ = x, py = *points++ = y;
            }
        }
        void close() {
            alloc(Atom::kClose, 1);
        }
        std::vector<Atom> atoms;
        size_t index;
        float px, py;
        Bounds bounds;
    };
    struct Bitmap {
        Bitmap() {}
        Bitmap(void *data, size_t width, size_t height, size_t rowBytes, size_t bpp) : data((uint8_t *)data), width(width), height(height), rowBytes(rowBytes), bpp(bpp), bytespp(bpp / 8) {}
        inline uint32_t *pixelAddress(short x, short y) { return (uint32_t *)(data + rowBytes * (height - 1 - y) + x * bytespp); }
        uint8_t *data;
        size_t width, height, rowBytes, bpp, bytespp;
    };
    struct Delta {
        Delta() {}
        inline Delta(float x, float delta) : x(x), delta(delta) {}
        inline bool operator< (const Delta& other) const { return x < other.x; }
        short x, delta;
    };
    struct Span {
        Span() {}
        inline Span(float x, float w) : x(x), w(w) {}
        short x, w;
    };
    template<typename T>
    struct Line {
        Line() : idx(0), size(0) {}
        void empty() { idx = 0; }
        inline T *alloc() {
            if (idx >= size)
                elems.resize(elems.size() == 0 ? 8 : elems.size() * 1.5), size = elems.size(), base = & elems[0];
            return base + idx++;
        }
        size_t idx, size;
        T *base;
        std::vector<T> elems;
    };
    typedef Line<Delta> Scanline;
    typedef Line<Span> Spanline;

    struct Context {
        Context() { memset(deltas, 0, sizeof(deltas)); }
        
        void setBitmap(Bitmap bm) {
            bitmap = bm;
            if (cliplines.size() != bm.height)
                cliplines.resize(bm.height);
            for (Scanline& clipline : cliplines)
                clipline.empty();
            if (scanlines.size() != bm.height)
                scanlines.resize(bm.height);
            if (spanlines.size() != bm.height)
                spanlines.resize(bm.height);
            device = Bounds(0, 0, bm.width, bm.height);
            clip = Bounds(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);
        }
        Bitmap bitmap;
        Bounds clip, device;
        float deltas[kDeltasDimension * kDeltasDimension];
        uint8_t mask[kDeltasDimension * kDeltasDimension];
        std::vector<Scanline> cliplines;
        std::vector<Scanline> scanlines;
        std::vector<Spanline> spanlines;
    };
    struct Scene {
        void empty() { ctms.resize(0), paths.resize(0), bgras.resize(0); }
        std::vector<uint32_t> bgras;
        std::vector<AffineTransform> ctms;
        std::vector<Path> paths;
    };
    
    static void writePathToBitmap(Path& path, AffineTransform ctm, uint32_t bgra, Context& context) {
        if (path.bounds.lx == FLT_MAX)
            return;
        Bounds dev = path.bounds.transform(ctm);
        Bounds device = dev.integral();
        Bounds clipped = device.intersected(context.device.intersected(context.clip));
        float w, h, elx, ely, eux, euy, sx, sy;
        if (!clipped.isZero()) {
            w = device.ux - device.lx, h = device.uy - device.ly;
            if (w * h < kDeltasDimension * kDeltasDimension) {
                elx = dev.lx - device.lx, elx = elx < kFloatOffset ? kFloatOffset : 0;
                eux = device.ux - dev.ux, eux = eux < kFloatOffset ? kFloatOffset : 0;
                ely = dev.ly - device.ly, ely = ely < kFloatOffset ? kFloatOffset : 0;
                euy = device.uy - dev.uy, euy = euy < kFloatOffset ? kFloatOffset : 0;
                sx = (w - elx - eux) / w, sy = (h - ely - euy) / h;
                AffineTransform bias(sx, 0, 0, sy, elx, ely);
                AffineTransform deltasCTM = bias.concat(AffineTransform(ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx - device.lx, ctm.ty - device.ly));
                writePathToDeltas(path, deltasCTM, context.deltas, w);
                writeDeltasToMask(context.deltas, device, context.mask);
                writeMaskToBitmap(context.mask, device, clipped, bgra, context.bitmap);
            } else {
                writeClippedPathToScanlines(path, ctm, clipped, & context.scanlines[0]);
                writeScanlinesToSpans(context.scanlines, clipped, context.spanlines, true);
                writeSpansToBitmap(context.spanlines, clipped, bgra, context.bitmap);
            }
        }
    }
    
    static void writePathToDeltas(Path& path, AffineTransform ctm, float *deltas, size_t stride) {
        float sx, sy, x0, y0, x1, y1, x2, y2, x3, y3, *p, scale;
        size_t index;
        uint8_t type;
        x0 = y0 = sx = sy = FLT_MAX, scale = 255.5f;
        for (Path::Atom& atom : path.atoms)
            for (index = 0, type = 0xF & atom.types[0]; type != Path::Atom::kNull; type = 0xF & (atom.types[index / 2] >> ((index & 1) * 4))) {
                p = atom.points + index * 2;
                switch (type) {
                    case Path::Atom::kMove:
                        if (sx != FLT_MAX && (sx != x0 || sy != y0))
                            writeSegmentToDeltasOrScanlines(x0, y0, sx, sy, scale, deltas, stride, nullptr);
                        sx = x0 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, sy = y0 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        index++;
                        break;
                    case Path::Atom::kLine:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        writeSegmentToDeltasOrScanlines(x0, y0, x1, y1, scale, deltas, stride, nullptr);
                        x0 = x1, y0 = y1;
                        index++;
                        break;
                    case Path::Atom::kQuadratic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        writeQuadraticToDeltasOrScanlines(x0, y0, x1, y1, x2, y2, scale, deltas, stride, nullptr);
                        x0 = x2, y0 = y2;
                        index += 2;
                        break;
                    case Path::Atom::kCubic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        x3 = p[4] * ctm.a + p[5] * ctm.c + ctm.tx, y3 = p[4] * ctm.b + p[5] * ctm.d + ctm.ty;
                        writeCubicToDeltasOrScanlines(x0, y0, x1, y1, x2, y2, x3, y3, scale, deltas, stride, nullptr);
                        x0 = x3, y0 = y3;
                        index += 3;
                        break;
                    case Path::Atom::kClose:
                        index++;
                        break;
                }
            }
        if (sx != FLT_MAX && (sx != x0 || sy != y0))
            writeSegmentToDeltasOrScanlines(x0, y0, sx, sy, scale, deltas, stride, nullptr);
    }
    
    static void writeSegmentToDeltasOrScanlines(float x0, float y0, float x1, float y1, float deltaScale, float *deltas, size_t stride, Scanline *scanlines) {
        if (y0 == y1)
            return;
        float tmp, dxdy, iy0, iy1, *deltasRow, sx0, sy0, sx1, sy1, lx, ux, ix0, ix1, dydx, cx0, cy0, cx1, cy1, cover, area, last, *delta;
        Scanline *scanline;
        size_t ily;
        deltaScale = copysign(deltaScale, y1 - y0);
        if (deltaScale < 0)
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
                cover = (sy1 - sy0) * deltaScale;
                area = (ix1 - (ux + lx) * 0.5f);
                if (scanlines) {
                    new (scanline->alloc()) Delta(ix0, cover * area);
                    new (scanline->alloc()) Delta(ix1, cover * (1.f - area));
                } else {
                    delta = deltasRow + size_t(ix0);
                    *delta++ += cover * area;
                    if (ix1 < stride)
                        *delta += cover * (1.f - area);
                }
            } else {
                dydx = 1.f / fabsf(dxdy);
                cx0 = lx, cy0 = sy0;
                cx1 = ux < ix1 ? ux : ix1;
                cy1 = ux == lx ? sy1 : (cx1 - lx) * dydx + sy0;
                for (last = 0, delta = deltasRow + size_t(ix0);
                     ix0 <= ux;
                     ix0 = ix1, ix1++, cx0 = cx1, cx1 = ux < ix1 ? ux : ix1, cy0 = cy1, cy1 += dydx, cy1 = cy1 < sy1 ? cy1 : sy1, delta++) {
                    cover = (cy1 - cy0) * deltaScale;
                    area = (ix1 - (cx0 + cx1) * 0.5f);
                    if (scanlines)
                        new (scanline->alloc()) Delta(ix0, cover * area + last);
                    else
                        *delta += cover * area + last;
                    last = cover * (1.f - area);
                }
                if (scanlines)
                    new (scanline->alloc()) Delta(ix0, last);
                else {
                    if (ix0 < stride)
                        *delta += last;
                }
            }
        }
    }
    static void writeQuadraticToDeltasOrScanlines(float x0, float y0, float x1, float y1, float x2, float y2, float scale, float *deltas, size_t stride, Scanline *scanlines) {
        float ax, ay, a, dt, s, t, px0, py0, px1, py1;
        size_t count;
        ax = x0 + x2 - x1 - x1, ay = y0 + y2 - y1 - y1;
        a = ax * ax + ay * ay;
        if (a < 0.1f)
            writeSegmentToDeltasOrScanlines(x0, y0, x2, y2, scale, deltas, stride, scanlines);
        else if (a < 8.f) {
            px0 = (x0 + x2) * 0.25f + x1 * 0.5f, py0 = (y0 + y2) * 0.25f + y1 * 0.5f;
            writeSegmentToDeltasOrScanlines(x0, y0, px0, py0, scale, deltas, stride, scanlines);
            writeSegmentToDeltasOrScanlines(px0, py0, x2, y2, scale, deltas, stride, scanlines);
        } else {
            count = 3.f + floorf(sqrtf(sqrtf(a - 8.f))), dt = 1.f / count, t = 0;
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
        float cx, bx, ax, cy, by, ay, s, t, a, px0, py0, px1, py1, dt, pw0, pw1, pw2, pw3;
        size_t count;
        cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
        cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
        s = fabsf(ax) + fabsf(bx), t = fabsf(ay) + fabsf(by);
        a = s * s + t * t;
        if (a < 0.1f)
            writeSegmentToDeltasOrScanlines(x0, y0, x3, y3, scale, deltas, stride, scanlines);
        else if (a < 8.f) {
            px0 = (x0 + x3) * 0.125f + (x1 + x2) * 0.375f, py0 = (y0 + y3) * 0.125f + (y1 + y2) * 0.375f;
            writeSegmentToDeltasOrScanlines(x0, y0, px0, py0, scale, deltas, stride, scanlines);
            writeSegmentToDeltasOrScanlines(px0, py0, x3, y3, scale, deltas, stride, scanlines);
        } else if (a < 16.f) {
            px0 = x0 * w0 + x1 * w1 + x2 * w2 + x3 * w3, py0 = y0 * w0 + y1 * w1 + y2 * w2 + y3 * w3;
            px1 = x0 * w3 + x1 * w2 + x2 * w1 + x3 * w0, py1 = y0 * w3 + y1 * w2 + y2 * w1 + y3 * w0;
            writeSegmentToDeltasOrScanlines(x0, y0, px0, py0, scale, deltas, stride, scanlines);
            writeSegmentToDeltasOrScanlines(px0, py0, px1, py1, scale, deltas, stride, scanlines);
            writeSegmentToDeltasOrScanlines(px1, py1, x3, y3, scale, deltas, stride, scanlines);
        } else {
            count = 4.f + floorf(sqrtf(sqrtf(a - 16.f))), dt = 1.f / count, t = 0.f;
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
        float ly, uy, iy0, iy1, sy0, sy1, ix0, ix1, cover, area;
        Scanline *scanline;
        ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
        ix0 = floorf(x), ix1 = ix0 + 1.f, area = ix1 - x;
        for (iy0 = floorf(ly), iy1 = iy0 + 1, scanline = scanlines + size_t(iy0); iy0 < uy; iy0 = iy1, iy1++, scanline++) {
            sy0 = y0 < iy0 ? iy0 : y0 > iy1 ? iy1 : y0;
            sy1 = y1 < iy0 ? iy0 : y1 > iy1 ? iy1 : y1;
            cover = (sy1 - sy0) * 32767.f;
            new (scanline->alloc()) Delta(ix0, cover * area);
            if (area < 1.f)
                new (scanline->alloc()) Delta(ix0, cover * (1.f - area));
        }
    }

    static void writeClippedPathToScanlines(Path& path, AffineTransform ctm, Bounds clip, Scanline *scanlines) {
        float sx, sy, x0, y0, x1, y1, x2, y2, x3, y3, *p;
        bool fsx, fsy, fx0, fy0, fx1, fy1, fx2, fy2, fx3, fy3;
        size_t index;
        uint8_t type;
        x0 = y0 = sx = sy = FLT_MAX;
        fx0 = fy0 = fsx = fsy = false;
        for (Path::Atom& atom : path.atoms)
            for (index = 0, type = 0xF & atom.types[0]; type != Path::Atom::kNull; type = 0xF & (atom.types[index / 2] >> ((index & 1) * 4))) {
                p = atom.points + index * 2;
                switch (type) {
                    case Path::Atom::kMove:
                        if (sx != FLT_MAX && (sx != x0 || sy != y0)) {
                            if (fx0 || fy0 || fsx || fsy)
                                writeClippedSegmentToScanlines(x0, y0, sx, sy, clip, scanlines);
                            else
                                writeSegmentToDeltasOrScanlines(x0, y0, sx, sy, 32767.f, nullptr, 0, scanlines);
                        }
                        sx = x0 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, sy = y0 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        fsx = fx0 = x0 < clip.lx || x0 >= clip.ux, fsy = fy0 = y0 < clip.ly || y0 >= clip.uy;
                        index++;
                        break;
                    case Path::Atom::kLine:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        fx1 = x1 < clip.lx || x1 >= clip.ux, fy1 = y1 < clip.ly || y1 >= clip.uy;
                        if (fx0 || fy0 || fx1 || fy1)
                            writeClippedSegmentToScanlines(x0, y0, x1, y1, clip, scanlines);
                        else
                            writeSegmentToDeltasOrScanlines(x0, y0, x1, y1, 32767.f, nullptr, 0, scanlines);
                        x0 = x1, y0 = y1;
                        fx0 = fx1, fy0 = fy1;
                        index++;
                        break;
                    case Path::Atom::kQuadratic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        fx1 = x1 < clip.lx || x1 >= clip.ux, fy1 = y1 < clip.ly || y1 >= clip.uy;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        fx2 = x2 < clip.lx || x2 >= clip.ux, fy2 = y2 < clip.ly || y2 >= clip.uy;
                        if (fx0 || fy0 || fx1 || fy1 || fx2 || fy2)
                            writeClippedQuadraticToScanlines(x0, y0, x1, y1, x2, y2, clip, scanlines);
                        else
                            writeQuadraticToDeltasOrScanlines(x0, y0, x1, y1, x2, y2, 32767.f, nullptr, 0, scanlines);
                        x0 = x2, y0 = y2;
                        fx0 = fx2, fy0 = fy2;
                        index += 2;
                        break;
                    case Path::Atom::kCubic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        fx1 = x1 < clip.lx || x1 >= clip.ux, fy1 = y1 < clip.ly || y1 >= clip.uy;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        fx2 = x2 < clip.lx || x2 >= clip.ux, fy2 = y2 < clip.ly || y2 >= clip.uy;
                        x3 = p[4] * ctm.a + p[5] * ctm.c + ctm.tx, y3 = p[4] * ctm.b + p[5] * ctm.d + ctm.ty;
                        fx3 = x3 < clip.lx || x3 >= clip.ux, fy3 = y3 < clip.ly || y3 >= clip.uy;
                        if (fx0 || fy0 || fx1 || fy1 || fx2 || fy2|| fx3 || fy3)
                            writeClippedCubicToScanlines(x0, y0, x1, y1, x2, y2, x3, y3, clip, scanlines);
                        else
                            writeCubicToDeltasOrScanlines(x0, y0, x1, y1, x2, y2, x3, y3, 32767.f, nullptr, 0, scanlines);
                        x0 = x3, y0 = y3;
                        fx0 = fx3, fy0 = fy3;
                        index += 3;
                        break;
                    case Path::Atom::kClose:
                        index++;
                        break;
                }
            }
        if (sx != FLT_MAX && (sx != x0 || sy != y0)) {
            if (fx0 || fy0 || fsx || fsy)
                writeClippedSegmentToScanlines(x0, y0, sx, sy, clip, scanlines);
            else
                writeSegmentToDeltasOrScanlines(x0, y0, sx, sy, 32767.f, nullptr, 0, scanlines);
        }
    }
    static void writeClippedSegmentToScanlines(float x0, float y0, float x1, float y1, Bounds clip, Scanline *scanlines) {
        float sx0, sy0, sx1, sy1, dx, dy, ty0, ty1, tx0, tx1, t0, t1, mx;
        int i;
        sy0 = y0 < clip.ly ? clip.ly : y0 > clip.uy ? clip.uy : y0;
        sy1 = y1 < clip.ly ? clip.ly : y1 > clip.uy ? clip.uy : y1;
        if (sy0 != sy1) {
            if (x0 == x1) {
                sx0 = sx1 = x1 < clip.lx ? clip.lx : x1 > clip.ux ? clip.ux : x1;
                writeVerticalSegmentToScanlines(sx0, sy0, sy1, scanlines);
            } else {
                dx = x1 - x0, dy = y1 - y0;
                ty0 = (sy0 - y0) / dy, tx0 = (clip.lx - x0) / dx;
                ty1 = (sy1 - y0) / dy, tx1 = (clip.ux - x0) / dx;
                tx0 = tx0 < ty0 ? ty0 : tx0 > ty1 ? ty1 : tx0;
                tx1 = tx1 < ty0 ? ty0 : tx1 > ty1 ? ty1 : tx1;
                float ts[4] = { ty0, tx0 < tx1 ? tx0 : tx1, tx0 > tx1 ? tx0 : tx1, ty1 };
                for (i = 0; i < 3; i++) {
                    t0 = ts[i], t1 = ts[i + 1];
                    if (t0 != t1) {
                        sy0 = y0 + t0 * dy, sy1 = y0 + t1 * dy;
                        sy0 = sy0 < clip.ly ? clip.ly : sy0 > clip.uy ? clip.uy : sy0;
                        sy1 = sy1 < clip.ly ? clip.ly : sy1 > clip.uy ? clip.uy : sy1;
                        mx = x0 + (t0 + t1) * 0.5f * dx;
                        if (mx >= clip.lx && mx < clip.ux) {
                            sx0 = x0 + t0 * dx, sx1 = x0 + t1 * dx;
                            sx0 = sx0 < clip.lx ? clip.lx : sx0 > clip.ux ? clip.ux : sx0;
                            sx1 = sx1 < clip.lx ? clip.lx : sx1 > clip.ux ? clip.ux : sx1;
                            writeSegmentToDeltasOrScanlines(sx0, sy0, sx1, sy1, 32767.f, nullptr, 0, scanlines);
                        } else
                            writeVerticalSegmentToScanlines(mx < clip.lx ? clip.lx : clip.ux, sy0, sy1, scanlines);
                    }
                }
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
        t0 = t0 < 0 ? 0 : t0 > 1 ? 1 : t0, t1 = t1 < 0 ? 0 : t1 > 1 ? 1 : t1;
    }
    static void writeClippedQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, float t0, float t1, Bounds clip, float *q) {
        float x01, x12, x012, y01, y12, y012, t, tx01, tx12, ty01, ty12, tx012, ty012;
        x01 = x0 + t1 * (x1 - x0), x12 = x1 + t1 * (x2 - x1), x012 = x01 + t1 * (x12 - x01);
        y01 = y0 + t1 * (y1 - y0), y12 = y1 + t1 * (y2 - y1), y012 = y01 + t1 * (y12 - y01);
        t = t0 / t1;
        tx01 = x0 + t * (x01 - x0), tx12 = x01 + t * (x012 - x01), tx012 = tx01 + t * (tx12 - tx01);
        ty01 = y0 + t * (y01 - y0), ty12 = y01 + t * (y012 - y01), ty012 = ty01 + t * (ty12 - ty01);
        *q++ = tx012 < clip.lx ? clip.lx : tx012 > clip.ux ? clip.ux : tx012;
        *q++ = ty012 < clip.ly ? clip.ly : ty012 > clip.uy ? clip.uy : ty012;
        *q++ = tx12, *q++ = ty12;
        *q++ = x012 < clip.lx ? clip.lx : x012 > clip.ux ? clip.ux : x012;
        *q++ = y012 < clip.ly ? clip.ly : y012 > clip.uy ? clip.uy : y012;
    }
    static void writeClippedQuadraticToScanlines(float x0, float y0, float x1, float y1, float x2, float y2, Bounds clip, Scanline *scanlines) {
        float ly, uy, cly, cuy, A, B, ts[8], q[6], t, s, t0, t1, x, y, vx;
        size_t i;
        bool visible;
        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2;
        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2;
        cly = ly < clip.ly ? clip.ly : ly > clip.uy ? clip.uy : ly;
        cuy = uy < clip.ly ? clip.ly : uy > clip.uy ? clip.uy : uy;
        if (cly != cuy) {
            A = y0 + y2 - y1 - y1, B = 2.f * (y1 - y0);
            solveQuadratic(A, B, y0 - clip.ly, ts[0], ts[1]);
            solveQuadratic(A, B, y0 - clip.uy, ts[2], ts[3]);
            A = x0 + x2 - x1 - x1, B = 2.f * (x1 - x0);
            solveQuadratic(A, B, x0 - clip.lx, ts[4], ts[5]);
            solveQuadratic(A, B, x0 - clip.ux, ts[6], ts[7]);
            std::sort(& ts[0], & ts[8]);
            for (i = 0; i < 7; i++) {
                t0 = ts[i], t1 = ts[i + 1];
                if (t0 != t1) {
                    t = (t0 + t1) * 0.5f, s = 1.f - t;
                    y = y0 * s * s + y1 * 2.f * s * t + y2 * t * t;
                    if (y >= clip.ly && y < clip.uy) {
                        x = x0 * s * s + x1 * 2.f * s * t + x2 * t * t;
                        visible = x >= clip.lx && x < clip.ux;
                        writeClippedQuadratic(x0, y0, x1, y1, x2, y2, t0, t1, clip, q);
                        if (visible) {
                            if (fabsf(t1 - t0) < 1e-2) {
                                writeSegmentToDeltasOrScanlines(q[0], q[1], x, y, 32767.f, nullptr, 0, scanlines);
                                writeSegmentToDeltasOrScanlines(x, y, q[4], q[5], 32767.f, nullptr, 0, scanlines);
                            } else
                                writeQuadraticToDeltasOrScanlines(q[0], q[1], q[2], q[3], q[4], q[5], 32767.f, nullptr, 0, scanlines);
                        } else {
                            vx = x <= clip.lx ? clip.lx : clip.ux;
                            writeVerticalSegmentToScanlines(vx, q[1], q[5], scanlines);
                        }
                    }
                }
            }
        }
    }
    static void solveCubic(double a, double b, double c, double d, float& t0, float& t1, float& t2) {
        double a3, p, q, q2, u1, v1, p3, discriminant, mp3, mp33, r, t, cosphi, phi, crtr, sd;
        if (fabs(d) < 1e-3) {
            solveQuadratic(a, b, c, t0, t1), t2 = FLT_MAX;
        } else {
            a /= d, b /= d, c /= d, a3 = a / 3;
            p = (3.0 * b - a * a) / 3.0, p3 = p / 3.0, q = (2 * a * a * a - 9.0 * a * b + 27.0 * c) / 27.0, q2 = q / 2.0;
            discriminant = q2 * q2 + p3 * p3 * p3;
            if (discriminant < 0) {
                mp3 = -p / 3, mp33 = mp3 * mp3 * mp3, r = sqrt(mp33), t = -q / (2 * r), cosphi = t < -1 ? -1 : t > 1 ? 1 : t;
                phi = acos(cosphi), crtr = 2 * copysign(cbrt(fabs(r)), r);
                t0 = crtr * cos(phi / 3) - a3;
                t1 = crtr * cos((phi + 2 * M_PI) / 3) - a3;
                t2 = crtr * cos((phi + 4 * M_PI) / 3) - a3;
            } else if (discriminant == 0) {
                u1 = copysign(cbrt(fabs(q2)), q2);
                t0 = 2 * u1 - a3, t1 = -u1 - a3, t2 = FLT_MAX;
            } else {
                sd = sqrt(discriminant), u1 = copysign(cbrt(fabs(sd - q2)), sd - q2), v1 = copysign(cbrt(fabs(sd + q2)), sd + q2);
                t0 = u1 - v1 - a3, t1 = t2 = FLT_MAX;
            }
        }
        t0 = t0 < 0 ? 0 : t0 > 1 ? 1 : t0, t1 = t1 < 0 ? 0 : t1 > 1 ? 1 : t1, t2 = t2 < 0 ? 0 : t2 > 1 ? 1 : t2;
    }
    static void writeClippedCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float t0, float t1, Bounds clip, float *cubic) {
        float x01, x12, x23, x012, x123, x0123, y01, y12, y23, y012, y123, y0123;
        float t, tx01, tx12, tx23, tx012, tx123, tx0123, ty01, ty12, ty23, ty012, ty123, ty0123;
        x01 = x0 + t1 * (x1 - x0), x12 = x1 + t1 * (x2 - x1), x23 = x2 + t1 * (x3 - x2);
        y01 = y0 + t1 * (y1 - y0), y12 = y1 + t1 * (y2 - y1), y23 = y2 + t1 * (y3 - y2);
        x012 = x01 + t1 * (x12 - x01), x123 = x12 + t1 * (x23 - x12);
        y012 = y01 + t1 * (y12 - y01), y123 = y12 + t1 * (y23 - y12);
        x0123 = x012 + t1 * (x123 - x012);
        y0123 = y012 + t1 * (y123 - y012);
        t = t0 / t1;
        tx01 = x0 + t * (x01 - x0), tx12 = x01 + t * (x012 - x01), tx23 = x012 + t * (x0123 - x012);
        ty01 = y0 + t * (y01 - y0), ty12 = y01 + t * (y012 - y01), ty23 = y012 + t * (y0123 - y012);
        tx012 = tx01 + t * (tx12 - tx01), tx123 = tx12 + t * (tx23 - tx12);
        ty012 = ty01 + t * (ty12 - ty01), ty123 = ty12 + t * (ty23 - ty12);
        tx0123 = tx012 + t * (tx123 - tx012);
        ty0123 = ty012 + t * (ty123 - ty012);
        *cubic++ = tx0123 < clip.lx ? clip.lx : tx0123 > clip.ux ? clip.ux : tx0123;
        *cubic++ = ty0123 < clip.ly ? clip.ly : ty0123 > clip.uy ? clip.uy : ty0123;
        *cubic++ = tx123, *cubic++ = ty123,
        *cubic++ = tx23, *cubic++ = ty23;
        *cubic++ = x0123 < clip.lx ? clip.lx : x0123 > clip.ux ? clip.ux : x0123;
        *cubic++ = y0123 < clip.ly ? clip.ly : y0123 > clip.uy ? clip.uy : y0123;
    }
    static void writeClippedCubicToScanlines(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Bounds clip, Scanline *scanlines) {
        float ly, uy, cx, bx, ax, cy, by, ay, s, t, cly, cuy, A, B, C, D, ts[12], t0, t1, w0, w1, w2, w3, x, y, cubic[8], vx;
        size_t i;
        bool visible;
        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2, ly = ly < y3 ? ly : y3;
        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2, uy = uy > y3 ? uy : y3;
        cly = ly < clip.ly ? clip.ly : ly > clip.uy ? clip.uy : ly;
        cuy = uy < clip.ly ? clip.ly : uy > clip.uy ? clip.uy : uy;
        if (cly != cuy) {
            cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
            cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
            s = fabsf(ax) + fabsf(bx), t = fabsf(ay) + fabsf(by);
            if (s * s + t * t < 0.1f)
                writeClippedSegmentToScanlines(x0, y0, x3, y3, clip, scanlines);
            else {
                A = by, B = cy, C = y0, D = ay;
                solveCubic(A, B, C - clip.ly, D, ts[0], ts[1], ts[2]);
                solveCubic(A, B, C - clip.uy, D, ts[3], ts[4], ts[5]);
                A = bx, B = cx, C = x0, D = ax;
                solveCubic(A, B, C - clip.lx, D, ts[6], ts[7], ts[8]);
                solveCubic(A, B, C - clip.ux, D, ts[9], ts[10], ts[11]);
                std::sort(& ts[0], & ts[12]);
                for (i = 0; i < 11; i++) {
                    t0 = ts[i], t1 = ts[i + 1];
                    if (t0 != t1) {
                        t = (t0 + t1) * 0.5f, s = 1.f - t;
                        w0 = s * s * s, w1 = 3.f * s * s * t, w2 = 3.f * s * t * t, w3 = t * t * t;
                        y = y0 * w0 + y1 * w1 + y2 * w2 + y3 * w3;
                        if (y >= clip.ly && y < clip.uy) {
                            x = x0 * w0 + x1 * w1 + x2 * w2 + x3 * w3;
                            visible = x >= clip.lx && x < clip.ux;
                            writeClippedCubic(x0, y0, x1, y1, x2, y2, x3, y3, t0, t1, clip, cubic);
                            if (visible) {
                                if (fabsf(t1 - t0) < 1e-2) {
                                    writeSegmentToDeltasOrScanlines(cubic[0], cubic[1], x, y, 32767.f, nullptr, 0, scanlines);
                                    writeSegmentToDeltasOrScanlines(x, y, cubic[6], cubic[7], 32767.f, nullptr, 0, scanlines);
                                } else
                                    writeCubicToDeltasOrScanlines(cubic[0], cubic[1], cubic[2], cubic[3], cubic[4], cubic[5], cubic[6], cubic[7], 32767.f, nullptr, 0, scanlines);
                            } else {
                                vx = x <= clip.lx ? clip.lx : clip.ux;
                                writeVerticalSegmentToScanlines(vx, cubic[1], cubic[7], scanlines);
                            }
                        }
                    }
                }
            }
        }
    }
    
    static void writeMaskRow(float *deltas, size_t w, uint8_t *mask) {
        float cover = 0, alpha;
#ifdef RASTERIZER_SIMD
        __m128 offset = _mm_setzero_ps(), sign_mask = _mm_set1_ps(-0.);
        __m128i shuffle_mask = _mm_set1_epi32(0x0c080400);
        __m128 x, y, z;
        while (w >> 2) {
            x = _mm_loadu_ps(deltas);
            *deltas++ = 0, *deltas++ = 0, *deltas++ = 0, *deltas++ = 0;
            x = _mm_add_ps(x, _mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(x), 4)));
            x = _mm_add_ps(x, _mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(x), 8)));
            x = _mm_add_ps(x, offset);
            y = _mm_min_ps(_mm_andnot_ps(sign_mask, x), _mm_set1_ps(255.0));
            z = _mm_shuffle_epi8(_mm_cvttps_epi32(y), shuffle_mask);
            _mm_store_ss((float *)mask, _mm_castsi128_ps(z));
            offset = _mm_shuffle_ps(x, x, 0xFF);
            w -= 4, mask += 4;
        }
        _mm_store_ss(& cover, offset);
#endif
        while (w--) {
            cover += *deltas, *deltas++ = 0;
            alpha = fabsf(cover);
            *mask++ = alpha < 255.f ? alpha : 255.f;
        }
    }
    static void writeDeltasToMask(float *deltas, Bounds device, uint8_t *mask) {
        size_t stride = device.ux - device.lx, h = device.uy - device.ly;
        for (size_t y = 0; y < h; y++, deltas += stride, mask += stride)
            writeMaskRow(deltas, stride, mask);
    }
    static void writeMaskToBitmap(uint8_t *mask, Bounds device, Bounds clipped, uint32_t bgra, Bitmap bitmap) {
        size_t stride = device.ux - device.lx;
        size_t offset = stride * (clipped.ly - device.ly) + (clipped.lx - device.lx);
        size_t w = clipped.ux - clipped.lx, h = clipped.uy - clipped.ly;
        writeMaskToBitmap(mask + offset, stride, w, h, bgra, bitmap.pixelAddress(clipped.lx, clipped.ly), bitmap.rowBytes);
    }
    static void writeMaskToBitmap(uint8_t *mask, size_t maskRowBytes, size_t w, size_t h, uint32_t bgra, uint32_t *pixelAddress, size_t rowBytes) {
        uint32_t *pixel;
        uint8_t *msk, *components, a, *dst;
        size_t columns;
        components = (uint8_t *)& bgra, a = components[3];
        float src0, src1, src2, src3, srcAlpha, alpha;
        src0 = components[0], src1 = components[1], src2 = components[2], src3 = components[3];
        srcAlpha = src3 * 0.003921568627f;
#ifdef RASTERIZER_SIMD
        __m128 bgra4 = _mm_set_ps(255.f, src2, src1, src0);
        __m128 a0, m0, d0;
        __m128i a32, a16, a8;
#endif
        while (h--) {
            pixel = pixelAddress, msk = mask, columns = w;
            while (columns--) {
                if (*msk == 255 && a == 255)
                    *pixel = bgra;
                else if (*msk) {
                    alpha = float(*msk) * 0.003921568627f * srcAlpha;
                    dst = (uint8_t *)pixel;
#ifdef RASTERIZER_SIMD
                    a0 = _mm_set1_ps(alpha);
                    m0 = _mm_mul_ps(bgra4, a0);
                    if (*pixel) {
                        dst = (uint8_t *)pixel, d0 = _mm_set_ps(float(dst[3]), float(dst[2]), float(dst[1]), float(dst[0]));
                        m0 = _mm_add_ps(m0, _mm_mul_ps(d0, _mm_sub_ps(_mm_set1_ps(1.f), a0)));
                    }
                    a32 = _mm_cvttps_epi32(m0);
                    a16 = _mm_packs_epi32(a32, a32);
                    a8 = _mm_packus_epi16(a16, a16);
                    *pixel = _mm_cvtsi128_si32(a8);
#else
                    if (*pixel == 0)
                        *dst++ = src0 * alpha, *dst++ = src1 * alpha, *dst++ = src2 * alpha, *dst++ = 255.f * alpha;
                    else {
                        *dst = *dst * (1.f - alpha) + src0 * alpha, dst++;
                        *dst = *dst * (1.f - alpha) + src1 * alpha, dst++;
                        *dst = *dst * (1.f - alpha) + src2 * alpha, dst++;
                        *dst = *dst * (1.f - alpha) + 255.f * alpha, dst++;
                    }
#endif
                }
                msk++, pixel++;
            }
            pixelAddress -= rowBytes / 4;
            mask += maskRowBytes;
        }
    }
    
    static inline void prefixSum(short *counts, short n) {
#ifdef RASTERIZER_SIMD
        __m128i shuffle_mask = _mm_set1_epi32(0x0F0E0F0E);
        __m128i sum8, c8, *src8, *end8;
        for (sum8 = _mm_setzero_si128(), src8 = (__m128i *)counts, end8 = src8 + (n + 7) / 8; src8 < end8; src8++) {
            c8 = _mm_loadu_si128(src8);
            c8 = _mm_add_epi16(c8, _mm_slli_si128(c8, 2)), c8 = _mm_add_epi16(c8, _mm_slli_si128(c8, 4)), c8 = _mm_add_epi16(c8, _mm_slli_si128(c8, 8));
            c8 = _mm_add_epi16(c8, sum8);
            _mm_storeu_si128(src8, c8);
            sum8 = _mm_shuffle_epi8(c8, shuffle_mask);
        }
#else
        short *src, *dst, i;
        for (src = counts, dst = src + 1, i = 1; i < n; i++)
            *dst++ += *src++;
#endif
    }
    static void radixSort(uint32_t *in, int n, short *counts0, short *counts1, uint32_t *out) {
        uint32_t x;
        memset(counts0, 0, sizeof(short) * 256);
        int i;
        for (i = 0; i < n; i++)
            counts0[in[i] & 0xFF]++;
        prefixSum(counts0, 256);
        memset(counts1, 0, sizeof(short) * 256);
        for (i = n - 1; i >= 0; i--) {
            x = in[i];
            out[--counts0[x & 0xFF]] = x;
            counts1[(x >> 8) & 0xFF]++;
        }
        prefixSum(counts1, 256);
        for (i = n - 1; i >= 0; i--) {
            x = out[i];
            in[--counts1[(x >> 8) & 0xFF]] = x;
        }
    }
    static void writeScanlinesToSpans(std::vector<Scanline>& scanlines, Bounds clipped, std::vector<Spanline>& spanlines, bool writeSpans) {
        const float scale = 255.5f / 32767.f;
        float x, y, ix, cover, alpha;
        uint8_t a;
        short counts0[256], counts1[256];
        Scanline *scanline = & scanlines[clipped.ly];
        Spanline *spanline = & spanlines[clipped.ly];
        Delta *begin, *end, *delta;
        for (y = clipped.ly; y < clipped.uy; y++, scanline++, spanline++) {
            if (scanline->idx == 0)
                continue;
            begin = & scanline->elems[0], end = begin + scanline->idx;
            if (scanline->idx > 32) {
                uint32_t mem0[scanline->idx];
                radixSort((uint32_t *)begin, int(scanline->idx), counts0, counts1, mem0);
            } else
                std::sort(begin, end);
            if (writeSpans) {
                for (cover = 0, delta = begin, x = begin->x; delta < end; delta++) {
                    if (delta->x != x) {
                        alpha = fabsf(cover), a = alpha < 255.f ? alpha : 255.f;
                        if (a > 254)
                            new (spanline->alloc()) Span(x, delta->x - x);
                        else if (a > 0)
                            for (ix = x; ix < delta->x; ix++)
                                new (spanline->alloc()) Span(ix, -a);
                        x = delta->x;
                    }
                    cover += float(delta->delta) * scale;
                }
                scanline->empty();
            }
        }
    }
    static void writeSpansToBitmap(std::vector<Spanline>& spanlines, Bounds clipped, uint32_t color, Bitmap bitmap) {
        float y, lx, ux, src0, src1, src2, src3, srcAlpha, alpha;
        uint8_t *components = (uint8_t *) & color, *dst;
        src0 = components[0], src1 = components[1], src2 = components[2], src3 = components[3];
        srcAlpha = src3 * 0.003921568627f;
        uint32_t *pixel, *last;
        Spanline *spanline = & spanlines[clipped.ly];
        Span *span, *end;
        for (y = clipped.ly; y < clipped.uy; y++, spanline->empty(), spanline++) {
            for (span = & spanline->elems[0], end = & spanline->elems[spanline->idx]; span < end; span++) {
                lx = span->x, ux = lx + (span->w > 0 ? span->w : 1);
                lx = lx < clipped.lx ? clipped.lx : lx > clipped.ux ? clipped.ux : lx;
                ux = ux < clipped.lx ? clipped.lx : ux > clipped.ux ? clipped.ux : ux;
                if (lx != ux) {
                    pixel = bitmap.pixelAddress(lx, y);
                    if (span->w > 0 && src3 == 255)
                        memset_pattern4(pixel, & color, (ux - lx) * bitmap.bytespp);
                    else {
                        if (span->w > 0)
                            last = pixel + size_t(ux - lx), alpha = srcAlpha;
                        else
                            last = pixel + 1, alpha = float(-span->w) * 0.003921568627f * srcAlpha;
                        for (; pixel < last; pixel++) {
                            dst = (uint8_t *)pixel;
                            if (*pixel == 0)
                                *dst++ = src0 * alpha, *dst++ = src1 * alpha, *dst++ = src2 * alpha, *dst++ = 255.f * alpha;
                            else {
                                *dst = *dst * (1.f - alpha) + src0 * alpha, dst++;
                                *dst = *dst * (1.f - alpha) + src1 * alpha, dst++;
                                *dst = *dst * (1.f - alpha) + src2 * alpha, dst++;
                                *dst = *dst * (1.f - alpha) + 255.f * alpha, dst++;
                            }
                        }
                    }
                }
            }
        }
    }
};
