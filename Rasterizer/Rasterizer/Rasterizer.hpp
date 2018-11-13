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
    
    template<typename T>
    static inline T lerp(T n0, T n1, T t) {
        return n0 + t * (n1 - n0);
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
                          
    template<typename T, typename C>
    static void radixSort(T *in, int n, C *counts0, C *counts1, T *out) {
        T x;
        memset(counts0, 0, sizeof(C) * 256);
        int i;
        for (i = 0; i < n; i++)
            counts0[in[i] & 0xFF]++;
        prefixSum(counts0, 256);
        memset(counts1, 0, sizeof(C) * 256);
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
    struct Scene {
        void empty() { ctms.resize(0), paths.resize(0), bgras.resize(0); }
        std::vector<uint32_t> bgras;
        std::vector<AffineTransform> ctms;
        std::vector<Path> paths;
    };
    
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
#endif
        while (w--) {
            cover += *deltas, *deltas++ = 0;
            alpha = fabsf(cover);
            *mask++ = alpha < 255.f ? alpha : 255.f;
        }
    }
    
    static void writeDeltasToMask(float *deltas, Bounds device, uint8_t *mask) {
        size_t w = device.ux - device.lx, h = device.uy - device.ly;
        for (size_t y = 0; y < h; y++, deltas += w, mask += w)
            writeMaskRow(deltas, w, mask);
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
        scanline = device.isZero() ? & scanlines[clipped.ly] : & scanlines[0];
        size_t count = device.isZero() ? clipped.uy - clipped.ly : device.uy - device.ly;
        for (; count--; scanline++)
            scanline->empty();
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
    
    static void writeMaskToBitmap(uint8_t *mask, Bounds device, Bounds clipped, uint32_t bgra, Bitmap bitmap) {
        size_t maskRowBytes = device.ux - device.lx;
        size_t offset = maskRowBytes * (clipped.ly - device.ly) + (clipped.lx - device.lx);
        size_t w = clipped.ux - clipped.lx, h = clipped.uy - clipped.ly;
        writeMaskToBitmap(mask + offset, maskRowBytes, w, h, bgra, bitmap.pixelAddress(clipped.lx, clipped.ly), bitmap.rowBytes);
    }
    
    static void writeSpansToBitmap(std::vector<Spanline>& spanlines, Bounds device, Bounds clipped, uint32_t color, Bitmap bitmap) {
        float y, lx, ux, src0, src1, src2, src3, srcAlpha, alpha;
        uint8_t *components = (uint8_t *) & color, *dst;
        src0 = components[0], src1 = components[1], src2 = components[2], src3 = components[3];
        srcAlpha = src3 * 0.003921568627f;
        uint32_t *pixel, *last;
        Spanline *spanline = & spanlines[clipped.ly - device.ly];
        Spanline::Span *span, *end;
        for (y = clipped.ly; y < clipped.uy; y++, spanline->empty(), spanline++) {
            for (span = & spanline->spans[0], end = & spanline->spans[spanline->idx]; span < end; span++) {
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
    
    static void writePathToBitmap(Path& path, Bounds bounds, AffineTransform ctm, uint32_t bgra, Context& context) {
        Bounds dev = bounds.transform(ctm);
        Bounds device = dev.integral();
        Bounds clipped = device.intersected(context.clipBounds);
        float w, h, cw, ch, elx, ely, eux, euy, sx, sy;
        if (!clipped.isZero()) {
            w = device.ux - device.lx, h = device.uy - device.ly;
            cw = context.clipBounds.ux - context.clipBounds.lx, ch = context.clipBounds.uy - context.clipBounds.ly;
            if (w < cw && h < ch) {
                elx = dev.lx - device.lx, elx = elx < kFloatOffset ? kFloatOffset : 0;
                eux = device.ux - dev.ux, eux = eux < kFloatOffset ? kFloatOffset : 0;
                ely = dev.ly - device.ly, ely = ely < kFloatOffset ? kFloatOffset : 0;
                euy = device.uy - dev.uy, euy = euy < kFloatOffset ? kFloatOffset : 0;
                sx = (w - elx - eux) / w, sy = (h - ely - euy) / h;
                AffineTransform bias(sx, 0, 0, sy, elx, ely);
                AffineTransform deltasCTM = bias.concat(AffineTransform(ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx - device.lx, ctm.ty - device.ly));
                if (w * h < kDeltasDimension * kDeltasDimension) {
                    writePathToDeltasOrScanlines(path, deltasCTM, context.deltas, w, nullptr, context.clipBounds);
                    writeDeltasToMask(context.deltas, device, context.mask);
                    writeMaskToBitmap(context.mask, device, clipped, bgra, context.bitmap);
                } else {
                    writePathToDeltasOrScanlines(path, deltasCTM, nullptr, 0, & context.scanlines[0], context.clipBounds);
                    writeScanlinesToSpans(context.scanlines, device, clipped, context.spanlines);
                    writeSpansToBitmap(context.spanlines, device, clipped, bgra, context.bitmap);
                }
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
        if (y0 == y1)
            return;
        if (x0 >= clipBounds.lx && x0 < clipBounds.ux && x1 >= clipBounds.lx && x1 < clipBounds.ux
            && y0 >= clipBounds.ly && y0 < clipBounds.uy && y1 >= clipBounds.ly && y1 < clipBounds.uy) {
            writeSegmentToDeltasOrScanlines(x0, y0, x1, y1, 32767.f, nullptr, 0, scanlines);
        } else {
            float sx0, sy0, sx1, sy1, ty0, ty1, tx0, tx1, t0, t1, mx;
            int i;
            sy0 = y0 < clipBounds.ly ? clipBounds.ly : y0 > clipBounds.uy ? clipBounds.uy : y0;
            sy1 = y1 < clipBounds.ly ? clipBounds.ly : y1 > clipBounds.uy ? clipBounds.uy : y1;
            if (sy0 != sy1) {
                if (x0 == x1) {
                    sx0 = sx1 = x1 < clipBounds.lx ? clipBounds.lx : x1 > clipBounds.ux ? clipBounds.ux : x1;
                    writeVerticalSegmentToScanlines(sx0, sy0, sy1, scanlines);
                } else {
                    ty0 = (sy0 - y0) / (y1 - y0);
                    ty1 = (sy1 - y0) / (y1 - y0);
                    tx0 = (clipBounds.lx - x0) / (x1 - x0);
                    tx1 = (clipBounds.ux - x0) / (x1 - x0);
                    tx0 = tx0 < ty0 ? ty0 : tx0 > ty1 ? ty1 : tx0;
                    tx1 = tx1 < ty0 ? ty0 : tx1 > ty1 ? ty1 : tx1;
                    float ts[4] = { ty0, tx0 < tx1 ? tx0 : tx1, tx0 > tx1 ? tx0 : tx1, ty1 };
                    for (i = 0; i < 3; i++) {
                        t0 = ts[i], t1 = ts[i + 1];
                        if (t0 != t1) {
                            sy0 = lerp(y0, y1, t0), sy1 = lerp(y0, y1, t1);
                            sy0 = sy0 < clipBounds.ly ? clipBounds.ly : sy0 > clipBounds.uy ? clipBounds.uy : sy0;
                            sy1 = sy1 < clipBounds.ly ? clipBounds.ly : sy1 > clipBounds.uy ? clipBounds.uy : sy1;
                            mx = lerp(x0, x1, (t0 + t1) * 0.5f);
                            if (mx >= clipBounds.lx && mx < clipBounds.ux) {
                                sx0 = lerp(x0, x1, t0), sx1 = lerp(x0, x1, t1);
                                sx0 = sx0 < clipBounds.lx ? clipBounds.lx : sx0 > clipBounds.ux ? clipBounds.ux : sx0;
                                sx1 = sx1 < clipBounds.lx ? clipBounds.lx : sx1 > clipBounds.ux ? clipBounds.ux : sx1;
                                writeSegmentToDeltasOrScanlines(sx0, sy0, sx1, sy1, 32767.f, nullptr, 0, scanlines);
                            } else
                                writeVerticalSegmentToScanlines(mx < clipBounds.lx ? clipBounds.lx : clipBounds.ux, sy0, sy1, scanlines);
                        }
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
    static void writeClippedQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, float t0, float t1, Bounds clipBounds, float *q) {
        float tx0, ty0, tx2, ty2, tx1, ty1, t, x01, x12, x012, y01, y12, y012, tx01, tx12, ty01, ty12;
        x01 = lerp(x0, x1, t1), x12 = lerp(x1, x2, t1), x012 = lerp(x01, x12, t1);
        y01 = lerp(y0, y1, t1), y12 = lerp(y1, y2, t1), y012 = lerp(y01, y12, t1);
        t = t0 / t1;
        tx01 = lerp(x0, x01, t), tx12 = lerp(x01, x012, t), tx0 = lerp(tx01, tx12, t), tx1 = tx12, tx2 = x012;
        ty01 = lerp(y0, y01, t), ty12 = lerp(y01, y012, t), ty0 = lerp(ty01, ty12, t), ty1 = ty12, ty2 = y012;
        
        tx0 = tx0 < clipBounds.lx ? clipBounds.lx : tx0 > clipBounds.ux ? clipBounds.ux : tx0;
        tx2 = tx2 < clipBounds.lx ? clipBounds.lx : tx2 > clipBounds.ux ? clipBounds.ux : tx2;
        ty0 = ty0 < clipBounds.ly ? clipBounds.ly : ty0 > clipBounds.uy ? clipBounds.uy : ty0;
        ty2 = ty2 < clipBounds.ly ? clipBounds.ly : ty2 > clipBounds.uy ? clipBounds.uy : ty2;
        
        *q++ = tx0, *q++ = ty0, *q++ = tx1, *q++ = ty1, *q++ = tx2, *q++ = ty2;
    }
    static void writeClippedQuadraticToScanlines(float x0, float y0, float x1, float y1, float x2, float y2, Bounds clipBounds, Scanline *scanlines) {
        float lx, ly, ux, uy, cly, cuy, A, B, ts[8], q[6], t, s, t0, t1, x, y, vx;
        size_t i;
        bool visible;
        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2;
        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2;
        cly = ly < clipBounds.ly ? clipBounds.ly : ly > clipBounds.uy ? clipBounds.uy : ly;
        cuy = uy < clipBounds.ly ? clipBounds.ly : uy > clipBounds.uy ? clipBounds.uy : uy;
        if (cly != cuy) {
            lx = x0 < x1 ? x0 : x1, lx = lx < x2 ? lx : x2;
            ux = x0 > x1 ? x0 : x1, ux = ux > x2 ? ux : x2;
            if (lx < clipBounds.lx || ux > clipBounds.ux || ly < clipBounds.ly || uy > clipBounds.uy) {
                A = y0 + y2 - y1 - y1, B = 2.f * (y1 - y0);
                solveQuadratic(A, B, y0 - clipBounds.ly, ts[0], ts[1]);
                solveQuadratic(A, B, y0 - clipBounds.uy, ts[2], ts[3]);
                A = x0 + x2 - x1 - x1, B = 2.f * (x1 - x0);
                solveQuadratic(A, B, x0 - clipBounds.lx, ts[4], ts[5]);
                solveQuadratic(A, B, x0 - clipBounds.ux, ts[6], ts[7]);
                std::sort(& ts[0], & ts[8]);
                for (i = 0; i < 7; i++) {
                    t0 = ts[i], t1 = ts[i + 1];
                    if (t0 != t1) {
                        t = (t0 + t1) * 0.5f, s = 1.f - t;
                        y = y0 * s * s + y1 * 2.f * s * t + y2 * t * t;
                        if (y >= clipBounds.ly && y < clipBounds.uy) {
                            x = x0 * s * s + x1 * 2.f * s * t + x2 * t * t;
                            visible = x >= clipBounds.lx && x < clipBounds.ux;
                            writeClippedQuadratic(x0, y0, x1, y1, x2, y2, t0, t1, clipBounds, q);
                            if (visible) {
                                if (fabsf(t1 - t0) < 1e-2) {
                                    writeSegmentToDeltasOrScanlines(q[0], q[1], x, y, 32767.f, nullptr, 0, scanlines);
                                    writeSegmentToDeltasOrScanlines(x, y, q[4], q[5], 32767.f, nullptr, 0, scanlines);
                                } else
                                    writeQuadraticToDeltasOrScanlines(q[0], q[1], q[2], q[3], q[4], q[5], 32767.f, nullptr, 0, scanlines);
                            } else {
                                vx = x <= clipBounds.lx ? clipBounds.lx : clipBounds.ux;
                                writeVerticalSegmentToScanlines(vx, q[1], q[5], scanlines);
                            }
                        }
                    }
                }
            } else
                writeQuadraticToDeltasOrScanlines(x0, y0, x1, y1, x2, y2, 32767.f, nullptr, 0, scanlines);
        }
    }
    static void solveCubic(double a, double b, double c, double d, float& t0, float& t1, float& t2) {
        const double limit = 1e-3;
        double a3, p, q, q2, u1, v1, p3, discriminant, mp3, mp33, r, t, cosphi, phi, crtr, sd;
        if (fabs(d) < limit) {
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
    static void writeClippedCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float t0, float t1, Bounds clipBounds, float *cubic) {
        float t, x01, x12, x23, x012, x123, x0123, y01, y12, y23, y012, y123, y0123;
        float tx01, tx12, tx23, tx012, tx123, tx0123, ty01, ty12, ty23, ty012, ty123, ty0123;
        float tx0, ty0, tx1, ty1, tx2, ty2, tx3, ty3;
        x01 = lerp(x0, x1, t1), x12 = lerp(x1, x2, t1), x23 = lerp(x2, x3, t1);
        x012 = lerp(x01, x12, t1), x123 = lerp(x12, x23, t1);
        x0123 = lerp(x012, x123, t1);
        y01 = lerp(y0, y1, t1), y12 = lerp(y1, y2, t1), y23 = lerp(y2, y3, t1);
        y012 = lerp(y01, y12, t1), y123 = lerp(y12, y23, t1);
        y0123 = lerp(y012, y123, t1);
        t = t0 / t1;
        tx01 = lerp(x0, x01, t), tx12 = lerp(x01, x012, t), tx23 = lerp(x012, x0123, t);
        tx012 = lerp(tx01, tx12, t), tx123 = lerp(tx12, tx23, t);
        tx0123 = lerp(tx012, tx123, t);
        ty01 = lerp(y0, y01, t), ty12 = lerp(y01, y012, t), ty23 = lerp(y012, y0123, t);
        ty012 = lerp(ty01, ty12, t), ty123 = lerp(ty12, ty23, t);
        ty0123 = lerp(ty012, ty123, t);
        tx0 = tx0123, tx1 = tx123, tx2 = tx23, tx3 = x0123;
        ty0 = ty0123, ty1 = ty123, ty2 = ty23, ty3 = y0123;

        tx0 = tx0 < clipBounds.lx ? clipBounds.lx : tx0 > clipBounds.ux ? clipBounds.ux : tx0;
        ty0 = ty0 < clipBounds.ly ? clipBounds.ly : ty0 > clipBounds.uy ? clipBounds.uy : ty0;
        tx3 = tx3 < clipBounds.lx ? clipBounds.lx : tx3 > clipBounds.ux ? clipBounds.ux : tx3;
        ty3 = ty3 < clipBounds.ly ? clipBounds.ly : ty3 > clipBounds.uy ? clipBounds.uy : ty3;
        
        *cubic++ = tx0, *cubic++ = ty0, *cubic++ = tx1, *cubic++ = ty1, *cubic++ = tx2, *cubic++ = ty2, *cubic++ = tx3, *cubic++ = ty3;
    }
    static void writeClippedCubicToScanlines(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Bounds clipBounds, Scanline *scanlines) {
        float lx, ly, ux, uy, cx, bx, ax, cy, by, ay, s, t, cly, cuy, ts[12], t0, t1, w0, w1, w2, w3, x, y, cubic[8], vx;
        float A, B, C, D;
        size_t i;
        bool visible;
        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2, ly = ly < y3 ? ly : y3;
        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2, uy = uy > y3 ? uy : y3;
        cly = ly < clipBounds.ly ? clipBounds.ly : ly > clipBounds.uy ? clipBounds.uy : ly;
        cuy = uy < clipBounds.ly ? clipBounds.ly : uy > clipBounds.uy ? clipBounds.uy : uy;
        if (cly != cuy) {
            cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
            cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
            s = fabsf(ax) + fabsf(bx), t = fabsf(ay) + fabsf(by);
            if (s * s + t * t < 0.1f) {
                writeClippedSegmentToScanlines(x0, y0, x3, y3, clipBounds, scanlines);
            } else {
                lx = x0 < x1 ? x0 : x1, lx = lx < x2 ? lx : x2, lx = lx < x3 ? lx : x3;
                ux = x0 > x1 ? x0 : x1, ux = ux > x2 ? ux : x2, ux = ux > x3 ? ux : x3;
                if (lx < clipBounds.lx || ux > clipBounds.ux || ly < clipBounds.ly || uy > clipBounds.uy) {
                    A = by, B = cy, C = y0, D = ay;
                    solveCubic(A, B, C - clipBounds.ly, D, ts[0], ts[1], ts[2]);
                    solveCubic(A, B, C - clipBounds.uy, D, ts[3], ts[4], ts[5]);
                    A = bx, B = cx, C = x0, D = ax;
                    solveCubic(A, B, C - clipBounds.lx, D, ts[6], ts[7], ts[8]);
                    solveCubic(A, B, C - clipBounds.ux, D, ts[9], ts[10], ts[11]);
                    std::sort(& ts[0], & ts[12]);
                    for (i = 0; i < 11; i++) {
                        t0 = ts[i], t1 = ts[i + 1];
                        if (t0 != t1) {
                            t = (t0 + t1) * 0.5f, s = 1.f - t;
                            w0 = s * s * s, w1 = 3.f * s * s * t, w2 = 3.f * s * t * t, w3 = t * t * t;
                            y = y0 * w0 + y1 * w1 + y2 * w2 + y3 * w3;
                            if (y >= clipBounds.ly && y < clipBounds.uy) {
                                x = x0 * w0 + x1 * w1 + x2 * w2 + x3 * w3;
                                visible = x >= clipBounds.lx && x < clipBounds.ux;
                                writeClippedCubic(x0, y0, x1, y1, x2, y2, x3, y3, t0, t1, clipBounds, cubic);
                                if (visible) {
                                    if (fabsf(t1 - t0) < 1e-2) {
                                        writeSegmentToDeltasOrScanlines(cubic[0], cubic[1], x, y, 32767.f, nullptr, 0, scanlines);
                                        writeSegmentToDeltasOrScanlines(x, y, cubic[6], cubic[7], 32767.f, nullptr, 0, scanlines);
                                    } else
                                        writeCubicToDeltasOrScanlines(cubic[0], cubic[1], cubic[2], cubic[3], cubic[4], cubic[5], cubic[6], cubic[7], 32767.f, nullptr, 0, scanlines);
                                } else {
                                    vx = x <= clipBounds.lx ? clipBounds.lx : clipBounds.ux;
                                    writeVerticalSegmentToScanlines(vx, cubic[1], cubic[7], scanlines);
                                }
                            }
                        }
                    }
                } else
                    writeCubicToDeltasOrScanlines(x0, y0, x1, y1, x2, y2, x3, y3, 32767.f, nullptr, 0, scanlines);
            }
        }
    }
    
    static void writePathToDeltasOrScanlines(Path& path, AffineTransform ctm, float *deltas, size_t stride, Scanline *scanlines, Bounds clipBounds) {
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
            if (x == 0)
                scanline->delta0 += 255.5f * (sy1 - sy0);
            else {
                cover = (sy1 - sy0) * 32767.f;
                scanline->insertDelta(ix0, cover * area);
                if (area < 1.f)
                    scanline->insertDelta(ix1, cover * (1.f - area));
            }
        }
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
                    scanline->insertDelta(ix0, cover * area);
                    scanline->insertDelta(ix1, cover * (1.f - area));
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
                        scanline->insertDelta(ix0, cover * area + last);
                    else
                        *delta += cover * area + last;
                    last = cover * (1.f - area);
                }
                if (scanlines)
                    scanline->insertDelta(ix0, last);
                else {
                    if (ix0 < stride)
                        *delta += last;
                }
            }
        }
    }
};
