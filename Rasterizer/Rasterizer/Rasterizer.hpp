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
        Bounds integral() { return { floorf(lx), floorf(ly), ceilf(ux), ceilf(uy) }; }
        Bounds intersect(Bounds other) {
            return {
                lx < other.lx ? other.lx : lx > other.ux ? other.ux : lx, ly < other.ly ? other.ly : ly > other.uy ? other.uy : ly,
                ux < other.lx ? other.lx : ux > other.ux ? other.ux : ux, uy < other.ly ? other.ly : uy > other.uy ? other.uy : uy
            };
        }
        Bounds transform(AffineTransform ctm) const {
            AffineTransform t = unit(ctm);
            return {
                t.tx + (t.a < 0.f ? t.a : 0.f) + (t.c < 0.f ? t.c : 0.f), t.ty + (t.b < 0.f ? t.b : 0.f) + (t.d < 0.f ? t.d : 0.f),
                t.tx + (t.a > 0.f ? t.a : 0.f) + (t.c > 0.f ? t.c : 0.f), t.ty + (t.b > 0.f ? t.b : 0.f) + (t.d > 0.f ? t.d : 0.f)
            };
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
        Sequence() : end(Atom::kCapacity), px(0), py(0), bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX), units(nullptr), circles(nullptr), refCount(1) {}
        Sequence(size_t count) : end(count), px(0), py(0), bounds(-5e11f, -5e11f, 5e11f, 5e11f), units((AffineTransform *)malloc(count * sizeof(units[0]))), circles((bool *)malloc(count * sizeof(bool))), refCount(1) {}
        ~Sequence() { if (units) free(units), free(circles); }
        
        float *alloc(Atom::Type type, size_t size) {
            if (end + size > Atom::kCapacity)
                end = 0, atoms.emplace_back();
            atoms.back().types[end / 2] |= (uint8_t(type) << ((end & 1) * 4));
            end += size;
            return atoms.back().points + (end - size) * 2;
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
        size_t refCount, end;
        std::vector<Atom> atoms;
        float px, py;
        AffineTransform *units;
        bool *circles;
        Bounds bounds;
    };
    struct Path {
        Path() {
            sequence = new Sequence();
        }
        Path(size_t count) {
            sequence = new Sequence(count);
        }
        Path(const Path& other) {
            if (this != & other)
                sequence = other.sequence, sequence->refCount++;
        }
        ~Path() {
            if (--(sequence->refCount) == 0)
                delete sequence;
        }
        Sequence *sequence = nullptr;
    };
    struct Bitmap {
        Bitmap() : data(nullptr), width(0), height(0), stride(0), bpp(0), bytespp(0) {}
        Bitmap(void *data, size_t width, size_t height, size_t stride, size_t bpp)
        : data((uint8_t *)data), width(width), height(height), stride(stride), bpp(bpp), bytespp(bpp / 8) {}
        void clear(uint32_t pixel) { memset_pattern4(data, & pixel, stride * height); }
        inline uint8_t *pixelAddress(short x, short y) { return data + stride * (height - 1 - y) + x * bytespp; }
        uint8_t *data;
        size_t width, height, stride, bpp, bytespp;
    };
    struct Segment {
        Segment() {}
        Segment(float x0, float y0, float x1, float y1) : x0(x0), y0(y0), x1(x1), y1(y1) {}
        struct Index {
            Index() {}
            Index(short x, short i) : x(x), i(i) {}
            short x, i;
            inline bool operator< (const Index& other) const { return x < other.x; }
        };
        float x0, y0, x1, y1;
    };
    template<typename T>
    struct Row {
        Row() : end(0), size(0), idx(0) {}
        void empty() { end = idx = 0; }
        inline T *alloc(size_t n) {
            size_t i = end;
            end += n;
            if (size < end)
                size = end, elems.resize(size), base = & elems[0];
            return base + i;
        }
        std::vector<T> elems;
        size_t end, size, idx;
        T *base;
    };
    template<typename T>
    struct Pages {
        const size_t kPageSize = 4096;
        Pages() : size(0), base(nullptr) {}
        ~Pages() { if (base) free(base); }
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
                width = w, height = h, strip = sheet = Bounds(0.f, 0.f, 0.f, 0.f);
            }
            void alloc(float w, float& ox, float& oy) {
                if (strip.ux - strip.lx < w) {
                    if (sheet.uy - sheet.ly < Context::kfh)
                        sheet = Bounds(0.f, 0.f, width, height);
                    strip = sheet;
                    strip.uy = sheet.ly + Context::kfh, sheet.ly = strip.uy;
                }
                ox = strip.lx, strip.lx += w, oy = strip.ly;
            }
            size_t width, height;
            Bounds sheet, strip;
        };
        struct Cell {
            Cell() {}
            Cell(float lx, float ly, float ux, float uy, float ox, float oy)
            : lx(lx), ly(ly), ux(ux), uy(uy), ox(ox), oy(oy) {}
            short lx, ly, ux, uy, ox, oy;
        };
        struct SuperCell {
            SuperCell() {}
            SuperCell(float lx, float ly, float ux, float uy, float ox, float oy, float cover, size_t iy, size_t idx, size_t begin, size_t count)
            : cell(lx, ly, ux, uy, ox, oy), cover(cover), iy(short(iy)), count(short(count)), idx(uint32_t(idx)), begin(uint32_t(begin)) {}
            Cell cell;
            float cover;
            short iy, count;
            uint32_t idx, begin;
        };
        struct Quad {
            enum Type { kNull = 0, kRect, kCircle, kCell, kSolidCell, kShapes, kOutlines };
            Quad() {}
            Quad(float lx, float ly, float ux, float uy, float ox, float oy, size_t iz, int type, float cover, size_t iy, size_t idx, size_t begin, size_t end)
            : super(lx, ly, ux, uy, ox, oy, cover, iy, idx, begin, end), iz((uint32_t)iz | type << 24) {}
            Quad(AffineTransform unit, size_t iz, int type) : unit(unit), iz((uint32_t)iz | type << 24) {}
            union {
                SuperCell super;
                AffineTransform unit;
            };
            uint32_t iz;
        };
        struct Edge {
            Cell cell;
            Segment segments[kSegmentsCount];
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
            enum Type { kColorants, kEdges, kQuads, kOpaques, kShapes };
            Entry() {}
            Entry(Type type, size_t begin, size_t end) : type(type), begin(begin), end(end) {}
            Type type;
            size_t begin, end;
        };
        Pages<uint8_t> data;
        Row<Entry> entries;
        Colorant clearColor;
    };
    typedef void (*Function)(float x0, float y0, float x1, float y1, float *deltas, uint32_t stride, Row<Segment> *segments);
    static void writeOutlineSegment(float x0, float y0, float x1, float y1, float *deltas, uint32_t stride, Row<Segment> *segments) {
        new (segments->alloc(1)) Segment(x0, y0, x1, y1);
    }
    static void writeClippedSegment(float x0, float y0, float x1, float y1, float *deltas, uint32_t stride, Row<Segment> *segments) {
        if (y0 == y1)
            return;
        float iy0 = y0 * Context::krfh, iy1 = y1 * Context::krfh;
        if (floorf(iy0) == floorf(iy1))
            new (segments[size_t(iy0)].alloc(1)) Segment(x0, y0, x1, y1);
        else {
            float sy1, dy, sx1, dx;
            int s, ds, count;
            if (y0 < y1) {
                iy0 = floorf(iy0), iy1 = ceilf(iy1), sy1 = iy0 * Context::kfh, dy = Context::kfh;
                s = iy0, ds = 1;
            } else {
                iy0 = floorf(iy1), iy1 = ceilf(y0 * Context::krfh), sy1 = iy1 * Context::kfh, dy = -Context::kfh;
                s = iy1 - 1.f, ds = -1;
            }
            count = iy1 - iy0;
            dx = dy / (y1 - y0) * (x1 - x0);
            sx1 = (sy1 - y0) / dy * dx + x0;
            while (--count) {
                sx1 += dx, sy1 += dy;
                new (segments[s].alloc(1)) Segment(x0, y0, sx1, sy1);
                x0 = sx1, y0 = sy1;
                s += ds;
            }
            new (segments[s].alloc(1)) Segment(x0, y0, x1, y1);
        }
    }
    static void writeDeltaSegment(float x0, float y0, float x1, float y1, float *deltas, uint32_t stride, Row<Segment> *segments) {
        if (y0 == y1)
            return;
        float scale = copysign(1.f, y1 - y0), tmp, dx, dy, iy0, iy1, sx0, sy0, dxdy, dydx, sx1, sy1, lx, ux, ix0, ix1, cx0, cy0, cx1, cy1, cover, area, last, *delta;
        if (scale < 0.f)
            tmp = x0, x0 = x1, x1 = tmp, tmp = y0, y0 = y1, y1 = tmp;
        dx = x1 - x0, dy = y1 - y0, dxdy = fabsf(dx) / (fabsf(dx) + 1e-4f) * dx / dy;
        for (sy0 = y0, sx0 = x0, iy0 = floorf(y0), delta = deltas + size_t(iy0 * stride); iy0 < y1; iy0 = iy1, sy0 = sy1, sx0 = sx1, delta += stride) {
            iy1 = iy0 + 1.f, sy1 = y1 > iy1 ? iy1 : y1, sx1 = x0 + (sy1 - y0) * dxdy;
            lx = sx0 < sx1 ? sx0 : sx1, ux = sx0 > sx1 ? sx0 : sx1;
            ix0 = floorf(lx), ix1 = ix0 + 1.f;
            if (lx >= ix0 && ux <= ix1) {
                cover = (sy1 - sy0) * scale, area = (ix1 - (ux + lx) * 0.5f);
                delta[int(ix0)] += cover * area;
                if (area < 1.f && ix1 < stride)
                    delta[int(ix1)] += cover * (1.f - area);
            } else {
                dydx = fabsf(dy / dx);
                for (last = 0.f, cx0 = lx, cy0 = sy0; ix0 <= ux; ix0 = ix1, cx0 = cx1, cy0 = cy1) {
                    ix1 = ix0 + 1.f, cx1 = ux < ix1 ? ux : ix1, cy1 = sy0 + (cx1 - lx) * dydx;
                    cover = (cy1 - cy0) * scale, area = (ix1 - (cx0 + cx1) * 0.5f);
                    delta[int(ix0)] += cover * area + last;
                    last = cover * (1.f - area);
                }
                if (ix0 < stride)
                    delta[int(ix0)] += last;
            }
        }
    }
    struct Context {
        static AffineTransform nullclip() { return { 1e12f, 0.f, 0.f, 1e12f, -5e11f, -5e11f }; }
        
        static void writeContextsToBuffer(Context *contexts, size_t count,
                                          Path *paths,
                                          AffineTransform *ctms,
                                          Colorant *colorants,
                                          size_t pathsCount,
                                          Buffer& buffer) {
            size_t size, i, j, begin, end, idx, qend, q, jend, iz;
            size = pathsCount * sizeof(Colorant);
            for (i = 0; i < count; i++)
                size += contexts[i].gpu.edgeInstances * sizeof(GPU::Edge) + (contexts[i].gpu.outlinesCount + contexts[i].gpu.shapesCount + contexts[i].gpu.quads.end + contexts[i].gpu.opaques.end) * sizeof(GPU::Quad);
            buffer.data.alloc(size);
            begin = end = 0;
            
            end += pathsCount * sizeof(Colorant);
            memcpy(buffer.data.base + begin, colorants, pathsCount * sizeof(Colorant));
            new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::Entry::kColorants, begin, end);
            begin = end;
            
            for (idx = begin, i = 0; i < count; i++) {
                end += contexts[i].gpu.opaques.end * sizeof(GPU::Quad);
                if (idx != end) {
                    memcpy(buffer.data.base + idx, contexts[i].gpu.opaques.base, end - idx);
                    idx = end;
                }
            }
            if (begin != end) {
                new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::Entry::kOpaques, begin, end);
                begin = end;
            }
            
            for (i = 0; i < count; i++) {
                Context *ctx = & contexts[i];
                while (ctx->gpu.quads.idx != ctx->gpu.quads.end) {
                    GPU::Quad *quad = ctx->gpu.quads.base;
                    for (qend = ctx->gpu.quads.idx + 1; qend < ctx->gpu.quads.end; qend++)
                        if (quad[qend].iz >> 24 == GPU::Quad::kCell && quad[qend].super.cell.oy == 0 && quad[qend].super.cell.ox == 0)
                            break;
                    
                    GPU::Edge *dst = (GPU::Edge *)(buffer.data.base + begin);
                    Segment *segments; Segment::Index *is;
                    for (q = ctx->gpu.quads.idx, quad += q; q < qend; q++, quad++) {
                        if (quad->iz >> 24 == GPU::Quad::kCell) {
                            end += (quad->super.count + kSegmentsCount - 1) / kSegmentsCount * sizeof(GPU::Edge);
                            segments = ctx->segments[quad->super.iy].base + quad->super.idx;
                            is = quad->super.begin == 0xFFFFFF ? nullptr : ctx->gpu.indices.base + quad->super.begin;
                            for (j = is ? quad->super.begin : 0, jend = j + quad->super.count; j < jend; j += kSegmentsCount, dst++) {
                                dst->cell = quad->super.cell;
                                dst->segments[0] = segments[is ? is++->i : j];
                                if (j + 1 < jend)
                                    dst->segments[1] = segments[is ? is++->i : j + 1];
                                else
                                    new (& dst->segments[1]) Segment(0.f, 0.f, 0.f, 0.f);
                            }
                        }
                    }
                    if (begin != end) {
                        new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::Entry::kEdges, begin, end);
                        begin = end;
                    }
                    
                    if (ctx->gpu.quads.idx != qend) {
                        quad = ctx->gpu.quads.base + ctx->gpu.quads.idx;
                        std::vector<size_t> idxes;
                        Buffer::Entry::Type type;
                        int qtype;
                        Buffer::Entry *entries, *entry;
                        qtype = quad[0].iz >> 24, type = qtype == GPU::Quad::kShapes || qtype == GPU::Quad::kOutlines ? Buffer::Entry::kShapes : Buffer::Entry::kQuads;
                        entries = entry = new (buffer.entries.alloc(1)) Buffer::Entry(type, begin, begin), idxes.emplace_back(0);
                        for (j = 0, jend = qend - ctx->gpu.quads.idx; j < jend; j++) {
                            qtype = quad[j].iz >> 24, type = qtype == GPU::Quad::kShapes || qtype == GPU::Quad::kOutlines ? Buffer::Entry::kShapes : Buffer::Entry::kQuads;
                            if (type != entry->type)
                                entry = new (buffer.entries.alloc(1)) Buffer::Entry(type, entry->end, entry->end), idxes.emplace_back(j);
                            if (qtype == GPU::Quad::kShapes) {
                                iz = quad[j].iz & 0xFFFFFF;
                                Path& path = paths[iz];
                                GPU::Quad *dst = (GPU::Quad *)(buffer.data.base + entry->end);
                                AffineTransform ctm = quad[j].unit;
                                for (int k = 0; k < path.sequence->end; k++, dst++)
                                    new (dst) GPU::Quad(ctm.concat(path.sequence->units[k]), iz, path.sequence->circles[k] ? GPU::Quad::kCircle : GPU::Quad::kRect);
                                entry->end += path.sequence->end * sizeof(GPU::Quad);
                            } else if (qtype == GPU::Quad::kOutlines) {
                                iz = quad[j].iz & 0xFFFFFF;
                                Segment *s = ctx->segments[quad[j].super.iy].base + quad[j].super.idx;
                                GPU::Quad *dst = (GPU::Quad *)(buffer.data.base + entry->end);
                                float dx, dy, l, nx, ny;
                                for (int k = 0; k < quad[j].super.begin; k++, s++, dst++) {
                                    dx = s->x1 - s->x0, dy = s->y1 - s->y0, l = sqrtf(dx * dx + dy * dy), nx = dx / l, ny = dy / l;
                                    new (dst) GPU::Quad(AffineTransform(ny, -nx, dx, dy, s->x0 - ny * 0.5f, s->y0 + nx * 0.5f), iz, GPU::Quad::kRect);
                                }
                                entry->end += quad[j].super.begin * sizeof(GPU::Quad);
                            } else
                                entry->end += sizeof(GPU::Quad);
                        }
                        for (int k = 0; k < idxes.size(); k++) {
                            if (entries[k].type == Buffer::Entry::kQuads)
                                memcpy(buffer.data.base + entries[k].begin, quad + idxes[k], entries[k].end - entries[k].begin);
                            begin = end = entries[k].end;
                        }
                        ctx->gpu.quads.idx = qend;
                    }
                }
                ctx->gpu.empty();
                for (j = 0; j < ctx->segments.size(); j++)
                    ctx->segments[j].empty();
            }
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
        }
        void drawPaths(Path *paths, AffineTransform *ctms, bool even, Colorant *colorants, size_t begin, size_t end) {
            if (begin == end)
                return;
            size_t iz;
            bool *flags = (bool *)malloc((end) * sizeof(bool));
            AffineTransform *units = (AffineTransform *)malloc((end - begin) * sizeof(AffineTransform)), *un;
            AffineTransform *clips = (AffineTransform *)malloc((end - begin) * sizeof(AffineTransform)), *cl = clips;
            Bounds *devices = (Bounds *)malloc((end - begin) * sizeof(Bounds)), *clipped = devices;
            paths += begin, ctms += begin;
            
            AffineTransform test = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
            Colorant *col = colorants;
            for (iz = 0; iz < end; iz++, col++)
                if (col->ctm.a != test.a || col->ctm.b != test.b || col->ctm.c != test.c || col->ctm.d != test.d || col->ctm.tx != test.tx || col->ctm.ty != test.ty)
                    flags[iz] = true, test = col->ctm;
            int i;
            for (i = (int)begin; i >= 0; i--)
                if (flags[i])
                    break;
            AffineTransform inv = bitmap.width ? nullclip().invert() : colorants[i].ctm.invert();
            Bounds dev = Bounds(0.f, 0.f, 1.f, 1.f).transform(colorants[i].ctm).integral().intersect(device);
            
            for (un = units, iz = begin; iz < end; iz++, paths++, ctms++, un++)
                *un = paths->sequence->bounds.unit(*ctms);
            for (un = units, iz = begin; iz < end; iz++, un++, cl++) {
                if (flags[iz])
                    inv = bitmap.width ? nullclip().invert() : colorants[iz].ctm.invert();
                *cl = inv.concat(*un);
            }
            for (un = units, iz = begin; iz < end; iz++, un++, clipped++) {
                if (flags[iz])
                    dev = Bounds(0.f, 0.f, 1.f, 1.f).transform(colorants[iz].ctm).integral().intersect(device);
                *clipped = Bounds(
                     un->tx + (un->a < 0.f ? un->a : 0.f) + (un->c < 0.f ? un->c : 0.f), un->ty + (un->b < 0.f ? un->b : 0.f) + (un->d < 0.f ? un->d : 0.f),
                     un->tx + (un->a > 0.f ? un->a : 0.f) + (un->c > 0.f ? un->c : 0.f), un->ty + (un->b > 0.f ? un->b : 0.f) + (un->d > 0.f ? un->d : 0.f)
                ).integral().intersect(dev);
            }
            paths -= (end - begin), ctms -= (end - begin), un = units, cl = clips, clipped = devices;
            for (iz = begin; iz < end; iz++, paths++, ctms++, un++, cl++, clipped++)
                if (paths->sequence->units || paths->sequence->bounds.lx != FLT_MAX)
                    if (clipped->lx != clipped->ux && clipped->ly != clipped->uy) {
                        Bounds clu(
                            cl->tx + (cl->a < 0.f ? cl->a : 0.f) + (cl->c < 0.f ? cl->c : 0.f), cl->ty + (cl->b < 0.f ? cl->b : 0.f) + (cl->d < 0.f ? cl->d : 0.f),
                            cl->tx + (cl->a > 0.f ? cl->a : 0.f) + (cl->c > 0.f ? cl->c : 0.f), cl->ty + (cl->b > 0.f ? cl->b : 0.f) + (cl->d > 0.f ? cl->d : 0.f));
                        bool hit = clu.lx < 0.f || clu.ux > 1.f || clu.ly < 0.f || clu.uy > 1.f;
                        if (clu.ux >= 0.f && clu.lx < 1.f && clu.uy >= 0.f && clu.ly < 1.f)
                            drawPath(*paths, *ctms, even, & colorants[iz].src0, iz, *clipped, hit);
                    }
            free(flags), free(units), free(clips), free(devices);
        }
        void drawPath(Path& path, AffineTransform ctm, bool even, uint8_t *src, size_t iz, Bounds clip, bool hit) {
            if (bitmap.width) {
                if (path.sequence->units)
                    return;
                float w = clip.ux - clip.lx, h = clip.uy - clip.ly, stride = w + 1.f;
                if (stride * h < deltas.size()) {
                    writePath(path, AffineTransform(ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx - clip.lx, ctm.ty - clip.ly), Bounds(0.f, 0.f, w, h), writeDeltaSegment, & deltas[0], stride, nullptr);
                    writeDeltas(& deltas[0], stride, clip, even, src, & bitmap);
                } else {
                    writePath(path, ctm, clip, writeClippedSegment, nullptr, 0, & segments[0]);
                    writeSegments(& segments[0], clip, even, & deltas[0], stride, src, & bitmap);
                }
            } else {
                if (path.sequence->units) {
                    gpu.shapesCount += path.sequence->end;
                    new (gpu.quads.alloc(1)) GPU::Quad(ctm, iz, GPU::Quad::kShapes);
                } else {
                    if (1) {
                        writePath(path, ctm, Bounds(clip.lx - 1.f, clip.ly - 1.f, clip.ux + 1.f, clip.uy + 1.f), writeOutlineSegment, nullptr, 0, & segments[0]);
                        size_t count = segments[0].end - segments[0].idx;
                        if (count) {
                            gpu.outlinesCount += count;
                            new (gpu.quads.alloc(1)) GPU::Quad(0.f, 0.f, 0.f, 0.f, 0, 0, iz, GPU::Quad::kOutlines, 0.f, 0, segments[0].idx, count, 0);
                            segments[0].idx = segments[0].end;
                        }
                    } else {
                        writePath(path, ctm, clip, writeClippedSegment, nullptr, 0, & segments[0]);
                        writeSegments(& segments[0], clip, even, src, iz, hit, & gpu);
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
    };
    
    static void writePath(Path& path, AffineTransform ctm, Bounds clip, Function function, float *deltas, uint32_t stride, Row<Segment> *segments) {
        float sx = FLT_MAX, sy = FLT_MAX, x0 = FLT_MAX, y0 = FLT_MAX, x1, y1, x2, y2, x3, y3;
        bool fs = false, f0 = false, f1, f2, f3;
        for (Sequence::Atom& atom : path.sequence->atoms)
            for (uint8_t index = 0, type = 0xF & atom.types[0]; type != Sequence::Atom::kNull; type = 0xF & (atom.types[index / 2] >> ((index & 1) * 4))) {
                float *p = atom.points + index * 2;
                switch (type) {
                    case Sequence::Atom::kMove:
                        if (sx != FLT_MAX && (sx != x0 || sy != y0)) {
                            if (f0 || fs)
                                writeClippedLine(x0, y0, sx, sy, clip, function, deltas, stride, segments);
                            else
                                (*function)(x0, y0, sx, sy, deltas, stride, segments);
                        }
                        sx = x0 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, sy = y0 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        fs = f0 = x0 < clip.lx || x0 >= clip.ux || y0 < clip.ly || y0 >= clip.uy;
                        index++;
                        break;
                    case Sequence::Atom::kLine:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        f1 = x1 < clip.lx || x1 >= clip.ux || y1 < clip.ly || y1 >= clip.uy;
                        if (f0 || f1)
                            writeClippedLine(x0, y0, x1, y1, clip, function, deltas, stride, segments);
                        else
                            (*function)(x0, y0, x1, y1, deltas, stride, segments);
                        x0 = x1, y0 = y1, f0 = f1;
                        index++;
                        break;
                    case Sequence::Atom::kQuadratic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        f1 = x1 < clip.lx || x1 >= clip.ux || y1 < clip.ly || y1 >= clip.uy;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        f2 = x2 < clip.lx || x2 >= clip.ux || y2 < clip.ly || y2 >= clip.uy;
                        if (f0 || f1 || f2)
                            writeClippedQuadratic(x0, y0, x1, y1, x2, y2, clip, function, deltas, stride, segments);
                        else
                            writeQuadratic(x0, y0, x1, y1, x2, y2, function, deltas, stride, segments);
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
                            writeClippedCubic(x0, y0, x1, y1, x2, y2, x3, y3, clip, function, deltas, stride, segments);
                        else
                            writeCubic(x0, y0, x1, y1, x2, y2, x3, y3, function, deltas, stride, segments);
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
                writeClippedLine(x0, y0, sx, sy, clip, function, deltas, stride, segments);
            else
                (*function)(x0, y0, sx, sy, deltas, stride, segments);
        }
    }
    static void writeClippedLine(float x0, float y0, float x1, float y1, Bounds clip, Function function, float *deltas, uint32_t stride, Row<Segment> *segments) {
        float sy0 = y0 < clip.ly ? clip.ly : y0 > clip.uy ? clip.uy : y0, sy1 = y1 < clip.ly ? clip.ly : y1 > clip.uy ? clip.uy : y1;
        if (sy0 != sy1) {
            float dx = x1 - x0, dy = y1 - y0, ty0 = (sy0 - y0) / dy, ty1 = (sy1 - y0) / dy, tx0, tx1, sx0, sx1, mx, vx;
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
                        (*function)(sx0, sy0, sx1, sy1, deltas, stride, segments);
                    } else {
                        vx = mx < clip.lx ? clip.lx : clip.ux;
                        (*function)(vx, sy0, vx, sy1, deltas, stride, segments);
                    }
                }
        }
    }
    static void solveQuadratic(double A, double B, double C, float& t0, float& t1) {
        double d, r;
        if (fabs(A) < 1e-3)
            t0 = -C / B, t1 = FLT_MAX;
        else {
            d = B * B - 4.0 * A * C;
            if (d < 0)
                t0 = t1 = FLT_MAX;
            else
                r = sqrt(d), t0 = (-B + r) * 0.5 / A, t1 = (-B - r) * 0.5 / A;
        }
        t0 = t0 < 0.f ? 0.f : t0 > 1.f ? 1.f : t0, t1 = t1 < 0.f ? 0.f : t1 > 1.f ? 1.f : t1;
    }
    static void writeClippedQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, Bounds clip, Function function, float *deltas, uint32_t stride, Row<Segment> *segments) {
        float ly, uy, lx, ux, ax, bx, ay, by, ts[8], t, x, y, vx, tx0, ty0, tx1, ty1, tx2, ty2;
        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2;
        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2;
        if (ly < clip.uy && uy > clip.ly) {
            lx = x0 < x1 ? x0 : x1, lx = lx < x2 ? lx : x2;
            ux = x0 > x1 ? x0 : x1, ux = ux > x2 ? ux : x2;
            ax = x0 + x2 - x1 - x1, bx = 2.f * (x1 - x0);
            ay = y0 + y2 - y1 - y1, by = 2.f * (y1 - y0);
            int end = 0;
            if (clip.ly >= ly && clip.ly < uy)
                solveQuadratic(ay, by, y0 - clip.ly, ts[end], ts[end + 1]), end += 2;
            if (clip.uy >= ly && clip.uy < uy)
                solveQuadratic(ay, by, y0 - clip.uy, ts[end], ts[end + 1]), end += 2;
            if (clip.lx >= lx && clip.lx < ux)
                solveQuadratic(ax, bx, x0 - clip.lx, ts[end], ts[end + 1]), end += 2;
            if (clip.ux >= lx && clip.ux < ux)
                solveQuadratic(ax, bx, x0 - clip.ux, ts[end], ts[end + 1]), end += 2;
            if (end < 8)
                ts[end] = 0.f, ts[end + 1] = 1.f, end += 2;
            std::sort(& ts[0], & ts[end]);
            for (int i = 0; i < end - 1; i++)
                if (ts[i] != ts[i + 1]) {
                    t = (ts[i] + ts[i + 1]) * 0.5f;
                    y = (ay * t + by) * t + y0;
                    if (y >= clip.ly && y < clip.uy) {
                        x = (ax * t + bx) * t + x0;
                        bool visible = x >= clip.lx && x < clip.ux;
                        t = ts[i], tx0 = (ax * t + bx) * t + x0, ty0 = (ay * t + by) * t + y0;
                        t = ts[i + 1], tx2 = (ax * t + bx) * t + x0, ty2 = (ay * t + by) * t + y0;
                        tx1 = 2.f * x - 0.5f * (tx0 + tx2), ty1 = 2.f * y - 0.5f * (ty0 + ty2);
                        ty0 = ty0 < clip.ly ? clip.ly : ty0 > clip.uy ? clip.uy : ty0;
                        ty2 = ty2 < clip.ly ? clip.ly : ty2 > clip.uy ? clip.uy : ty2;
                        if (visible) {
                            tx0 = tx0 < clip.lx ? clip.lx : tx0 > clip.ux ? clip.ux : tx0;
                            tx2 = tx2 < clip.lx ? clip.lx : tx2 > clip.ux ? clip.ux : tx2;
                            writeQuadratic(tx0, ty0, tx1, ty1, tx2, ty2, function, deltas, stride, segments);
                       } else {
                            vx = x <= clip.lx ? clip.lx : clip.ux;
                            (*function)(vx, ty0, vx, ty2, deltas, stride, segments);
                        }
                    }
                }
        }
    }
    static void writeQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, Function function, float *deltas, uint32_t stride, Row<Segment> *segments) {
        float ax, ay, a, count, dt, f2x, f1x, f2y, f1y, px0, px1, py0, py1;
        ax = x0 + x2 - x1 - x1, ay = y0 + y2 - y1 - y1, a = ax * ax + ay * ay;
        count = a < 0.1f ? 1.f : a < 8.f ? 2.f : 3.f + floorf(sqrtf(sqrtf(a - 8.f))), dt = 1.f / count;
        ax *= dt * dt, f2x = 2.f * ax, f1x = ax + 2.f * (x1 - x0) * dt;
        ay *= dt * dt, f2y = 2.f * ay, f1y = ay + 2.f * (y1 - y0) * dt;
        px0 = px1 = x0, py0 = py1 = y0;
        while (--count) {
            px1 += f1x, f1x += f2x, py1 += f1y, f1y += f2y;
            (*function)(px0, py0, px1, py1, deltas, stride, segments);
            px0 = px1, py0 = py1;
        }
        (*function)(px0, py0, x2, y2, deltas, stride, segments);
    }
    static void solveCubic(double A, double B, double C, double D, float& t0, float& t1, float& t2) {
        if (fabs(D) < 1e-3)
            solveQuadratic(A, B, C, t0, t1), t2 = FLT_MAX;
        else {
            const double wq0 = 2.0 / 27.0, third = 1.0 / 3.0;
            double  p, q, q2, u1, v1, a3, discriminant, sd;
            A /= D, B /= D, C /= D, p = B - A * A * third, q = A * (wq0 * A * A - third * B) + C;
            q2 = q * 0.5, a3 = A * third;
            discriminant = q2 * q2 + p * p * p / 27.0;
            if (discriminant < 0) {
                double mp3 = -p / 3, mp33 = mp3 * mp3 * mp3, r = sqrt(mp33), t = -q / (2 * r), cosphi = t < -1 ? -1 : t > 1 ? 1 : t;
                double phi = acos(cosphi), crtr = 2 * copysign(cbrt(fabs(r)), r);
                t0 = crtr * cos(phi / 3) - a3, t1 = crtr * cos((phi + 2 * M_PI) / 3) - a3, t2 = crtr * cos((phi + 4 * M_PI) / 3) - a3;
            } else if (discriminant == 0) {
                u1 = copysign(cbrt(fabs(q2)), q2);
                t0 = 2 * u1 - a3, t1 = -u1 - a3, t2 = FLT_MAX;
            } else {
                sd = sqrt(discriminant), u1 = copysign(cbrt(fabs(sd - q2)), sd - q2), v1 = copysign(cbrt(fabs(sd + q2)), sd + q2);
                t0 = u1 - v1 - a3, t1 = t2 = FLT_MAX;
            }
        }
        t0 = t0 < 0.f ? 0.f : t0 > 1.f ? 1.f : t0, t1 = t1 < 0.f ? 0.f : t1 > 1.f ? 1.f : t1, t2 = t2 < 0.f ? 0.f : t2 > 1.f ? 1.f : t2;
    }
    static void writeClippedCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Bounds clip, Function function, float *deltas, uint32_t stride, Row<Segment> *segments) {
        float ly, uy, lx, ux;
        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2, ly = ly < y3 ? ly : y3;
        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2, uy = uy > y3 ? uy : y3;
        if (ly < clip.uy && uy > clip.ly) {
            float cy, by, ay, cx, bx, ax, ts[12], t, x, y, vx, tx0, ty0, tx1, ty1, tx2, ty2, tx3, ty3, fx, gx, fy, gy;
            lx = x0 < x1 ? x0 : x1, lx = lx < x2 ? lx : x2, lx = lx < x3 ? lx : x3;
            ux = x0 > x1 ? x0 : x1, ux = ux > x2 ? ux : x2, ux = ux > x3 ? ux : x3;
            cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
            cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
            int end = 0;
            if (clip.ly >= ly && clip.ly < uy)
                solveCubic(by, cy, y0 - clip.ly, ay, ts[end], ts[end + 1], ts[end + 2]), end += 3;
            if (clip.uy >= ly && clip.uy < uy)
                solveCubic(by, cy, y0 - clip.uy, ay, ts[end], ts[end + 1], ts[end + 2]), end += 3;
            if (clip.lx >= lx && clip.lx < ux)
                solveCubic(bx, cx, x0 - clip.lx, ax, ts[end], ts[end + 1], ts[end + 2]), end += 3;
            if (clip.ux >= lx && clip.ux < ux)
                solveCubic(bx, cx, x0 - clip.ux, ax, ts[end], ts[end + 1], ts[end + 2]), end += 3;
            if (end < 12)
                ts[end] = 0.f, ts[end + 1] = 1.f, end += 2;
            std::sort(& ts[0], & ts[end]);
            for (int i = 0; i < end - 1; i++)
                if (ts[i] != ts[i + 1]) {
                    t = (ts[i] + ts[i + 1]) * 0.5f;
                    y = ((ay * t + by) * t + cy) * t + y0;
                    if (y >= clip.ly && y < clip.uy) {
                        x = ((ax * t + bx) * t + cx) * t + x0;
                        bool visible = x >= clip.lx && x < clip.ux;
                        t = ts[i];
                        tx0 = ((ax * t + bx) * t + cx) * t + x0, ty0 = ((ay * t + by) * t + cy) * t + y0;
                        t = ts[i + 1];
                        tx3 = ((ax * t + bx) * t + cx) * t + x0, ty3 = ((ay * t + by) * t + cy) * t + y0;
                        ty0 = ty0 < clip.ly ? clip.ly : ty0 > clip.uy ? clip.uy : ty0;
                        ty3 = ty3 < clip.ly ? clip.ly : ty3 > clip.uy ? clip.uy : ty3;
                        if (visible) {
                            const float u = 1.f / 3.f, v = 2.f / 3.f, u3 = 1.f / 27.f, v3 = 8.f / 27.f, m0 = 3.f, m1 = 1.5f;
                            t = v * ts[i] + u * ts[i + 1];
                            tx1 = ((ax * t + bx) * t + cx) * t + x0, ty1 = ((ay * t + by) * t + cy) * t + y0;
                            t = u * ts[i] + v * ts[i + 1];
                            tx2 = ((ax * t + bx) * t + cx) * t + x0, ty2 = ((ay * t + by) * t + cy) * t + y0;
                            fx = tx1 - v3 * tx0 - u3 * tx3, fy = ty1 - v3 * ty0 - u3 * ty3;
                            gx = tx2 - u3 * tx0 - v3 * tx3, gy = ty2 - u3 * ty0 - v3 * ty3;
                            tx1 = fx * m0 + gx * -m1, ty1 = fy * m0 + gy * -m1;
                            tx2 = fx * -m1 + gx * m0, ty2 = fy * -m1 + gy * m0;
                            tx0 = tx0 < clip.lx ? clip.lx : tx0 > clip.ux ? clip.ux : tx0;
                            tx3 = tx3 < clip.lx ? clip.lx : tx3 > clip.ux ? clip.ux : tx3;
                            writeCubic(tx0, ty0, tx1, ty1, tx2, ty2, tx3, ty3, function, deltas, stride, segments);
                        } else {
                            vx = x <= clip.lx ? clip.lx : clip.ux;
                            (*function)(vx, ty0, vx, ty3, deltas, stride, segments);
                        }
                    }
                }
        }
    }
    static void writeCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Function function, float *deltas, uint32_t stride, Row<Segment> *segments) {
        float cx, bx, ax, cy, by, ay, s, t, a, count, dt, dt2, f3x, f2x, f1x, f3y, f2y, f1y, px0, px1, py0, py1;
        cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
        cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
        s = fabsf(ax) + fabsf(bx), t = fabsf(ay) + fabsf(by), a = s * s + t * t;
        count = a < 0.1f ? 1.f : a < 16.f ? 3.f : 4.f + floorf(sqrtf(sqrtf(a - 16.f)));
        dt = 1.f / count, dt2 = dt * dt;
        bx *= dt2, ax *= dt2 * dt, f3x = 6.f * ax, f2x = f3x + 2.f * bx, f1x = ax + bx + cx * dt;
        by *= dt2, ay *= dt2 * dt, f3y = 6.f * ay, f2y = f3y + 2.f * by, f1y = ay + by + cy * dt;
        px0 = px1 = x0, py0 = py1 = y0;
        while (--count) {
            px1 += f1x, f1x += f2x, f2x += f3x, py1 += f1y, f1y += f2y, f2y += f3y;
            (*function)(px0, py0, px1, py1, deltas, stride, segments);
            px0 = px1, py0 = py1;
        }
        (*function)(px0, py0, x3, y3, deltas, stride, segments);
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
        size_t ily = floorf(clip.ly * Context::krfh), iuy = ceilf(clip.uy * Context::krfh), iy, count, i, begin;
        short counts0[256], counts1[256];
        float ly, uy, scale, cover, winding, lx, ux, x, ox, oy;
        Segment *segment;
        Row<Segment::Index>& indices = gpu->indices;    Segment::Index *index;
        for (segments += ily, iy = ily; iy < iuy; iy++, segments++) {
            ly = iy * Context::kfh, ly = ly < clip.ly ? clip.ly : ly > clip.uy ? clip.uy : ly;
            uy = (iy + 1) * Context::kfh, uy = uy < clip.ly ? clip.ly : uy > clip.uy ? clip.uy : uy;
            count = segments->end - segments->idx;
            if (count) {
                if (clip.ux - clip.lx < 32.f) {
                    gpu->allocator.alloc(clip.ux - clip.lx, ox, oy);
                    new (gpu->quads.alloc(1)) GPU::Quad(clip.lx, ly, clip.ux, uy, ox, oy, iz, GPU::Quad::kCell, 0.f, iy, segments->idx, 0xFFFFFF, count);
                    gpu->edgeInstances += (count + kSegmentsCount - 1) / kSegmentsCount;
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
                                gpu->allocator.alloc(ux - lx, ox, oy);
                                new (gpu->quads.alloc(1)) GPU::Quad(lx, ly, ux, uy, ox, oy, iz, GPU::Quad::kCell, cover, iy, segments->idx, begin, i - begin);
                                gpu->edgeInstances += (i - begin + kSegmentsCount - 1) / kSegmentsCount;
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
                        gpu->allocator.alloc(ux - lx, ox, oy);
                        new (gpu->quads.alloc(1)) GPU::Quad(lx, ly, ux, uy, ox, oy, iz, GPU::Quad::kCell, cover, iy, segments->idx, begin, i - begin);
                        gpu->edgeInstances += (i - begin + kSegmentsCount - 1) / kSegmentsCount;
                    }
                }
                indices.idx = indices.end;
                segments->idx = segments->end;
            }
        }
    }
    static void writeSegments(Row<Segment> *segments, Bounds clip, bool even, float *deltas, uint32_t stride, uint8_t *src, Bitmap *bitmap) {
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
                        writeDeltas(deltas, stride, Bounds(lx, ly, ux, uy), even, src, bitmap);
                        lx = ux, ux = index->x;
                        if (alphaForCover(cover, even) > 0.998f)
                            for (delta = deltas, y = ly; y < uy; y++, delta += stride) {
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
                    writeDeltaSegment(segment->x0 - lx, segment->y0 - ly, segment->x1 - lx, segment->y1 - ly, deltas, stride, nullptr);
                }
                writeDeltas(deltas, stride, Bounds(lx, ly, ux, uy), even, src, bitmap);
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
