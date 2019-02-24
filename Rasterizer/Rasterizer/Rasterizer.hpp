//
//  Rasterizer.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 17/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#ifdef RASTERIZER_SIMD
typedef float vec4f __attribute__((vector_size(16)));
typedef uint8_t vec4b __attribute__((vector_size(4)));
#endif

#import "Rasterizer.h"
#import <vector>
#import <unordered_map>
#pragma clang diagnostic ignored "-Wcomma"

struct Rasterizer {
    struct AffineTransform {
        AffineTransform() {}
        AffineTransform(float a, float b, float c, float d, float tx, float ty) : a(a), b(b), c(c), d(d), tx(tx), ty(ty) {}
        inline AffineTransform concat(AffineTransform t) const {
            return {
                t.a * a + t.b * c, t.a * b + t.b * d,
                t.c * a + t.d * c, t.c * b + t.d * d,
                t.tx * a + t.ty * c + tx, t.tx * b + t.ty * d + ty
            };
        }
        AffineTransform invert() const {
            float det = a * d - b * c, recip = 1.f / det;
            if (det == 0.f)
                return *this;
            return {
                d * recip,                      -b * recip,
                -c * recip,                     a * recip,
                (c * ty - d * tx) * recip,      -(a * ty - b * tx) * recip
            };
        }
        float a, b, c, d, tx, ty;
    };
    struct Bounds {
        Bounds() {}
        Bounds(float lx, float ly, float ux, float uy) : lx(lx), ly(ly), ux(ux), uy(uy) {}
        Bounds(AffineTransform t) {
            lx = t.tx + (t.a < 0.f ? t.a : 0.f) + (t.c < 0.f ? t.c : 0.f), ly = t.ty + (t.b < 0.f ? t.b : 0.f) + (t.d < 0.f ? t.d : 0.f);
            ux = t.tx + (t.a > 0.f ? t.a : 0.f) + (t.c > 0.f ? t.c : 0.f), uy = t.ty + (t.b > 0.f ? t.b : 0.f) + (t.d > 0.f ? t.d : 0.f);
        }
        Bounds integral() const { return { floorf(lx), floorf(ly), ceilf(ux), ceilf(uy) }; }
        Bounds intersect(Bounds other) const {
            return {
                lx < other.lx ? other.lx : lx > other.ux ? other.ux : lx, ly < other.ly ? other.ly : ly > other.uy ? other.uy : ly,
                ux < other.lx ? other.lx : ux > other.ux ? other.ux : ux, uy < other.ly ? other.ly : uy > other.uy ? other.uy : uy
            };
        }
        inline Bounds transform(AffineTransform ctm) const {
            return Bounds(unit(ctm));
        }
        inline AffineTransform unit(AffineTransform t) const {
            return { t.a * (ux - lx), t.b * (ux - lx), t.c * (uy - ly), t.d * (uy - ly), lx * t.a + ly * t.c + t.tx, lx * t.b + ly * t.d + t.ty };
        }
        float lx, ly, ux, uy;
    };
    struct Colorant {
        Colorant() {}
        Colorant(uint8_t *src) : src0(src[0]), src1(src[1]), src2(src[2]), src3(src[3]), ctm(0.f, 0.f, 0.f, 0.f, 0.f, 0.f)  {}
        Colorant(uint8_t *src, AffineTransform ctm) : src0(src[0]), src1(src[1]), src2(src[2]), src3(src[3]), ctm(ctm) {}
        uint8_t src0, src1, src2, src3;
        AffineTransform ctm;
    };
    struct Sequence {
        struct Atom {
            enum Type { kNull = 0, kMove, kLine, kQuadratic, kCubic, kClose, kCapacity = 15 };
            Atom() { bzero(points, sizeof(points)), bzero(types, sizeof(types)); }
            float       points[30];
            uint8_t     types[8];
        };
        Sequence() : end(Atom::kCapacity), shapesCount(0), px(0), py(0), bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX), shapes(nullptr), circles(nullptr), refCount(0), hash(0) {}
        ~Sequence() { if (shapes) free(shapes), free(circles); }
        
        float *alloc(Atom::Type type, size_t size) {
            if (end + size > Atom::kCapacity)
                end = 0, atoms.emplace_back();
            atoms.back().types[end / 2] |= (uint8_t(type) << ((end & 1) * 4));
            end += size;
            return atoms.back().points + (end - size) * 2;
        }
        void addShapes(size_t count) {
            shapesCount = count, bounds = Bounds(-5e11f, -5e11f, 5e11f, 5e11f);
            shapes = (AffineTransform *)calloc(count, sizeof(shapes[0])), circles = (bool *)calloc(count, sizeof(bool));
        }
        void addBounds(Bounds b) { moveTo(b.lx, b.ly), lineTo(b.ux, b.ly), lineTo(b.ux, b.uy), lineTo(b.lx, b.uy), close(); }
        
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
            if (fabsf(det) < 1e-4f) {
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
            if (dx * dx + dy * dy < 1e-4f)
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
        size_t refCount, end, shapesCount, hash;
        std::vector<Atom> atoms;
        float px, py;
        AffineTransform *shapes;
        bool *circles;
        Bounds bounds;
    };
    template<typename T>
    struct Ref {
        Ref()                               { ref = new T(), ref->refCount = 1; }
        ~Ref()                              { if (--(ref->refCount) == 0) delete ref; }
        Ref(const Ref& other)               { assign(other); }
        Ref& operator= (const Ref other)    { assign(other); return *this; }
        void assign(const Ref& other) {
            if (this != & other) {
                if (ref)
                    this->~Ref();
                ref = other.ref, ref->refCount++;
            }
        }
        T *ref = nullptr;
    };
    typedef Ref<Sequence> Path;
    
    struct Bitmap {
        Bitmap() : data(nullptr), width(0), height(0), stride(0), bpp(0), bytespp(0) {}
        Bitmap(void *data, size_t width, size_t height, size_t stride, size_t bpp)
        : data((uint8_t *)data), width(width), height(height), stride(stride), bpp(bpp), bytespp(bpp / 8) {}
        void clear(uint8_t *src) { memset_pattern4(data, src, stride * height); }
        inline uint8_t *pixelAddress(short x, short y) { return data + stride * (height - 1 - y) + x * bytespp; }
        uint8_t *data;
        size_t width, height, stride, bpp, bytespp;
    };
    struct Segment {
        Segment(float x0, float y0, float x1, float y1) : x0(x0), y0(y0), x1(x1), y1(y1) {}
        struct Index {
            Index(short x, short i) : x(x), i(i) {}
            short x, i;
            inline bool operator< (const Index& other) const { return x < other.x; }
        };
        float x0, y0, x1, y1;
    };
    template<typename T>
    struct Memory {
        Memory() : refCount(0), size(16), addr((T *)malloc(size * sizeof(T))) {}
        ~Memory() { free(addr); }
        void resize(size_t n) { size = n, addr = (T *)realloc(addr, size * sizeof(T)); }
        size_t refCount, size;
        T *addr;
    };
    template<typename T>
    struct Row {
        Row() : end(0), size(memory.ref->size), idx(0), base(memory.ref->addr) {}
        void empty() { end = idx = 0; }
        inline T *alloc(size_t n) {
            size_t i = end;
            end += n;
            if (size < end)
                size = end * 1.5, memory.ref->resize(size), base = memory.ref->addr;
            return base + i;
        }
        Ref<Memory<T>> memory;
        size_t end, size, idx;
        T *base;
    };
    template<typename T>
    struct Pages {
        const size_t kPageSize = 4096;
        Pages() : size(0), base(nullptr) {}
        ~Pages() { if (base) free(base); }
        void reset() { size = 0, ~Pages(), base = nullptr; }
        T *alloc(size_t n) {
            if (size < n) {
                size = (n * sizeof(T) + kPageSize - 1) / kPageSize * kPageSize;
                if (base)
                    free(base);
                posix_memalign((void **)& base, kPageSize, size);
            }
            return base;
        }
        size_t size;
        T *base;
    };
    struct GPU {
        struct Allocator {
            void init(size_t w, size_t h) {
                width = w, height = h, sheet = strip = fast = Bounds(0.f, 0.f, 0.f, 0.f);
            }
            void alloc(float w, float h, float& ox, float& oy) {
                h = h > Context::kfh ? kFastHeight : Context::kfh;
                Bounds& b = h > Context::kfh ? fast : strip;
                if (b.ux - b.lx < w) {
                    if (sheet.uy - sheet.ly < h)
                        sheet = Bounds(0.f, 0.f, width, height), strip = fast = Bounds(0.f, 0.f, 0.f, 0.f);
                    b.lx = sheet.lx, b.ly = sheet.ly, b.ux = sheet.ux, b.uy = sheet.ly + h, sheet.ly = b.uy;
                }
                ox = b.lx, b.lx += w, oy = b.ly;
            }
            size_t width, height;
            Bounds sheet, strip, fast;
        };
        struct Cell {
            Cell(float lx, float ly, float ux, float uy, float ox, float oy) : lx(lx), ly(ly), ux(ux), uy(uy), ox(ox), oy(oy) {}
            short lx, ly, ux, uy, ox, oy;
        };
        struct SuperCell {
            SuperCell(float lx, float ly, float ux, float uy, float ox, float oy, float cover, short iy, size_t idx, size_t begin, size_t count) : cell(lx, ly, ux, uy, ox, oy), cover(cover), iy(iy), count(short(count)), idx(uint32_t(idx)), begin(uint32_t(begin)) {}
            Cell cell;
            float cover;
            short iy, count;
            uint32_t idx, begin;
        };
        struct Outline {
            Outline(Segment *s, float width) : s(*s), width(width), prev(-1), next(1) {}
            Segment s;
            float width;
            short prev, next;
        };
        struct Quad {
            enum Type { kNull = 0, kRect, kCircle, kCell, kSolidCell, kShapes, kOutlines };
            Quad(float lx, float ly, float ux, float uy, float ox, float oy, size_t iz, int type, float cover, short iy, size_t idx, size_t begin, size_t end) : super(lx, ly, ux, uy, ox, oy, cover, iy, idx, begin, end), iz((uint32_t)iz | type << 24) {}
            Quad(AffineTransform unit, size_t iz, int type) : unit(unit), iz((uint32_t)iz | type << 24) {}
            Quad(Segment *s, float width, size_t iz, int type) : outline(s, width), iz((uint32_t)iz | type << 24) {}
            union {
                SuperCell super;
                AffineTransform unit;
                Outline outline;
            };
            uint32_t iz;
        };
        struct Edge {
            Cell cell;
            int iy, base;
            uint16_t i0, i1;
        };
        GPU() : edgeInstances(0), outlinesCount(0), shapesCount(0) {}
        void empty() {
            edgeInstances = outlinesCount = shapesCount = 0, indices.empty(), quads.empty(), opaques.empty();
        }
        size_t width, height, edgeInstances, outlinesCount, shapesCount;
        Allocator allocator;
        Row<Segment::Index> indices;
        Row<Quad> quads, opaques;
    };
    struct Buffer {
        struct Entry {
            enum Type { kColorants, kEdges, kQuads, kOpaques, kShapes, kSegments };
            Entry(Type type, size_t begin, size_t end) : type(type), begin(begin), end(end) {}
            Type type;
            size_t begin, end;
        };
        Pages<uint8_t> data;
        Row<Entry> entries;
        Colorant clearColor;
    };
    struct Info {
        Info(float *deltas, uint32_t stride, Row<Segment> *segments) : deltas(deltas), stride(stride), segments(segments) {}
        float *deltas; uint32_t stride; Row<Segment> *segments;
    };
    typedef void (*Function)(float x0, float y0, float x1, float y1, Info *info);
    static void writeOutlineSegment(float x0, float y0, float x1, float y1, Info *info) {
        new (info->segments->alloc(1)) Segment(x0, y0, x1, y1);
    }
    static void writeClippedSegment(float x0, float y0, float x1, float y1, Info *info) {
        if (y0 == y1)
            return;
        float iy0 = y0 * Context::krfh, iy1 = y1 * Context::krfh;
        if (floorf(iy0) == floorf(iy1))
            new (info->segments[size_t(iy0)].alloc(1)) Segment(x0, y0, x1, y1);
        else {
            float sy1, dy, sx1, dx;
            int s, ds, count;
            if (y0 < y1)
                iy0 = floorf(iy0), iy1 = ceilf(iy1), sy1 = iy0 * Context::kfh, dy = Context::kfh, s = iy0, ds = 1;
            else
                iy0 = floorf(iy1), iy1 = ceilf(y0 * Context::krfh), sy1 = iy1 * Context::kfh, dy = -Context::kfh, s = iy1 - 1.f, ds = -1;
            count = iy1 - iy0;
            dx = dy / (y1 - y0) * (x1 - x0), sx1 = (sy1 - y0) / dy * dx + x0;
            while (--count) {
                sx1 += dx, sy1 += dy;
                new (info->segments[s].alloc(1)) Segment(x0, y0, sx1, sy1);
                x0 = sx1, y0 = sy1, s += ds;
            }
            new (info->segments[s].alloc(1)) Segment(x0, y0, x1, y1);
        }
    }
    static void writeDeltaSegment(float x0, float y0, float x1, float y1, Info *info) {
        if (y0 == y1)
            return;
        float scale = copysign(1.f, y1 - y0), tmp, dx, dy, iy0, iy1, sx0, sy0, dxdy, dydx, sx1, sy1, lx, ux, ix0, ix1, cx0, cy0, cx1, cy1, cover, area, last, *delta;
        if (scale < 0.f)
            tmp = x0, x0 = x1, x1 = tmp, tmp = y0, y0 = y1, y1 = tmp;
        dx = x1 - x0, dy = y1 - y0, dxdy = fabsf(dx) / (fabsf(dx) + 1e-4f) * dx / dy;
        for (sy0 = y0, sx0 = x0, iy0 = floorf(y0), delta = info->deltas + size_t(iy0 * info->stride); iy0 < y1; iy0 = iy1, sy0 = sy1, sx0 = sx1, delta += info->stride) {
            iy1 = iy0 + 1.f, sy1 = y1 > iy1 ? iy1 : y1, sx1 = x0 + (sy1 - y0) * dxdy;
            lx = sx0 < sx1 ? sx0 : sx1, ux = sx0 > sx1 ? sx0 : sx1;
            ix0 = floorf(lx), ix1 = ix0 + 1.f;
            if (lx >= ix0 && ux <= ix1) {
                cover = (sy1 - sy0) * scale, area = (ix1 - (ux + lx) * 0.5f);
                delta[int(ix0)] += cover * area;
                if (area < 1.f && ix1 < info->stride)
                    delta[int(ix1)] += cover * (1.f - area);
            } else {
                dydx = fabsf(dy / dx);
                for (last = 0.f, cx0 = lx, cy0 = sy0; ix0 <= ux; ix0 = ix1, cx0 = cx1, cy0 = cy1) {
                    ix1 = ix0 + 1.f, cx1 = ux < ix1 ? ux : ix1, cy1 = sy0 + (cx1 - lx) * dydx;
                    cover = (cy1 - cy0) * scale, area = (ix1 - (cx0 + cx1) * 0.5f);
                    delta[int(ix0)] += cover * area + last;
                    last = cover * (1.f - area);
                }
                if (ix0 < info->stride)
                    delta[int(ix0)] += last;
            }
        }
    }
    struct Cache {
        struct Entry {
            Entry() {}
            Entry(AffineTransform ctm) : ctm(ctm) {}
            Entry *writeOutlines(Path& path, AffineTransform ctm, Bounds clip, Info info) {
                begin = info.segments->idx;
                writePath(path, ctm, clip, writeOutlineSegment, info);
                info.segments->idx = end = info.segments->end;
                return this;
            }
            AffineTransform ctm;
            size_t begin, end;
        };
        void empty() { entries.clear(), ms.empty(), segments.empty(); }
        Entry *addPath(Path& path, AffineTransform ctm, AffineTransform unit, Bounds clip, Info info, AffineTransform& m) {
            Entry *e = nullptr;
            Bounds dev = Bounds(unit).integral();
            if (dev.lx == clip.lx && dev.ly == clip.ly && dev.ux == clip.ux && dev.uy == clip.uy) {
                if (path.ref->hash == 0)
                    e = outline.writeOutlines(path, ctm, clip, info);
                else {
                    auto it = entries.find(path.ref->hash);
                    if (it == entries.end()) {
                        e = & entries.emplace(path.ref->hash, Entry(ctm.invert())).first->second;
                        e->writeOutlines(path, ctm, clip, info);
                    } else {
                        m = ctm.concat(it->second.ctm);
                        if (m.a == m.d && m.b == -m.c && fabsf(m.a * m.a + m.b * m.b - 1.f) < 1e-6f)
                            e = & it->second;
                    }
                }
            }
            return e;
        }
        std::unordered_map<size_t, Entry> entries;
        Entry outline;
        Row<Segment> ms, segments;
    };
    
    struct Context {
        static AffineTransform nullclip() { return { 1e12f, 0.f, 0.f, 1e12f, -5e11f, -5e11f }; }
        
        static void writeContextToBuffer(Context *ctx,
                                           Path *paths,
                                           AffineTransform *ctms,
                                           Colorant *colorants,
                                           size_t begin,
                                           std::vector<Buffer::Entry>& entries,
                                           Buffer& buffer) {
            size_t end = begin, j, qend, q, jend, iz, sbegins[ctx->segments.size()], size;
            std::vector<size_t> idxes;
            
            size = ctx->cache.segments.end + ctx->cache.ms.end;
            for (j = 0; j < ctx->segments.size(); j++)
                sbegins[j] = size, size += ctx->segments[j].end;
            if (size) {
                Segment *dst = (Segment *)(buffer.data.base + begin);
                if (ctx->cache.ms.end)
                    memcpy(dst, ctx->cache.ms.base, ctx->cache.ms.end * sizeof(Segment));
                if (ctx->cache.segments.end)
                    memcpy(dst + ctx->cache.ms.end, ctx->cache.segments.base, ctx->cache.segments.end * sizeof(Segment));
                for (j = 0; j < ctx->segments.size(); j++)
                    memcpy(dst + sbegins[j], ctx->segments[j].base, ctx->segments[j].end * sizeof(Segment));
                end = begin + size * sizeof(Segment);
                entries.emplace_back(Buffer::Entry::kSegments, begin, end), idxes.emplace_back(0);
                begin = end;
            }
            while (ctx->gpu.quads.idx != ctx->gpu.quads.end) {
                GPU::Quad *quad = ctx->gpu.quads.base;
                for (qend = ctx->gpu.quads.idx + 1; qend < ctx->gpu.quads.end; qend++)
                    if (quad[qend].iz >> 24 == GPU::Quad::kCell && quad[qend].super.cell.oy == 0 && quad[qend].super.cell.ox == 0)
                        break;
                
                GPU::Edge *dst = (GPU::Edge *)(buffer.data.base + begin);
                Segment::Index *is;
                for (q = ctx->gpu.quads.idx, quad += q; q < qend; q++, quad++) {
                    if (quad->iz >> 24 == GPU::Quad::kCell) {
                        end += (quad->super.count + 1) / 2 * sizeof(GPU::Edge);
                        int base = quad->super.idx, iy = 0;
                        if (quad->super.iy < 0)
                            base += ctx->cache.ms.end, iy = quad->super.cover;
                        else
                            base += sbegins[quad->super.iy];
                        is = quad->super.begin == 0xFFFFFF ? nullptr : ctx->gpu.indices.base + quad->super.begin;
                        for (j = 0, jend = quad->super.count; j < jend; j++, dst++) {
                            dst->cell = quad->super.cell, dst->iy = iy, dst->base = base;
                            dst->i0 = uint16_t(is ? is++->i : j);
                            if (++j < jend)
                                dst->i1 = uint16_t(is ? is++->i : j);
                            else
                                dst->i1 = 0xFFFF;
                        }
                    }
                }
                if (begin != end)
                    entries.emplace_back(Buffer::Entry::kEdges, begin, end), idxes.emplace_back(0);
                begin = end;
                
                if (ctx->gpu.quads.idx != qend) {
                    quad = ctx->gpu.quads.base + ctx->gpu.quads.idx;
                    int qtype = quad[0].iz >> 24, type = qtype == GPU::Quad::kShapes || qtype == GPU::Quad::kOutlines ? Buffer::Entry::kShapes : Buffer::Entry::kQuads;
                    Buffer::Entry *entry;
                    entries.emplace_back(Buffer::Entry::Type(type), begin, begin), idxes.emplace_back(ctx->gpu.quads.idx), entry = & entries.back();
                    for (j = 0, jend = qend - ctx->gpu.quads.idx; j < jend; j++) {
                        qtype = quad[j].iz >> 24, type = qtype == GPU::Quad::kShapes || qtype == GPU::Quad::kOutlines ? Buffer::Entry::kShapes : Buffer::Entry::kQuads;
                        if (type != entry->type)
                            end = entry->end, entries.emplace_back(Buffer::Entry::Type(type), end, end), idxes.emplace_back(ctx->gpu.quads.idx + j), entry = & entries.back();
                           
                        if (qtype == GPU::Quad::kShapes) {
                            iz = quad[j].iz & 0xFFFFFF;
                            Path& path = paths[iz];
                            GPU::Quad *dst = (GPU::Quad *)(buffer.data.base + entry->end);
                            AffineTransform ctm = quad[j].unit;
                            for (int k = 0; k < path.ref->shapesCount; k++, dst++)
                                new (dst) GPU::Quad(ctm.concat(path.ref->shapes[k]), iz, path.ref->circles[k] ? GPU::Quad::kCircle : GPU::Quad::kRect);
                            entry->end += path.ref->shapesCount * sizeof(GPU::Quad);
                        } else if (qtype == GPU::Quad::kOutlines) {
                            iz = quad[j].iz & 0xFFFFFF;
                            Segment *s = ctx->segments[quad[j].super.iy].base + quad[j].super.idx, *es = s + quad[j].super.begin;
                            GPU::Quad *dst = (GPU::Quad *)(buffer.data.base + entry->end), *dst0;
                            for (dst0 = dst; s < es; s++, dst++) {
                                new (dst) GPU::Quad(s, quad[j].super.cover, iz, GPU::Quad::kOutlines);
                                if (s->x0 == FLT_MAX && dst - dst0 > 1)
                                    dst0->outline.prev = (int)(dst - dst0 - 1), (dst - 1)->outline.next = -dst0->outline.prev, dst0 = dst + 1;
                            }
                            entry->end += quad[j].super.begin * sizeof(GPU::Quad);
                        } else
                            entry->end += sizeof(GPU::Quad);
                    }
                    begin = end = entry->end;
                    ctx->gpu.quads.idx = qend;
                }
            }
            for (int k = 0; k < idxes.size(); k++) {
                if (entries[k].type == Buffer::Entry::kQuads)
                    memcpy(buffer.data.base + entries[k].begin, ctx->gpu.quads.base + idxes[k], entries[k].end - entries[k].begin);
            }
            ctx->gpu.empty();
            for (j = 0; j < ctx->segments.size(); j++)
                ctx->segments[j].empty();
        }
        static size_t writeContextsToBuffer(Context *contexts, size_t count,
                                          Path *paths,
                                          AffineTransform *ctms,
                                          Colorant *colorants,
                                          size_t pathsCount,
                                          size_t *begins,
                                          Buffer& buffer) {
            size_t size, sz, i, j, begin, end, idx;
            begin = 0, end = size = pathsCount * sizeof(Colorant);
            for (i = 0; i < count; i++)
                size += contexts[i].gpu.opaques.end * sizeof(GPU::Quad);
            for (i = 0; i < count; i++) {
                begins[i] = size;
                size += contexts[i].gpu.edgeInstances * sizeof(GPU::Edge) + (contexts[i].gpu.outlinesCount + contexts[i].gpu.shapesCount + contexts[i].gpu.quads.end) * sizeof(GPU::Quad) + (contexts[i].cache.segments.end + contexts[i].cache.ms.end) * sizeof(Segment);
                for (j = 0; j < contexts[i].segments.size(); j++)
                    size += contexts[i].segments[j].end * sizeof(Segment);
            }
            buffer.data.alloc(size);
            
            memcpy(buffer.data.base, colorants, end);
            new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::Entry::kColorants, begin, end);
            
            for (idx = begin = end, i = 0; i < count; i++)
                if ((sz = contexts[i].gpu.opaques.end * sizeof(GPU::Quad)))
                    memcpy(buffer.data.base + idx, contexts[i].gpu.opaques.base, sz), idx = end = end + sz;
            if (begin != end)
                new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::Entry::kOpaques, begin, end);
            return size;
        }
        Context() {}
        void setBitmap(Bitmap bm, Bounds cl) {
            bitmap = bm;
            device = Bounds(0.f, 0.f, bm.width, bm.height).intersect(cl.integral());
            size_t size = ceilf(float(bm.height) * krfh);
            if (segments.size() != size)
                segments.resize(size);
            deltas.resize((bm.width + 1) * kfh);
            memset(& deltas[0], 0, deltas.size() * sizeof(deltas[0]));
        }
        void setGPU(size_t width, size_t height) {
            bitmap = Bitmap();
            device = Bounds(0.f, 0.f, width, height);
            size_t size = ceilf(float(height) * krfh);
            if (segments.size() != size)
                segments.resize(size);
            gpu.width = width, gpu.height = height;
            gpu.allocator.init(width, height);
            cache.empty();
        }
        void drawPaths(Path *paths, AffineTransform *ctms, bool even, Colorant *colorants, float width, size_t begin, size_t end) {
            if (begin == end)
                return;
            size_t iz;
            bool *flags = (bool *)calloc(end - begin, sizeof(bool)), *flag = flags;
            AffineTransform *units = (AffineTransform *)malloc((end - begin) * sizeof(AffineTransform)), *un;
            Bounds *clus = (Bounds *)malloc((end - begin) * sizeof(Bounds)), *cl = clus;
            Bounds *clips = (Bounds *)malloc((end - begin) * sizeof(Bounds)), *clip = clips;
            
            AffineTransform t = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
            Colorant *col = colorants + begin;
            for (iz = begin; iz < end; iz++, col++, flag++)
                if (col->ctm.a != t.a || col->ctm.b != t.b || col->ctm.c != t.c || col->ctm.d != t.d || col->ctm.tx != t.tx || col->ctm.ty != t.ty)
                    *flag = true, t = col->ctm;
            
            for (un = units, paths += begin, ctms += begin, iz = begin; iz < end; iz++, paths++, ctms++, un++)
                *un = paths->ref->bounds.unit(*ctms);
            AffineTransform inv = nullclip().invert();
            for (un = units, flag = flags, iz = begin; iz < end; iz++, un++, flag++, cl++) {
                if (*flag)
                    inv = bitmap.width ? inv : colorants[iz].ctm.invert();
                *cl = Bounds(inv.concat(*un));
            }
            Bounds dev = device;
            for (un = units, flag = flags, iz = begin; iz < end; iz++, un++, flag++, clip++) {
                if (*flag)
                    dev = Bounds(colorants[iz].ctm).integral().intersect(device);
                *clip = Bounds(*un).integral().intersect(dev);
            }
            paths -= (end - begin), ctms -= (end - begin), un = units, cl = clus, clip = clips;
            for (iz = begin; iz < end; iz++, paths++, ctms++, un++, cl++, clip++)
                if (paths->ref->shapes || paths->ref->bounds.lx != FLT_MAX)
                    if (clip->lx != clip->ux && clip->ly != clip->uy) {
                        bool hit = cl->lx < 0.f || cl->ux > 1.f || cl->ly < 0.f || cl->uy > 1.f;
                        if (cl->ux >= 0.f && cl->lx < 1.f && cl->uy >= 0.f && cl->ly < 1.f)
                            drawPath(*paths, *ctms, *un, even, & colorants[iz].src0, iz, *clip, hit, width);
                    }
            free(flags), free(units), free(clus), free(clips);
        }
        void drawPath(Path& path, AffineTransform ctm, AffineTransform unit, bool even, uint8_t *src, size_t iz, Bounds clip, bool hit, float width) {
            Info sgmnts(nullptr, 0, & segments[0]);
            if (bitmap.width) {
                if (path.ref->shapes)
                    return;
                float w = clip.ux - clip.lx, h = clip.uy - clip.ly, stride = w + 1.f;
                if (stride * h < deltas.size()) {
                    writePath(path, AffineTransform(ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx - clip.lx, ctm.ty - clip.ly), Bounds(0.f, 0.f, w, h), writeDeltaSegment, Info(& deltas[0], stride, nullptr));
                    writeDeltas(& deltas[0], stride, clip, even, src, & bitmap);
                } else {
                    writePath(path, ctm, clip, writeClippedSegment, sgmnts);
                    writeSegments(sgmnts.segments, clip, even, Info(& deltas[0], stride, nullptr), stride, src, & bitmap);
                }
            } else {
                if (path.ref->shapes) {
                    gpu.shapesCount += path.ref->shapesCount;
                    new (gpu.quads.alloc(1)) GPU::Quad(ctm, iz, GPU::Quad::kShapes);
                } else {
                    if (width) {
                        writePath(path, ctm, Bounds(clip.lx - width, clip.ly - width, clip.ux + width, clip.uy + width), writeOutlineSegment, sgmnts);
                        size_t count = sgmnts.segments->end - sgmnts.segments->idx;
                        if (count > 1) {
                            gpu.outlinesCount += count;
                            new (gpu.quads.alloc(1)) GPU::Quad(0.f, 0.f, 0.f, 0.f, 0, 0, iz, GPU::Quad::kOutlines, width, 0, sgmnts.segments->idx, count, 0);
                            sgmnts.segments->idx = sgmnts.segments->end;
                        }
                    } else {
                        AffineTransform m = { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f };
                        Cache::Entry *e = !(path.ref->hash || clip.uy - clip.ly <= kFastHeight) ? nullptr : cache.addPath(path, ctm, unit, clip, Info(nullptr, 0, & cache.segments), m);
                        if (e && e->begin != e->end) {
                            if (clip.uy - clip.ly > kFastHeight) {
                                writeCachedOutline(cache.segments.base + e->begin, cache.segments.base + e->end, m, sgmnts);
                                writeSegments(sgmnts.segments, clip, even, src, iz, hit, & gpu);
                            } else {
                                float ox, oy, iy = 0.f;
                                if (path.ref->hash) {
                                    iy = -float(cache.ms.end + 1);
                                    new (cache.ms.alloc(1)) Segment(m.tx, m.ty, m.tx + m.a, m.ty + m.b);
                                }
                                size_t count = e->end - e->begin;
                                gpu.allocator.alloc(clip.ux - clip.lx, clip.uy - clip.ly, ox, oy);
                                new (gpu.quads.alloc(1)) GPU::Quad(clip.lx, clip.ly, clip.ux, clip.uy, ox, oy, iz, GPU::Quad::kCell, iy, -1, e->begin, 0xFFFFFF, count);
                                gpu.edgeInstances += (count + 1) / 2;
                            }
                        } else {
                            writePath(path, ctm, clip, writeClippedSegment, sgmnts);
                            writeSegments(sgmnts.segments, clip, even, src, iz, hit, & gpu);
                        }
                    }
                }
            }
        }
        Bitmap bitmap;
        GPU gpu;
        Bounds device;
        static constexpr float kfh = kFatHeight, krfh = 1.0 / kfh;
        std::vector<float> deltas;
        std::vector<Row<Segment>> segments;
        Cache cache;
    };
    
    static void writeCachedOutline(Segment *s, Segment *end, AffineTransform m, Info info) {
        float x0, y0, x1, y1, iy0, iy1;
        for (x0 = s->x0 * m.a + s->y0 * m.c + m.tx, y0 = s->x0 * m.b + s->y0 * m.d + m.ty, iy0 = floorf(y0 * Context::krfh); s < end; s++, x0 = x1, y0 = y1, iy0 = iy1) {
            x1 = s->x1 * m.a + s->y1 * m.c + m.tx, y1 = s->x1 * m.b + s->y1 * m.d + m.ty, iy1 = floorf(y1 * Context::krfh);
            if (s->x0 != FLT_MAX) {
                if (iy0 == iy1 && y0 != y1)
                    new (info.segments[size_t(iy0)].alloc(1)) Segment(x0, y0, x1, y1);
                else
                    writeClippedSegment(x0, y0, x1, y1, & info);
            } else if (s < end - 1)
                x1 = (s + 1)->x0 * m.a + (s + 1)->y0 * m.c + m.tx, y1 = (s + 1)->x0 * m.b + (s + 1)->y0 * m.d + m.ty, iy1 = floorf(y1 * Context::krfh);
        }
    }
    static void writePath(Path& path, AffineTransform ctm, Bounds clip, Function function, Info info) {
        float sx = FLT_MAX, sy = FLT_MAX, x0 = FLT_MAX, y0 = FLT_MAX, x1, y1, x2, y2, x3, y3;
        bool fs = false, f0 = false, f1, f2, f3;
        for (Sequence::Atom& atom : path.ref->atoms)
            for (uint8_t index = 0, type = 0xF & atom.types[0]; type != Sequence::Atom::kNull; type = 0xF & (atom.types[index / 2] >> ((index & 1) * 4))) {
                float *p = atom.points + index * 2;
                switch (type) {
                    case Sequence::Atom::kMove:
                        if (sx != FLT_MAX && (sx != x0 || sy != y0)) {
                            if (f0 || fs)
                                writeClippedLine(x0, y0, sx, sy, clip, function, & info);
                            else
                                (*function)(x0, y0, sx, sy, & info);
                        }
                        if (sx != FLT_MAX && function == writeOutlineSegment)
                            (*function)(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, & info);
                        sx = x0 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, sy = y0 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        fs = f0 = x0 < clip.lx || x0 >= clip.ux || y0 < clip.ly || y0 >= clip.uy;
                        index++;
                        break;
                    case Sequence::Atom::kLine:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        f1 = x1 < clip.lx || x1 >= clip.ux || y1 < clip.ly || y1 >= clip.uy;
                        if (f0 || f1)
                            writeClippedLine(x0, y0, x1, y1, clip, function, & info);
                        else
                            (*function)(x0, y0, x1, y1, & info);
                        x0 = x1, y0 = y1, f0 = f1;
                        index++;
                        break;
                    case Sequence::Atom::kQuadratic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        f1 = x1 < clip.lx || x1 >= clip.ux || y1 < clip.ly || y1 >= clip.uy;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        f2 = x2 < clip.lx || x2 >= clip.ux || y2 < clip.ly || y2 >= clip.uy;
                        if (f0 || f1 || f2)
                            writeClippedQuadratic(x0, y0, x1, y1, x2, y2, clip, function, & info);
                        else
                            writeQuadratic(x0, y0, x1, y1, x2, y2, function, & info);
                        x0 = x2, y0 = y2, f0 = f2;
                        index += 2;
                        break;
                    case Sequence::Atom::kCubic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        f1 = x1 < clip.lx || x1 >= clip.ux || y1 < clip.ly || y1 >= clip.uy;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        f2 = x2 < clip.lx || x2 >= clip.ux || y2 < clip.ly || y2 >= clip.uy;
                        x3 = p[4] * ctm.a + p[5] * ctm.c + ctm.tx, y3 = p[4] * ctm.b + p[5] * ctm.d + ctm.ty;
                        f3 = x3 < clip.lx || x3 >= clip.ux || y3 < clip.ly || y3 >= clip.uy;
                        if (f0 || f1 || f2 || f3)
                            writeClippedCubic(x0, y0, x1, y1, x2, y2, x3, y3, clip, function, & info);
                        else
                            writeCubic(x0, y0, x1, y1, x2, y2, x3, y3, function, & info);
                        x0 = x3, y0 = y3, f0 = f3;
                        index += 3;
                        break;
                    case Sequence::Atom::kClose:
                        index++;
                        break;
                }
            }
        if (sx != FLT_MAX && (sx != x0 || sy != y0)) {
            if (f0 || fs)
                writeClippedLine(x0, y0, sx, sy, clip, function, & info);
            else
                (*function)(x0, y0, sx, sy, & info);
        }
        if (sx != FLT_MAX && function == writeOutlineSegment)
            (*function)(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, & info);
    }
    static void writeClippedLine(float x0, float y0, float x1, float y1, Bounds clip, Function function, Info *info) {
        float ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
        if (ly < clip.uy && uy > clip.ly && (ly != uy || function == writeOutlineSegment)) {
            float sy0, sy1, dx, dy, ty0, ty1, tx0, tx1, sx0, sx1, mx, vx;
            sy0 = y0 < clip.ly ? clip.ly : y0 > clip.uy ? clip.uy : y0;
            sy1 = y1 < clip.ly ? clip.ly : y1 > clip.uy ? clip.uy : y1;
            dx = x1 - x0, dy = y1 - y0;
            ty0 = dy == 0.f ? 0.f : (sy0 - y0) / dy;
            ty1 = dy == 0.f ? 1.f : (sy1 - y0) / dy;
            tx0 = dx == 0.f ? 0.f : (clip.lx - x0) / dx, tx0 = tx0 < ty0 ? ty0 : tx0 > ty1 ? ty1 : tx0;
            tx1 = dx == 0.f ? 1.f : (clip.ux - x0) / dx, tx1 = tx1 < ty0 ? ty0 : tx1 > ty1 ? ty1 : tx1;
            float ts[4] = { ty0, tx0 < tx1 ? tx0 : tx1, tx0 > tx1 ? tx0 : tx1, ty1 };
            for (int i = 0; i < 3; i++)
                if (ts[i] != ts[i + 1]) {
                    sy0 = y0 + ts[i] * dy, sy0 = sy0 < clip.ly ? clip.ly : sy0 > clip.uy ? clip.uy : sy0;
                    sy1 = y0 + ts[i + 1] * dy, sy1 = sy1 < clip.ly ? clip.ly : sy1 > clip.uy ? clip.uy : sy1;
                    mx = x0 + (ts[i] + ts[i + 1]) * 0.5f * dx;
                    if (mx >= clip.lx && mx < clip.ux) {
                        sx0 = x0 + ts[i] * dx, sx0 = sx0 < clip.lx ? clip.lx : sx0 > clip.ux ? clip.ux : sx0;
                        sx1 = x0 + ts[i + 1] * dx, sx1 = sx1 < clip.lx ? clip.lx : sx1 > clip.ux ? clip.ux : sx1;
                        (*function)(sx0, sy0, sx1, sy1, info);
                    } else if (function != writeOutlineSegment) {
                        vx = mx < clip.lx ? clip.lx : clip.ux;
                        (*function)(vx, sy0, vx, sy1, info);
                    }
                }
        }
    }
    static int solveQuadratic(double A, double B, double C, float *ts, int end) {
        if (fabs(A) < 1e-3)
            ts[end++] = -C / B;
        else {
            double d = B * B - 4.0 * A * C, r;
            if (d >= 0)
                r = sqrt(d), ts[end++] = (-B + r) * 0.5 / A, ts[end++] = (-B - r) * 0.5 / A;
        }
        return end;
    }
    static void writeClippedQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, Bounds clip, Function function, Info *info) {
        float ly, uy, lx, ux, ax, bx, ay, by, ts[8], t0, t1, t, x, y, vx, tx0, ty0, tx1, ty1, tx2, ty2;
        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2;
        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2;
        if (ly < clip.uy && uy > clip.ly) {
            lx = x0 < x1 ? x0 : x1, lx = lx < x2 ? lx : x2;
            ux = x0 > x1 ? x0 : x1, ux = ux > x2 ? ux : x2;
            ax = x0 + x2 - x1 - x1, bx = 2.f * (x1 - x0);
            ay = y0 + y2 - y1 - y1, by = 2.f * (y1 - y0);
            int end = 0;
            if (clip.ly >= ly && clip.ly < uy)
                end = solveQuadratic(ay, by, y0 - clip.ly, ts, end);
            if (clip.uy >= ly && clip.uy < uy)
                end = solveQuadratic(ay, by, y0 - clip.uy, ts, end);
            if (clip.lx >= lx && clip.lx < ux)
                end = solveQuadratic(ax, bx, x0 - clip.lx, ts, end);
            if (clip.ux >= lx && clip.ux < ux)
                end = solveQuadratic(ax, bx, x0 - clip.ux, ts, end);
            if (end < 8)
                ts[end++] = 0.f, ts[end++] = 1.f;
            std::sort(& ts[0], & ts[end]);
            for (int i = 0; i < end - 1; i++) {
                t0 = ts[i],     t0 = t0 < 0.f ? 0.f : t0 > 1.f ? 1.f : t0;
                t1 = ts[i + 1], t1 = t1 < 0.f ? 0.f : t1 > 1.f ? 1.f : t1;
                if (t0 != t1) {
                    t = (t0 + t1) * 0.5f;
                    y = (ay * t + by) * t + y0;
                    if (y >= clip.ly && y < clip.uy) {
                        x = (ax * t + bx) * t + x0;
                        bool visible = x >= clip.lx && x < clip.ux;
                        tx0 = (ax * t0 + bx) * t0 + x0, ty0 = (ay * t0 + by) * t0 + y0;
                        tx2 = (ax * t1 + bx) * t1 + x0, ty2 = (ay * t1 + by) * t1 + y0;
                        tx1 = 2.f * x - 0.5f * (tx0 + tx2), ty1 = 2.f * y - 0.5f * (ty0 + ty2);
                        ty0 = ty0 < clip.ly ? clip.ly : ty0 > clip.uy ? clip.uy : ty0;
                        ty2 = ty2 < clip.ly ? clip.ly : ty2 > clip.uy ? clip.uy : ty2;
                        if (visible) {
                            tx0 = tx0 < clip.lx ? clip.lx : tx0 > clip.ux ? clip.ux : tx0;
                            tx2 = tx2 < clip.lx ? clip.lx : tx2 > clip.ux ? clip.ux : tx2;
                            writeQuadratic(tx0, ty0, tx1, ty1, tx2, ty2, function, info);
                       } else if (function != writeOutlineSegment) {
                            vx = x <= clip.lx ? clip.lx : clip.ux;
                            (*function)(vx, ty0, vx, ty2, info);
                        }
                    }
                }
            }
        }
    }
    static void writeQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, Function function, Info *info) {
        float ax, ay, a, count, dt, f2x, f1x, f2y, f1y;
        ax = x0 + x2 - x1 - x1, ay = y0 + y2 - y1 - y1, a = ax * ax + ay * ay;
        count = a < 0.1f ? 1.f : a < 8.f ? 2.f : 3.f + floorf(sqrtf(sqrtf(a - 8.f))), dt = 1.f / count;
        ax *= dt * dt, f2x = 2.f * ax, f1x = ax + 2.f * (x1 - x0) * dt;
        ay *= dt * dt, f2y = 2.f * ay, f1y = ay + 2.f * (y1 - y0) * dt;
        x1 = x0, y1 = y0;
        while (--count) {
            x1 += f1x, f1x += f2x, y1 += f1y, f1y += f2y;
            (*function)(x0, y0, x1, y1, info);
            x0 = x1, y0 = y1;
        }
        (*function)(x0, y0, x2, y2, info);
    }
    static int solveCubic(double A, double B, double C, double D, float *ts, int end) {
        if (fabs(D) < 1e-3)
            return solveQuadratic(A, B, C, ts, end);
        else {
            const double wq0 = 2.0 / 27.0, third = 1.0 / 3.0;
            double  p, q, q2, u1, v1, a3, discriminant, sd;
            A /= D, B /= D, C /= D, p = B - A * A * third, q = A * (wq0 * A * A - third * B) + C;
            q2 = q * 0.5, a3 = A * third;
            discriminant = q2 * q2 + p * p * p / 27.0;
            if (discriminant < 0) {
                double mp3 = -p / 3, mp33 = mp3 * mp3 * mp3, r = sqrt(mp33), t = -q / (2 * r), cosphi = t < -1 ? -1 : t > 1 ? 1 : t;
                double phi = acos(cosphi), crtr = 2 * copysign(cbrt(fabs(r)), r);
                ts[end++] = crtr * cos(phi / 3) - a3, ts[end++] = crtr * cos((phi + 2 * M_PI) / 3) - a3, ts[end++] = crtr * cos((phi + 4 * M_PI) / 3) - a3;
            } else if (discriminant == 0) {
                u1 = copysign(cbrt(fabs(q2)), q2);
                ts[end++] = 2 * u1 - a3, ts[end++] = -u1 - a3;
            } else {
                sd = sqrt(discriminant), u1 = copysign(cbrt(fabs(sd - q2)), sd - q2), v1 = copysign(cbrt(fabs(sd + q2)), sd + q2);
                ts[end++] = u1 - v1 - a3;
            }
        }
        return end;
    }
    static void writeClippedCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Bounds clip, Function function, Info *info) {
        float ly, uy, lx, ux;
        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2, ly = ly < y3 ? ly : y3;
        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2, uy = uy > y3 ? uy : y3;
        if (ly < clip.uy && uy > clip.ly) {
            float cy, by, ay, cx, bx, ax, ts[12], t0, t1, t, x, y, vx, tx0, ty0, tx1, ty1, tx2, ty2, tx3, ty3, fx, gx, fy, gy;
            lx = x0 < x1 ? x0 : x1, lx = lx < x2 ? lx : x2, lx = lx < x3 ? lx : x3;
            ux = x0 > x1 ? x0 : x1, ux = ux > x2 ? ux : x2, ux = ux > x3 ? ux : x3;
            cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
            cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
            int end = 0;
            if (clip.ly >= ly && clip.ly < uy)
                end = solveCubic(by, cy, y0 - clip.ly, ay, ts, end);
            if (clip.uy >= ly && clip.uy < uy)
                end = solveCubic(by, cy, y0 - clip.uy, ay, ts, end);
            if (clip.lx >= lx && clip.lx < ux)
                end = solveCubic(bx, cx, x0 - clip.lx, ax, ts, end);
            if (clip.ux >= lx && clip.ux < ux)
                end = solveCubic(bx, cx, x0 - clip.ux, ax, ts, end);
            if (end < 12)
                ts[end++] = 0.f, ts[end++] = 1.f;
            std::sort(& ts[0], & ts[end]);
            for (int i = 0; i < end - 1; i++) {
                t0 = ts[i],     t0 = t0 < 0.f ? 0.f : t0 > 1.f ? 1.f : t0;
                t1 = ts[i + 1], t1 = t1 < 0.f ? 0.f : t1 > 1.f ? 1.f : t1;
                if (t0 != t1) {
                    t = (t0 + t1) * 0.5f;
                    y = ((ay * t + by) * t + cy) * t + y0;
                    if (y >= clip.ly && y < clip.uy) {
                        x = ((ax * t + bx) * t + cx) * t + x0;
                        bool visible = x >= clip.lx && x < clip.ux;
                        tx0 = ((ax * t0 + bx) * t0 + cx) * t0 + x0, ty0 = ((ay * t0 + by) * t0 + cy) * t0 + y0;
                        tx3 = ((ax * t1 + bx) * t1 + cx) * t1 + x0, ty3 = ((ay * t1 + by) * t1 + cy) * t1 + y0;
                        ty0 = ty0 < clip.ly ? clip.ly : ty0 > clip.uy ? clip.uy : ty0;
                        ty3 = ty3 < clip.ly ? clip.ly : ty3 > clip.uy ? clip.uy : ty3;
                        if (visible) {
                            const float u = 1.f / 3.f, v = 2.f / 3.f, u3 = 1.f / 27.f, v3 = 8.f / 27.f, m0 = 3.f, m1 = 1.5f;
                            t = v * t0 + u * t1, tx1 = ((ax * t + bx) * t + cx) * t + x0, ty1 = ((ay * t + by) * t + cy) * t + y0;
                            t = u * t0 + v * t1, tx2 = ((ax * t + bx) * t + cx) * t + x0, ty2 = ((ay * t + by) * t + cy) * t + y0;
                            fx = tx1 - v3 * tx0 - u3 * tx3, fy = ty1 - v3 * ty0 - u3 * ty3;
                            gx = tx2 - u3 * tx0 - v3 * tx3, gy = ty2 - u3 * ty0 - v3 * ty3;
                            tx1 = fx * m0 + gx * -m1, ty1 = fy * m0 + gy * -m1;
                            tx2 = fx * -m1 + gx * m0, ty2 = fy * -m1 + gy * m0;
                            tx0 = tx0 < clip.lx ? clip.lx : tx0 > clip.ux ? clip.ux : tx0;
                            tx3 = tx3 < clip.lx ? clip.lx : tx3 > clip.ux ? clip.ux : tx3;
                            writeCubic(tx0, ty0, tx1, ty1, tx2, ty2, tx3, ty3, function, info);
                        } else if (function != writeOutlineSegment) {
                            vx = x <= clip.lx ? clip.lx : clip.ux;
                            (*function)(vx, ty0, vx, ty3, info);
                        }
                    }
                }
            }
        }
    }
    static void writeCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Function function, Info *info) {
        float cx, bx, ax, cy, by, ay, a, count, dt, dt2, f3x, f2x, f1x, f3y, f2y, f1y;
        cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
        cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
        a = ax * ax + ay * ay + bx * bx + by * by;
        count = a < 0.1f ? 1.f : a < 16.f ? 3.f : 4.f + floorf(sqrtf(sqrtf(a - 16.f)));
        dt = 1.f / count, dt2 = dt * dt;
        bx *= dt2, ax *= dt2 * dt, f3x = 6.f * ax, f2x = f3x + 2.f * bx, f1x = ax + bx + cx * dt;
        by *= dt2, ay *= dt2 * dt, f3y = 6.f * ay, f2y = f3y + 2.f * by, f1y = ay + by + cy * dt;
        x1 = x0, y1 = y0;
        while (--count) {
            x1 += f1x, f1x += f2x, f2x += f3x, y1 += f1y, f1y += f2y, f2y += f3y;
            (*function)(x0, y0, x1, y1, info);
            x0 = x1, y0 = y1;
        }
        (*function)(x0, y0, x3, y3, info);
    }
    static inline float alphaForCover(float cover, bool even) {
        float alpha = fabsf(cover);
        return even ? (1.f - fabsf(fmodf(alpha, 2.f) - 1.f)) : (alpha < 1.f ? alpha : 1.f);
    }
    static void writeDeltas(float *deltas, uint32_t stride, Bounds clip, bool even, uint8_t *src, Bitmap *bitmap) {
        if (clip.lx == clip.ux)
            for (float y = clip.ly; y < clip.uy; y++, deltas += stride)
                *deltas = 0.f;
        else {
            uint8_t *pixelAddress = nullptr, *pixel;
            size_t bstride = 0, bytespp = 0;
            if (bitmap)
                pixelAddress = bitmap->pixelAddress(clip.lx, clip.ly), bstride = bitmap->stride, bytespp = bitmap->bytespp;
            float src0 = src[0], src1 = src[1], src2 = src[2], srcAlpha = src[3] * 0.003921568627f, y, x, cover, alpha, *delta;
            for (y = clip.ly; y < clip.uy; y++, deltas += stride, pixelAddress -= bstride, *delta = 0.f)
                for (cover = alpha = 0.f, delta = deltas, pixel = pixelAddress, x = clip.lx; x < clip.ux; x++, delta++, pixel += bytespp) {
                    if (*delta)
                        cover += *delta, *delta = 0.f, alpha = alphaForCover(cover, even);
                    if (alpha > 0.003921568627f)
                        writePixel(src0, src1, src2, alpha * srcAlpha, pixel);
                }
        }
    }
    static void radixSort(uint32_t *in, short n, short *counts0, short *counts1) {
        uint32_t x, tmp[n];
        memset(counts0, 0, sizeof(short) * 256);
        for (int i = 0; i < n; i++)
            counts0[in[i] & 0xFF]++;
        for (short *src = counts0, *dst = src + 1, i = 1; i < 256; i++)
            *dst++ += *src++;
        memset(counts1, 0, sizeof(short) * 256);
        for (int i = n - 1; i >= 0; i--)
            x = in[i], tmp[--counts0[x & 0xFF]] = x, counts1[(x >> 8) & 0xFF]++;
        for (short *src = counts1, *dst = src + 1, i = 1; i < 256; i++)
            *dst++ += *src++;
        for (int i = n - 1; i >= 0; i--)
            x = tmp[i], in[--counts1[(x >> 8) & 0xFF]] = x;
    }
    
    static void writeSegments(Row<Segment> *segments, Bounds clip, bool even, uint8_t *src, size_t iz, bool hit, GPU *gpu) {
        size_t ily = floorf(clip.ly * Context::krfh), iuy = ceilf(clip.uy * Context::krfh), count, i, begin;
        short counts0[256], counts1[256], iy;
        float ly, uy, scale, cover, winding, lx, ux, x, ox, oy;
        Segment *segment;
        Row<Segment::Index>& indices = gpu->indices;    Segment::Index *index;
        for (segments += ily, iy = ily; iy < iuy; iy++, segments++) {
            ly = iy * Context::kfh, ly = ly < clip.ly ? clip.ly : ly > clip.uy ? clip.uy : ly;
            uy = (iy + 1) * Context::kfh, uy = uy < clip.ly ? clip.ly : uy > clip.uy ? clip.uy : uy;
            count = segments->end - segments->idx;
            if (count) {
                if (clip.ux - clip.lx < 32.f) {
                    gpu->allocator.alloc(clip.ux - clip.lx, Context::kfh, ox, oy);
                    new (gpu->quads.alloc(1)) GPU::Quad(clip.lx, ly, clip.ux, uy, ox, oy, iz, GPU::Quad::kCell, 0.f, iy, segments->idx, 0xFFFFFF, count);
                    gpu->edgeInstances += (count + 1) / 2;
                } else {
                    for (index = indices.alloc(count), segment = segments->base + segments->idx, i = 0; i < count; i++, segment++, index++)
                        new (index) Segment::Index(segment->x0 < segment->x1 ? segment->x0 : segment->x1, i);
                    
                    if (indices.end - indices.idx > 32)
                        radixSort((uint32_t *)indices.base + indices.idx, indices.end - indices.idx, counts0, counts1);
                    else
                        std::sort(indices.base + indices.idx, indices.base + indices.end);
                    
                    for (scale = 1.f / (uy - ly), cover = winding = 0.f, index = indices.base + indices.idx, lx = ux = index->x, i = begin = indices.idx; i < indices.end; i++, index++) {
                        if (index->x > ux && winding - floorf(winding) < 1e-6f) {
                            if (lx != ux) {
                                gpu->allocator.alloc(ux - lx, Context::kfh, ox, oy);
                                new (gpu->quads.alloc(1)) GPU::Quad(lx, ly, ux, uy, ox, oy, iz, GPU::Quad::kCell, cover, iy, segments->idx, begin, i - begin);
                                gpu->edgeInstances += (i - begin + 1) / 2;
                            }
                            begin = i;
                            if (alphaForCover(winding, even) > 0.998f) {
                                lx = ux, ux = index->x;
                                if (lx != ux) {
                                    if (src[3] == 255 && !hit)
                                        new (gpu->opaques.alloc(1)) GPU::Quad(lx, ly, ux, uy, 0.f, 0.f, iz, GPU::Quad::kCell, 1.f, 0, 0, 0, 0);
                                    else
                                        new (gpu->quads.alloc(1)) GPU::Quad(lx, ly, ux, uy, 0.f, 0.f, iz, GPU::Quad::kSolidCell, 1.f, 0, 0, 0, 0);
                                }
                            }
                            lx = ux = index->x;
                            cover = winding;
                        }
                        segment = segments->base + segments->idx + index->i;
                        winding += (segment->y1 - segment->y0) * scale;
                        x = ceilf(segment->x0 > segment->x1 ? segment->x0 : segment->x1), ux = x > ux ? x : ux;
                    }
                    if (lx != ux) {
                        gpu->allocator.alloc(ux - lx, Context::kfh, ox, oy);
                        new (gpu->quads.alloc(1)) GPU::Quad(lx, ly, ux, uy, ox, oy, iz, GPU::Quad::kCell, cover, iy, segments->idx, begin, i - begin);
                        gpu->edgeInstances += (i - begin + 1) / 2;
                    }
                }
                indices.idx = indices.end;
                segments->idx = segments->end;
            }
        }
    }
    static void writeSegments(Row<Segment> *segments, Bounds clip, bool even, Info del, uint32_t stride, uint8_t *src, Bitmap *bitmap) {
        size_t ily = floorf(clip.ly * Context::krfh), iuy = ceilf(clip.uy * Context::krfh), iy, i;
        short counts0[256], counts1[256];
        float src0 = src[0], src1 = src[1], src2 = src[2], srcAlpha = src[3] * 0.003921568627f, ly, uy, scale, cover, lx, ux, x, y, *delta;
        Segment *segment;
        Row<Segment::Index> indices;    Segment::Index *index;
        for (segments += ily, iy = ily; iy < iuy; iy++, segments++) {
            ly = iy * Context::kfh, ly = ly < clip.ly ? clip.ly : ly > clip.uy ? clip.uy : ly;
            uy = (iy + 1) * Context::kfh, uy = uy < clip.ly ? clip.ly : uy > clip.uy ? clip.uy : uy;
            if (segments->end) {
                for (index = indices.alloc(segments->end), segment = segments->base, i = 0; i < segments->end; i++, segment++, index++)
                    new (index) Segment::Index(segment->x0 < segment->x1 ? segment->x0 : segment->x1, i);
                if (indices.end > 32)
                    radixSort((uint32_t *)indices.base, indices.end, counts0, counts1);
                else
                    std::sort(indices.base, indices.base + indices.end);
                for (scale = 1.f / (uy - ly), cover = 0.f, index = indices.base, lx = ux = index->x, i = 0; i < indices.end; i++, index++) {
                    if (index->x > ux && cover - floorf(cover) < 1e-6f) {
                        writeDeltas(del.deltas, stride, Bounds(lx, ly, ux, uy), even, src, bitmap);
                        lx = ux, ux = index->x;
                        if (alphaForCover(cover, even) > 0.998f)
                            for (delta = del.deltas, y = ly; y < uy; y++, delta += stride) {
                                *delta = cover;
                                uint8_t *dst = bitmap->pixelAddress(lx, y);
                                if (src[3] == 255)
                                    memset_pattern4(dst, src, (ux - lx) * bitmap->bytespp);
                                else
                                    for (size_t x = lx; x < ux; x++, dst += bitmap->bytespp)
                                        writePixel(src0, src1, src2, srcAlpha, dst);
                            }
                        lx = ux = index->x;
                    }
                    segment = segments->base + index->i;
                    cover += (segment->y1 - segment->y0) * scale;
                    x = ceilf(segment->x0 > segment->x1 ? segment->x0 : segment->x1), ux = x > ux ? x : ux;
                    writeDeltaSegment(segment->x0 - lx, segment->y0 - ly, segment->x1 - lx, segment->y1 - ly, & del);
                }
                writeDeltas(del.deltas, stride, Bounds(lx, ly, ux, uy), even, src, bitmap);
                indices.empty();
                segments->empty();
            }
        }
    }
    static inline void writePixel(float src0, float src1, float src2, float alpha, uint8_t *dst) {
#ifdef RASTERIZER_SIMD
        vec4f src4 = { src0, src1, src2, 255.f }, alpha4 = { alpha, alpha, alpha, alpha }, mul4 = alpha4 * src4, one4 = { 1.f, 1.f, 1.f, 1.f };
        vec4b bgra = { dst[0], dst[1], dst[2], dst[3] };
        mul4 += __builtin_convertvector(bgra, vec4f) * (one4 - alpha4);
        *((vec4b *)dst) = __builtin_convertvector(mul4, vec4b);
#else
        if (*((uint32_t *)dst) == 0)
            dst[0] = src0 * alpha, dst[1] = src1 * alpha, dst[2] = src2 * alpha, dst[3] = 255.f * alpha;
        else {
            float s = 1.f - alpha;
            dst[0] = dst[0] * s + src0 * alpha, dst[1] = dst[1] * s + src1 * alpha, dst[2] = dst[2] * s + src2 * alpha, dst[3] = dst[3] * s + 255.f * alpha;
        }
#endif
    }
};
