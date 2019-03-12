//
//  Rasterizer.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 17/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "Rasterizer.h"
#import "crc64.h"
#import <vector>
#pragma clang diagnostic ignored "-Wcomma"

struct Rasterizer {
    struct Transform {
        Transform() {}
        Transform(float a, float b, float c, float d, float tx, float ty) : a(a), b(b), c(c), d(d), tx(tx), ty(ty) {}
        static Transform nullclip() { return { 1e12f, 0.f, 0.f, 1e12f, -5e11f, -5e11f }; }
        inline Transform concat(Transform t) const {
            return {
                t.a * a + t.b * c, t.a * b + t.b * d,
                t.c * a + t.d * c, t.c * b + t.d * d,
                t.tx * a + t.ty * c + tx, t.tx * b + t.ty * d + ty
            };
        }
        inline Transform invert() const {
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
        Bounds(Transform t) {
            lx = t.tx + (t.a < 0.f ? t.a : 0.f) + (t.c < 0.f ? t.c : 0.f), ly = t.ty + (t.b < 0.f ? t.b : 0.f) + (t.d < 0.f ? t.d : 0.f);
            ux = t.tx + (t.a > 0.f ? t.a : 0.f) + (t.c > 0.f ? t.c : 0.f), uy = t.ty + (t.b > 0.f ? t.b : 0.f) + (t.d > 0.f ? t.d : 0.f);
        }
        inline void extend(float x, float y) {
            lx = lx < x ? lx : x, ux = ux > x ? ux : x, ly = ly < y ? ly : y, uy = uy > y ? uy : y;
        }
        Bounds integral() const { return { floorf(lx), floorf(ly), ceilf(ux), ceilf(uy) }; }
        inline Bounds intersect(Bounds b) const {
            return {
                lx < b.lx ? b.lx : lx > b.ux ? b.ux : lx, ly < b.ly ? b.ly : ly > b.uy ? b.uy : ly,
                ux < b.lx ? b.lx : ux > b.ux ? b.ux : ux, uy < b.ly ? b.ly : uy > b.uy ? b.uy : uy
            };
        }
        inline Bounds transform(Transform t) const {
            return Bounds(unit(t));
        }
        inline Transform unit(Transform t) const {
            return { t.a * (ux - lx), t.b * (ux - lx), t.c * (uy - ly), t.d * (uy - ly), lx * t.a + ly * t.c + t.tx, lx * t.b + ly * t.d + t.ty };
        }
        float lx, ly, ux, uy;
    };
    struct Colorant {
        Colorant() {}
        Colorant(uint8_t *src) : src0(src[0]), src1(src[1]), src2(src[2]), src3(src[3]) {}
        uint8_t src0, src1, src2, src3;
    };
    struct Geometry {
        struct Atom {
            enum Type { kNull = 0, kMove, kLine, kQuadratic, kCubic, kClose, kCapacity = 15 };
            Atom() { bzero(points, sizeof(points)), bzero(types, sizeof(types)); }
            float       points[30];
            uint8_t     types[8];
        };
        Geometry() : end(Atom::kCapacity), atomsCount(0), shapesCount(0), px(0), py(0), bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX), shapes(nullptr), circles(nullptr), isGlyph(false), refCount(0), hash(0) { bzero(counts, sizeof(counts)); }
        ~Geometry() { if (shapes) free(shapes), free(circles); }
        
        float *alloc(Atom::Type type, size_t size) {
            if (end + size > Atom::kCapacity)
                end = 0, atoms.emplace_back();
            atoms.back().types[end / 2] |= (uint8_t(type) << ((end & 1) * 4));
            end += size;
            return atoms.back().points + (end - size) * 2;
        }
        void update(Atom::Type type, size_t size, float *p) {
            atomsCount += size, counts[type]++, hash = ::crc64(::crc64(hash, & type, sizeof(type)), p, size * 2 * sizeof(float));
            if (type == Atom::kMove)
                molecules.emplace_back(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
            while (size--)
                bounds.extend(p[0], p[1]), molecules.back().extend(p[0], p[1]), p += 2;
        }
        void addShapes(size_t count) {
            shapesCount = count, bounds = Bounds(-5e11f, -5e11f, 5e11f, 5e11f);
            shapes = (Transform *)calloc(count, sizeof(shapes[0])), circles = (bool *)calloc(count, sizeof(bool));
        }
        void addBounds(Bounds b) { moveTo(b.lx, b.ly), lineTo(b.ux, b.ly), lineTo(b.ux, b.uy), lineTo(b.lx, b.uy), close(); }
        
        void moveTo(float x, float y) {
            float *points = alloc(Atom::kMove, 1);
            px = points[0] = x, py = points[1] = y;
            update(Atom::kMove, 1, points);
        }
        void lineTo(float x, float y) {
            if (px != x || py != y) {
                float *points = alloc(Atom::kLine, 1);
                px = points[0] = x, py = points[1] = y;
                update(Atom::kLine, 1, points);
            }
        }
        void quadTo(float cx, float cy, float x, float y) {
            float ax = cx - px, ay = cy - py, bx = x - cx, by = y - cy, det = ax * by - ay * bx, dot = ax * bx + ay * by;
            if (fabsf(det) < 1e-4f) {
                if (dot < 0.f && det)
                    lineTo((px + x) * 0.25f + cx * 0.5f, (py + y) * 0.25f + cy * 0.5f);
                lineTo(x, y);
            } else {
                float *points = alloc(Atom::kQuadratic, 2);
                points[0] = cx, points[1] = cy, px = points[2] = x, py = points[3] = y;
                update(Atom::kQuadratic, 2, points);
            }
        }
        void cubicTo(float cx0, float cy0, float cx1, float cy1, float x, float y) {
            float dx = 3.f * (cx0 - cx1) - px + x, dy = 3.f * (cy0 - cy1) - py + y;
            if (dx * dx + dy * dy < 1e-4f)
                quadTo((3.f * (cx0 + cx1) - px - x) * 0.25f, (3.f * (cy0 + cy1) - py - y) * 0.25f, x, y);
            else {
                float *points = alloc(Atom::kCubic, 3);
                points[0] = cx0, points[1] = cy0, points[2] = cx1, points[3] = cy1, px = points[4] = x, py = points[5] = y;
                update(Atom::kCubic, 3, points);
            }
        }
        void close() {
            update(Atom::kClose, 1, alloc(Atom::kClose, 1));
        }
        size_t refCount, atomsCount, shapesCount, hash, end, counts[Atom::kClose + 1];
        std::vector<Atom> atoms;
        std::vector<Bounds> molecules;
        float px, py;
        Transform *shapes;
        bool *circles, isGlyph;
        Bounds bounds;
    };
    template<typename T>
    struct Ref {
        Ref()                               { ref = new T(), ref->refCount = 1; }
        ~Ref()                              { if (--(ref->refCount) == 0) delete ref; }
        Ref(const Ref& other)               { *this = other; }
        Ref& operator= (const Ref& other)   {
            if (this != & other) {
                if (ref)
                    this->~Ref();
                ref = other.ref, ref->refCount++;
            }
            return *this;
        }
        T *ref = nullptr;
    };
    typedef Ref<Geometry> Path;
    
    template<typename T>
    struct Memory {
        ~Memory() { if (addr) free(addr); }
        void resize(size_t n) { size = n, addr = (T *)realloc(addr, size * sizeof(T)); }
        size_t refCount = 0, size = 0;
        T *addr = nullptr;
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
                this->~Pages();
                posix_memalign((void **)& base, kPageSize, size);
            }
            return base;
        }
        size_t size;
        T *base;
    };
    struct Segment {
        Segment(float x0, float y0, float x1, float y1) : x0(x0), y0(y0), x1(x1), y1(y1) {}
        float x0, y0, x1, y1;
    };
    struct Info {
        Info(void *info) : info(info), stride(0) {}
        Info(float *deltas, uint32_t stride) : deltas(deltas), stride(stride) {}
        Info(Row<Segment> *segments) : segments(segments), stride(0) {}
        union { void *info;  float *deltas;  Row<Segment> *segments; };
        uint32_t stride;
    };
    struct Cache {
        static constexpr int kGridSize = 4096, kGridMask = kGridSize - 1, kChunkSize = 4;
        struct Entry {
            Entry() {}
            Entry(Transform ctm) : ctm(ctm) {}
            int begin, end;
            Transform ctm;
        };
        struct Chunk {
            Entry entries[kChunkSize];
            size_t hashes[kChunkSize];
            uint32_t end = 0, next = 0;
        };
        struct Grid {
            Grid() { empty(); }
            void empty() { chunks.empty(), chunks.alloc(1), bzero(grid, sizeof(grid)); }
            Chunk *chunk(size_t hash) {
                uint16_t& idx = grid[hash & kGridMask];
                if (idx == 0)
                    idx = chunks.end, new (chunks.alloc(1)) Chunk();
                return chunks.base + idx;
            }
            Entry *alloc(Chunk *chunk, size_t hash) {
                if (chunk->end == kChunkSize) {
                    uint16_t& idx = grid[chunk->hashes[0] & kGridMask], next = idx;
                    idx = chunks.end, chunk = new (chunks.alloc(1)) Chunk(), chunk->next = uint32_t(next);
                }
                chunk->hashes[chunk->end] = hash;
                return chunk->entries + chunk->end++;
            }
            Entry *find(Chunk *chunk, size_t hash) {
                do {
                    for (int i = 0; i < chunk->end; i++)
                        if (chunk->hashes[i] == hash)
                            return & chunk->entries[i];
                } while (chunk->next && (chunk = chunks.base + chunk->next));
                return nullptr;
            }
            Row<Chunk> chunks;
            uint16_t grid[kGridSize];
        };
        void empty() { grid->empty(), segments.empty(); }
        
        Entry *getPath(Path& path, Transform ctm, Transform *m) {
            Chunk *chunk = grid->chunk(path.ref->hash);
            Entry *e = nullptr, *srch = chunk->end ? grid->find(chunk, path.ref->hash) : nullptr;
            if (srch == nullptr) {
                e = new (grid->alloc(chunk, path.ref->hash)) Entry(ctm.invert());
                e->begin = int(segments.idx);
                writePath(path, ctm, Bounds(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX), writeOutlineSegment, Info(& segments));
                segments.idx = e->end = int(segments.end);
            } else {
                *m = ctm.concat(srch->ctm);
                if (m->a == m->d && m->b == -m->c && fabsf(m->a * m->a + m->b * m->b - 1.f) < 1e-6f)
                    e = srch;
            }
            return e;
        }
        void writeCachedOutline(Entry *e, Transform m, Info info) {
            float x0, y0, x1, y1, iy0, iy1;
            Segment *s = segments.base + e->begin, *end = segments.base + e->end;
            for (x0 = s->x0 * m.a + s->y0 * m.c + m.tx, y0 = s->x0 * m.b + s->y0 * m.d + m.ty, iy0 = floorf(y0 * krfh); s < end; s++, x0 = x1, y0 = y1, iy0 = iy1) {
                x1 = s->x1 * m.a + s->y1 * m.c + m.tx, y1 = s->x1 * m.b + s->y1 * m.d + m.ty, iy1 = floorf(y1 * krfh);
                if (s->x0 != FLT_MAX) {
                    if (iy0 == iy1 && y0 != y1)
                        new (info.segments[size_t(iy0)].alloc(1)) Segment(x0, y0, x1, y1);
                    else
                        writeClippedSegment(x0, y0, x1, y1, & info);
                } else if (s < end - 1)
                    x1 = (s + 1)->x0 * m.a + (s + 1)->y0 * m.c + m.tx, y1 = (s + 1)->x0 * m.b + (s + 1)->y0 * m.d + m.ty, iy1 = floorf(y1 * krfh);
            }
        }
        Grid grid0, *grid = & grid0;
        Row<Segment> segments;
    };
    struct Index {
        Index(uint16_t x, uint16_t i) : x(x), i(i) {}
        uint16_t x, i;
        inline bool operator< (const Index& other) const { return x < other.x; }
    };
    struct GPU {
        struct Allocator {
            struct Pass {
                Pass(size_t idx) : li(idx), ui(idx) {}
                size_t cells = 0, edgeInstances = 0, fastInstances = 0, li, ui;
            };
            void init(size_t w, size_t h) {
                width = w, height = h, sheet = strip = fast = molecules = Bounds(0.f, 0.f, 0.f, 0.f), passes.empty();
            }
            void alloc(float w, float h, size_t idx, size_t cells, size_t instances, bool isFast, float& ox, float& oy) {
                Pass *pass = passes.end ? & passes.base[passes.end - 1] : new (passes.alloc(1)) Pass(0);
                Bounds *b = & strip;
                float hght = kfh;
                if (h > kfh) {
                    if (h > kFastHeight)
                        hght = kMoleculesHeight, b = & molecules;
                    else
                        hght = kFastHeight, b = & fast;
                }
                if (b->ux - b->lx < w) {
                    if (sheet.uy - sheet.ly < hght) {
                        pass = sheet.ux == 0.f ? pass : new (passes.alloc(1)) Pass(idx);
                        sheet = Bounds(0.f, 0.f, width, height), strip = fast = molecules = Bounds(0.f, 0.f, 0.f, 0.f);
                    }
                    b->lx = sheet.lx, b->ly = sheet.ly, b->ux = sheet.ux, b->uy = sheet.ly + hght, sheet.ly = b->uy;
                }
                ox = b->lx, b->lx += w, oy = b->ly;
                pass->cells += cells, pass->ui++, pass->fastInstances += isFast * instances, pass->edgeInstances += !isFast * instances;
            }
            inline void countQuad() {
                Pass *pass = passes.end ? & passes.base[passes.end - 1] : new (passes.alloc(1)) Pass(0);
                pass->ui++;
            }
            size_t width, height;
            Row<Pass> passes;
            Bounds sheet, strip, fast, molecules;
        };
        struct Molecules {
            struct Range {
                Range(size_t begin, size_t end) : begin(int(begin)), end(int(end)) {}
                int begin, end;
            };
            struct Cell {
                Cell(float ux, size_t begin, size_t end) : ux(ux), begin(int(begin)), end(int(end)) {}
                float ux;
                int begin, end;
            };
            Cell *alloc(size_t size) {
                new (ranges.alloc(1)) Range(cells.idx, cells.idx + size), cells.idx += size;
                return cells.alloc(size);
            }
            void empty() { ranges.empty(), cells.empty(); }
            Row<Range> ranges;
            Row<Cell> cells;
        };
        struct Cell {
            Cell(float lx, float ly, float ux, float uy, float ox, float oy) : lx(lx), ly(ly), ux(ux), uy(uy), ox(ox), oy(oy) {}
            short lx, ly, ux, uy, ox, oy;
        };
        struct SuperCell {
            SuperCell(float lx, float ly, float ux, float uy, float ox, float oy, float cover, int iy, int end, int begin, size_t count) : cell(lx, ly, ux, uy, ox, oy), cover(short(roundf(cover))), count(uint16_t(count)), iy(iy), begin(begin), end(end) {}
            Cell cell;
            short cover;
            uint16_t count;
            int iy, begin, end;
        };
        struct Outline {
            Outline(Segment *s, float width) : s(*s), width(width), prev(-1), next(1) {}
            Segment s;
            float width;
            short prev, next;
        };
        struct Quad {
            enum Type { kRect = 1 << 24, kCircle = 1 << 25, kEdge = 1 << 26, kSolidCell = 1 << 27, kShapes = 1 << 28, kOutlines = 1 << 29, kOpaque = 1 << 30, kMolecule = 1 << 31 };
            Quad(float lx, float ly, float ux, float uy, float ox, float oy, size_t iz, int type, float cover, int iy, int end, int begin, size_t count) : super(lx, ly, ux, uy, ox, oy, cover, iy, end, begin, count), iz((uint32_t)iz | type) {}
            Quad(Transform unit, size_t iz, int type) : unit(unit), iz((uint32_t)iz | type) {}
            Quad(Segment *s, float width, size_t iz, int type) : outline(s, width), iz((uint32_t)iz | type) {}
            union {
                SuperCell super;
                Transform unit;
                Outline outline;
            };
            uint32_t iz;
        };
        struct EdgeCell {
            Cell cell;
            int im, base;
        };
        struct Edge {
            int ic;
            uint16_t i0, i1;
        };
        GPU() { empty(); }
        void empty() {
            shapesCount = shapePaths = outlinePaths = 0, indices.empty(), quads.empty(), opaques.empty(), outlines.empty(), ctms = nullptr, molecules.empty();
        }
        size_t shapesCount, shapePaths, outlinePaths;
        Allocator allocator;
        Molecules molecules;
        Row<Index> indices;
        Row<Quad> quads, opaques;
        Row<Segment> outlines;
        Transform *ctms;
        Cache cache;
    };
    typedef void (*Function)(float x0, float y0, float x1, float y1, Info *info);
    static void writeOutlineSegment(float x0, float y0, float x1, float y1, Info *info) {
        new (info->segments->alloc(1)) Segment(x0, y0, x1, y1);
    }
    static void writeClippedSegment(float x0, float y0, float x1, float y1, Info *info) {
        if (y0 == y1)
            return;
        float iy0 = y0 * krfh, iy1 = y1 * krfh;
        if (floorf(iy0) == floorf(iy1))
            new (info->segments[size_t(iy0)].alloc(1)) Segment(x0, y0, x1, y1);
        else {
            float sy1, dy, sx1, dx;
            int s, ds, count;
            if (y0 < y1)
                iy0 = floorf(iy0), iy1 = ceilf(iy1), sy1 = iy0 * kfh, dy = kfh, s = iy0, ds = 1;
            else
                iy0 = floorf(iy1), iy1 = ceilf(y0 * krfh), sy1 = iy1 * kfh, dy = -kfh, s = iy1 - 1.f, ds = -1;
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
    struct Bitmap {
        Bitmap() : data(nullptr), width(0), height(0), stride(0), bpp(0), bytespp(0) {}
        Bitmap(void *data, size_t width, size_t height, size_t stride, size_t bpp) : data((uint8_t *)data), width(width), height(height), stride(stride), bpp(bpp), bytespp(bpp / 8) {}
        void clear(uint8_t *src) { memset_pattern4(data, src, stride * height); }
        inline uint8_t *pixelAddress(short x, short y) { return data + stride * (height - 1 - y) + x * bytespp; }
        uint8_t *data;
        size_t width, height, stride, bpp, bytespp;
    };
    struct Context {
        Context() {}
        void setBitmap(Bitmap bm, Bounds cl) {
            bitmap = bm;
            bounds = Bounds(0.f, 0.f, bm.width, bm.height).intersect(cl.integral());
            size_t size = ceilf(float(bm.height) * krfh);
            if (segments.size() != size)
                segments.resize(size);
            deltas.empty(), deltas.alloc((bm.width + 1) * kfh);
            memset(deltas.base, 0, deltas.end * sizeof(*deltas.base));
        }
        void setGPU(size_t width, size_t height, Transform *ctms) {
            bitmap = Bitmap();
            bounds = Bounds(0.f, 0.f, width, height);
            size_t size = ceilf(float(height) * krfh);
            if (segments.size() != size)
                segments.resize(size);
            gpu.allocator.init(width, height), gpu.ctms = ctms;
            gpu.cache.empty();
        }
        void drawPaths(Path *paths, Transform *ctms, bool even, Colorant *colors, Transform *clips, float width, size_t begin, size_t end) {
            if (begin == end)
                return;
            size_t iz;
            Transform inv = Transform::nullclip().invert(), t = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
            Bounds device = bounds;
            for (paths += begin, ctms += begin, clips += begin, iz = begin; iz < end; iz++, paths++, ctms++, clips++)
                if ((bitmap.width == 0 && paths->ref->shapesCount) || paths->ref->atomsCount > 2) {
                    if (memcmp(clips, & t, sizeof(Transform)))
                        device = Bounds(*clips).integral().intersect(bounds), inv = bitmap.width ? inv : clips->invert(), t = *clips;
                    Transform unit = paths->ref->bounds.unit(*ctms);
                    Bounds dev = Bounds(unit).integral(), clip = dev.intersect(device);
                    bool unclipped = dev.lx == clip.lx && dev.ly == clip.ly && dev.ux == clip.ux && dev.uy == clip.uy;
                    if (clip.lx != clip.ux && clip.ly != clip.uy) {
                        Bounds clu = Bounds(inv.concat(unit));
                        if (clu.ux >= 0.f && clu.lx < 1.f && clu.uy >= 0.f && clu.ly < 1.f) {
                            if (bitmap.width)
                                writeBitmapPath(*paths, *ctms, even, & colors[iz].src0, clip, Info(& segments[0]), deltas.base, deltas.end, & bitmap);
                            else
                                writeGPUPath(*paths, *ctms, even, & colors[iz].src0, iz, unclipped, clip, clu.lx < 0.f || clu.ux > 1.f || clu.ly < 0.f || clu.uy > 1.f, width, Info(& segments[0]), gpu);
                        }
                    }
                }
        }
        GPU gpu;
        Bitmap bitmap;
        Bounds bounds;
        Row<float> deltas;
        std::vector<Row<Segment>> segments;
    };
    static void writeBitmapPath(Path& path, Transform ctm, bool even, uint8_t *src, Bounds clip, Info sgmnts, float *deltas, size_t deltasSize, Bitmap *bm) {
        float w = clip.ux - clip.lx, h = clip.uy - clip.ly, stride = w + 1.f;
        if (stride * h < deltasSize) {
            writePath(path, Transform(ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx - clip.lx, ctm.ty - clip.ly), Bounds(0.f, 0.f, w, h), writeDeltaSegment, Info(deltas, stride));
            writeDeltas(Info(deltas, stride), clip, even, src, bm);
        } else {
            writePath(path, ctm, clip, writeClippedSegment, sgmnts);
            writeSegments(sgmnts.segments, clip, even, Info(deltas, stride), src, bm);
        }
    }
    static void writeGPUPath(Path& path, Transform ctm, bool even, uint8_t *src, size_t iz, bool unclipped, Bounds clip, bool hit, float width, Info segments, GPU& gpu) {
        if (path.ref->shapes) {
            gpu.shapePaths++, gpu.shapesCount += path.ref->shapesCount;
            new (gpu.quads.alloc(1)) GPU::Quad(ctm, iz, GPU::Quad::kShapes);
            gpu.allocator.countQuad();
        } else {
            if (width) {
                gpu.outlinePaths++;
                writePath(path, ctm, Bounds(clip.lx - width, clip.ly - width, clip.ux + width, clip.uy + width), writeOutlineSegment, Info(& gpu.outlines));
                Segment params(float(gpu.outlines.idx), float(gpu.outlines.end), 0.f, 0.f);
                new (gpu.quads.alloc(1)) GPU::Quad(& params, width, iz, GPU::Quad::kOutlines);
                gpu.outlines.idx = gpu.outlines.end;
                gpu.allocator.countQuad();
            } else {
                Cache::Entry *entry = nullptr;
                Transform m = { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f };
                bool molecules = path.ref->atomsCount > 256 && path.ref->molecules.size() > 1;
                bool fast = clip.uy - clip.ly <= kFastHeight && clip.ux - clip.lx <= kFastHeight;
                bool slow = (!fast && !molecules) || (clip.uy - clip.ly > kMoleculesHeight || clip.ux - clip.lx > kMoleculesHeight);
                if (!slow || (path.ref->isGlyph && unclipped))
                    entry = gpu.cache.getPath(path, ctm, & m);
                if (entry == nullptr)
                    writePath(path, ctm, clip, writeClippedSegment, segments);
                else if (slow)
                    gpu.cache.writeCachedOutline(entry, m, segments);
                if (entry && !slow) {
                    size_t midx = 0, count = entry->end - entry->begin, cells = 1, instances = (count + kFastSegments - 1) / kFastSegments;
                    gpu.ctms[iz] = m;
                    if (molecules) {
                        cells = path.ref->molecules.size(), instances = 0;
                        midx = gpu.molecules.ranges.end;
                        GPU::Molecules::Cell *data = gpu.molecules.alloc(cells);
                        Bounds *molecule = & path.ref->molecules[0];
                        for (Segment *ls = gpu.cache.segments.base + entry->begin, *us = ls + count, *s = ls, *is = ls; s < us; s++)
                            if (s->x0 == FLT_MAX) {
                                float ux = ceilf(molecule->transform(ctm).ux);
                                ux = ux < clip.lx ? clip.lx : ux;
                                ux = ux > clip.ux ? clip.ux : ux;
                                new (data) GPU::Molecules::Cell(ux, is - ls, s - ls);
                                instances += (s - is + kFastSegments - 1) / kFastSegments;
                                is = s + 1, molecule++, data++;
                            }
                    }
                    writeEdges(clip.lx, clip.ly, clip.ux, clip.uy, iz, cells, instances, true, molecules ? GPU::Quad::kMolecule : GPU::Quad::kEdge, 0.f, -int(iz + 1), entry->begin, int(midx), count, gpu);
                } else
                    writeSegments(segments.segments, clip, even, iz, src[3] == 255 && !hit, gpu);
            }
        }
    }
    static void writeEdges(float lx, float ly, float ux, float uy, size_t iz, size_t cells, size_t instances, bool fast, int type, float cover, int iy, int end, int begin, size_t count, GPU& gpu) {
        float ox, oy;
        gpu.allocator.alloc(ux - lx, uy - ly, gpu.quads.end, cells, instances, fast, ox, oy);
        new (gpu.quads.alloc(1)) GPU::Quad(lx, ly, ux, uy, ox, oy, iz, type, cover, iy, end, begin, count);
    }
    static void writePath(Path& path, Transform ctm, Bounds clip, Function function, Info info) {
        float sx = FLT_MAX, sy = FLT_MAX, x0 = FLT_MAX, y0 = FLT_MAX, x1, y1, x2, y2, x3, y3;
        bool fs = false, f0 = false, f1, f2, f3;
        for (Geometry::Atom& atom : path.ref->atoms)
            for (uint8_t index = 0, type = 0xF & atom.types[0]; type != Geometry::Atom::kNull; type = 0xF & (atom.types[index / 2] >> ((index & 1) * 4))) {
                float *p = atom.points + index * 2;
                switch (type) {
                    case Geometry::Atom::kMove:
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
                    case Geometry::Atom::kLine:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        f1 = x1 < clip.lx || x1 >= clip.ux || y1 < clip.ly || y1 >= clip.uy;
                        if (f0 || f1)
                            writeClippedLine(x0, y0, x1, y1, clip, function, & info);
                        else
                            (*function)(x0, y0, x1, y1, & info);
                        x0 = x1, y0 = y1, f0 = f1;
                        index++;
                        break;
                    case Geometry::Atom::kQuadratic:
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
                    case Geometry::Atom::kCubic:
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
                    case Geometry::Atom::kClose:
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
            if (dy == 0.f)
                ty0 = 0.f, ty1 = 1.f;
            else
                ty0 = (sy0 - y0) / dy, ty1 = (sy1 - y0) / dy;
            if (dx == 0.f)
                tx0 = 0.f, tx1 = 1.f;
            else {
                tx0 = (clip.lx - x0) / dx, tx0 = tx0 < ty0 ? ty0 : tx0 > ty1 ? ty1 : tx0;
                tx1 = (clip.ux - x0) / dx, tx1 = tx1 < ty0 ? ty0 : tx1 > ty1 ? ty1 : tx1;
            }
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
                    t = (t0 + t1) * 0.5f, y = (ay * t + by) * t + y0;
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
                    t = (t0 + t1) * 0.5f, y = ((ay * t + by) * t + cy) * t + y0;
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
    static void radixSort(uint32_t *in, int n, uint32_t bias, uint32_t range, bool single, uint16_t *counts) {
        range = range < 4 ? 4 : range;
        uint32_t tmp[n], mask = range - 1;
        memset(counts, 0, sizeof(uint16_t) * range);
        for (int i = 0; i < n; i++)
            counts[(in[i] - bias) & mask]++;
        uint64_t *sums = (uint64_t *)counts, sum = 0, count;
        for (int i = 0; i < range / 4; i++) {
            count = sums[i], sum += count + (count << 16) + (count << 32) + (count << 48), sums[i] = sum;
            sum = sum & 0xFFFF000000000000, sum = sum | (sum >> 16) | (sum >> 32) | (sum >> 48);
        }
        for (int i = n - 1; i >= 0; i--)
            tmp[--counts[(in[i] - bias) & mask]] = in[i];
        if (single)
            memcpy(in, tmp, n * sizeof(uint32_t));
        else {
            memset(counts, 0, sizeof(uint16_t) * 64);
            for (int i = 0; i < n; i++)
                counts[(in[i] >> 8) & 0x3F]++;
            for (uint16_t *src = counts, *dst = src + 1, i = 1; i < 64; i++)
                *dst++ += *src++;
            for (int i = n - 1; i >= 0; i--)
                in[--counts[(tmp[i] >> 8) & 0x3F]] = tmp[i];
        }
    }
    static void writeSegments(Row<Segment> *segments, Bounds clip, bool even, size_t iz, bool opaque, GPU& gpu) {
        size_t ily = floorf(clip.ly * krfh), iuy = ceilf(clip.uy * krfh), count, i, begin;
        uint16_t counts[256], iy;
        float ly, uy, scale, cover, winding, lx, ux, x;
        bool single = clip.ux - clip.lx < 256.f;
        uint32_t range = single ? powf(2.f, ceilf(log2f(clip.ux - clip.lx + 1.f))) : 256;
        Segment *segment;
        Row<Index>& indices = gpu.indices;    Index *index;
        for (segments += ily, iy = ily; iy < iuy; iy++, indices.idx = indices.end, segments->idx = segments->end, segments++)
            if ((count = segments->end - segments->idx)) {
                for (index = indices.alloc(count), segment = segments->base + segments->idx, i = 0; i < count; i++, segment++, index++)
                    new (index) Index(segment->x0 < segment->x1 ? segment->x0 : segment->x1, i);
                if (indices.end - indices.idx > 32)
                    radixSort((uint32_t *)indices.base + indices.idx, int(indices.end - indices.idx), single ? clip.lx : 0, range, single, counts);
                else
                    std::sort(indices.base + indices.idx, indices.base + indices.end);
                
                ly = iy * kfh, ly = ly < clip.ly ? clip.ly : ly > clip.uy ? clip.uy : ly;
                uy = (iy + 1) * kfh, uy = uy < clip.ly ? clip.ly : uy > clip.uy ? clip.uy : uy;
                for (scale = 1.f / (uy - ly), cover = winding = 0.f, index = indices.base + indices.idx, lx = ux = index->x, i = begin = indices.idx; i < indices.end; i++, index++) {
                    if (index->x > ux && winding - floorf(winding) < 1e-6f) {
                        if (lx != ux)
                            writeEdges(lx, ly, ux, uy, iz, 1, (i - begin + 1) / 2, false, GPU::Quad::kEdge, cover, iy, int(segments->idx), int(begin), i - begin, gpu);
                        begin = i;
                        if (alphaForCover(winding, even) > 0.998f) {
                            if (opaque)
                                new (gpu.opaques.alloc(1)) GPU::Quad(ux, ly, index->x, uy, 0.f, 0.f, iz, GPU::Quad::kOpaque, 1.f, 0, 0, 0, 0);
                            else {
                                new (gpu.quads.alloc(1)) GPU::Quad(ux, ly, index->x, uy, 0.f, 0.f, iz, GPU::Quad::kSolidCell, 1.f, 0, 0, 0, 0);
                                 gpu.allocator.countQuad();
                            }
                        }
                        lx = ux = index->x;
                        cover = winding;
                    }
                    segment = segments->base + segments->idx + index->i;
                    winding += (segment->y1 - segment->y0) * scale;
                    x = ceilf(segment->x0 > segment->x1 ? segment->x0 : segment->x1), ux = x > ux ? x : ux;
                }
                if (lx != ux)
                    writeEdges(lx, ly, ux, uy, iz, 1, (i - begin + 1) / 2, false, GPU::Quad::kEdge, cover, iy, int(segments->idx), int(begin), i - begin, gpu);
            }
    }
    static void writeSegments(Row<Segment> *segments, Bounds clip, bool even, Info del, uint8_t *src, Bitmap *bitmap) {
        size_t ily = floorf(clip.ly * krfh), iuy = ceilf(clip.uy * krfh), iy, i;
        uint16_t counts[256];
        float src0 = src[0], src1 = src[1], src2 = src[2], srcAlpha = src[3] * 0.003921568627f, ly, uy, scale, cover, lx, ux, x, y, *delta;
        Segment *segment;
        Row<Index> indices;    Index *index;
        for (segments += ily, iy = ily; iy < iuy; iy++, segments++) {
            ly = iy * kfh, ly = ly < clip.ly ? clip.ly : ly > clip.uy ? clip.uy : ly;
            uy = (iy + 1) * kfh, uy = uy < clip.ly ? clip.ly : uy > clip.uy ? clip.uy : uy;
            if (segments->end) {
                for (index = indices.alloc(segments->end), segment = segments->base, i = 0; i < segments->end; i++, segment++, index++)
                    new (index) Index(segment->x0 < segment->x1 ? segment->x0 : segment->x1, i);
                if (indices.end > 32)
                    radixSort((uint32_t *)indices.base, int(indices.end), 0, 256, false, counts);
                else
                    std::sort(indices.base, indices.base + indices.end);
                for (scale = 1.f / (uy - ly), cover = 0.f, index = indices.base, lx = ux = index->x, i = 0; i < indices.end; i++, index++) {
                    if (index->x > ux && cover - floorf(cover) < 1e-6f) {
                        writeDeltas(del, Bounds(lx, ly, ux, uy), even, src, bitmap);
                        lx = ux, ux = index->x;
                        if (alphaForCover(cover, even) > 0.998f)
                            for (delta = del.deltas, y = ly; y < uy; y++, delta += del.stride) {
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
                writeDeltas(del, Bounds(lx, ly, ux, uy), even, src, bitmap);
                indices.empty();
                segments->empty();
            }
        }
    }
    static void writeDeltas(Info del, Bounds clip, bool even, uint8_t *src, Bitmap *bitmap) {
        float *deltas = del.deltas;
        if (clip.lx == clip.ux)
            for (float y = clip.ly; y < clip.uy; y++, deltas += del.stride)
                *deltas = 0.f;
        else {
            uint8_t *pixelAddress = pixelAddress = bitmap->pixelAddress(clip.lx, clip.ly), *pixel;
            float src0 = src[0], src1 = src[1], src2 = src[2], srcAlpha = src[3] * 0.003921568627f, y, x, cover, alpha, *delta;
            for (y = clip.ly; y < clip.uy; y++, deltas += del.stride, pixelAddress -= bitmap->stride, *delta = 0.f)
                for (cover = alpha = 0.f, delta = deltas, pixel = pixelAddress, x = clip.lx; x < clip.ux; x++, delta++, pixel += bitmap->bytespp) {
                    if (*delta)
                        cover += *delta, *delta = 0.f, alpha = alphaForCover(cover, even);
                    if (alpha > 0.003921568627f)
                        writePixel(src0, src1, src2, alpha * srcAlpha, pixel);
                }
        }
    }
    static inline void writePixel(float src0, float src1, float src2, float alpha, uint8_t *dst) {
#ifdef RASTERIZER_SIMD
        typedef float vec4f __attribute__((vector_size(16)));
        typedef uint8_t vec4b __attribute__((vector_size(4)));

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
    struct Buffer {
        struct Entry {
            enum Type { kColorants, kAffineTransforms, kClips, kEdgeCells, kEdges, kFastEdges, kQuads, kOpaques, kShapes, kSegments };
            Entry(Type type, size_t begin, size_t end) : type(type), begin(begin), end(end) {}
            Type type;
            size_t begin, end;
        };
        Entry *writeEntry(Entry::Type type, size_t begin, size_t size, void *src) {
            if (src)
                memcpy(data.base + begin, src, size);
            return new (entries.alloc(1)) Entry(type, begin, begin + size);
        }
        Pages<uint8_t> data;
        Row<Entry> entries;
        Colorant clearColor;
    };
    static size_t writeContextsToBuffer(Context *contexts, size_t count,
                                        Path *paths,
                                        Transform *ctms,
                                        Colorant *colorants,
                                        Transform *clips,
                                        size_t pathsCount,
                                        size_t *begins,
                                        Buffer& buffer) {
        size_t size, sz, i, j, begin, end, cells, instances;
        size = pathsCount * (sizeof(Colorant) + 2 * sizeof(Transform));
        for (i = 0; i < count; i++)
            size += contexts[i].gpu.opaques.end * sizeof(GPU::Quad);
        for (i = 0; i < count; i++) {
            begins[i] = size;
            GPU& gpu = contexts[i].gpu;
            for (cells = 0, instances = 0, j = 0; j < gpu.allocator.passes.end; j++)
                cells += gpu.allocator.passes.base[j].cells, instances += gpu.allocator.passes.base[j].edgeInstances, instances += gpu.allocator.passes.base[j].fastInstances;
            size += instances * sizeof(GPU::Edge) + cells * sizeof(GPU::EdgeCell) + (gpu.outlines.end - gpu.outlinePaths + gpu.shapesCount - gpu.shapePaths + gpu.quads.end) * sizeof(GPU::Quad) + gpu.cache.segments.end * sizeof(Segment);
            for (j = 0; j < contexts[i].segments.size(); j++)
                size += contexts[i].segments[j].end * sizeof(Segment);
        }
        buffer.data.alloc(size);
        
        Buffer::Entry *entry;
        entry = buffer.writeEntry(Buffer::Entry::kColorants, 0, pathsCount * sizeof(Colorant), colorants);
        entry = buffer.writeEntry(Buffer::Entry::kAffineTransforms, entry->end, pathsCount * sizeof(Transform), contexts[0].gpu.ctms);
        entry = buffer.writeEntry(Buffer::Entry::kClips, entry->end, pathsCount * sizeof(Transform), clips);
    
        for (begin = end = entry->end, i = 0; i < count; i++)
            if ((sz = contexts[i].gpu.opaques.end * sizeof(GPU::Quad)))
                memcpy(buffer.data.base + end, contexts[i].gpu.opaques.base, sz), end += sz;
        if (begin != end)
            new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::Entry::kOpaques, begin, end);
        return size;
    }
    static void writeContextToBuffer(Context *ctx,
                                     Path *paths,
                                     Transform *ctms,
                                     Colorant *colorants,
                                     size_t begin,
                                     std::vector<Buffer::Entry>& entries,
                                     Buffer& buffer) {
        size_t j, iz, sbegins[ctx->segments.size()], size;
        std::vector<size_t> idxes;
        
        size = ctx->gpu.cache.segments.end;
        for (j = 0; j < ctx->segments.size(); j++)
            sbegins[j] = size, size += ctx->segments[j].end;
        if (size) {
            Segment *dst = (Segment *)(buffer.data.base + begin);
            if (ctx->gpu.cache.segments.end)
                memcpy(dst, ctx->gpu.cache.segments.base, ctx->gpu.cache.segments.end * sizeof(Segment));
            for (j = 0; j < ctx->segments.size(); j++)
                memcpy(dst + sbegins[j], ctx->segments[j].base, ctx->segments[j].end * sizeof(Segment));
            entries.emplace_back(Buffer::Entry::kSegments, begin, begin + size * sizeof(Segment)), idxes.emplace_back(0), begin = entries.back().end;
        }
        for (int k = 0; k < ctx->gpu.allocator.passes.end; k++, begin = entries.back().end) {
            GPU::Allocator::Pass& e = ctx->gpu.allocator.passes.base[k];

            GPU::EdgeCell *cell = (GPU::EdgeCell *)(buffer.data.base + begin), *c0 = cell;
            entries.emplace_back(Buffer::Entry::kEdgeCells, begin, begin + e.cells * sizeof(GPU::EdgeCell)), idxes.emplace_back(0), begin = entries.back().end;
            
            GPU::Edge *edge = (GPU::Edge *)(buffer.data.base + begin);
            entries.emplace_back(Buffer::Entry::kEdges, begin, begin + e.edgeInstances * sizeof(GPU::Edge)), idxes.emplace_back(0), begin = entries.back().end;

            GPU::Edge *fast = (GPU::Edge *)(buffer.data.base + begin);
            entries.emplace_back(Buffer::Entry::kFastEdges, begin, begin + e.fastInstances * sizeof(GPU::Edge)), idxes.emplace_back(0), begin = entries.back().end;
            
            Buffer::Entry *entry;
            GPU::Quad *q0 = ctx->gpu.quads.base + e.li, *qidx = ctx->gpu.quads.base + e.ui, *quad;
            int type = q0->iz & GPU::Quad::kShapes || q0->iz & GPU::Quad::kOutlines ? Buffer::Entry::kShapes : Buffer::Entry::kQuads;
            entries.emplace_back(Buffer::Entry::Type(type), begin, begin), idxes.emplace_back(e.li), entry = & entries.back();
            
            for (quad = q0; quad < qidx; quad++) {
                type = quad->iz & GPU::Quad::kShapes || quad->iz & GPU::Quad::kOutlines ? Buffer::Entry::kShapes : Buffer::Entry::kQuads;
                iz = quad->iz & 0xFFFFFF;
                if (type != entry->type)
                    entries.emplace_back(Buffer::Entry::Type(type), begin, begin), idxes.emplace_back(quad - ctx->gpu.quads.base), entry = & entries.back();
                if (quad->iz & GPU::Quad::kShapes) {
                    Path& path = paths[iz];
                    GPU::Quad *dst = (GPU::Quad *)(buffer.data.base + entry->end);
                    for (int k = 0; k < path.ref->shapesCount; k++, dst++)
                        new (dst) GPU::Quad(path.ref->shapes[k], iz, path.ref->circles[k] ? GPU::Quad::kCircle : GPU::Quad::kRect);
                    entry->end += path.ref->shapesCount * sizeof(GPU::Quad);
                } else if (quad->iz & GPU::Quad::kOutlines) {
                    size_t count = quad->outline.s.y0 - quad->outline.s.x0;
                    Segment *src = ctx->gpu.outlines.base + int(quad->outline.s.x0), *es = src + count;;
                    GPU::Quad *dst = (GPU::Quad *)(buffer.data.base + entry->end), *dst0;
                    for (dst0 = dst; src < es; src++, dst++) {
                        new (dst) GPU::Quad(src, quad->outline.width, iz, GPU::Quad::kOutlines);
                        if (src->x0 == FLT_MAX && dst - dst0 > 1)
                            dst0->outline.prev = (int)(dst - dst0 - 1), (dst - 1)->outline.next = -dst0->outline.prev, dst0 = dst + 1;
                    }
                    entry->end += count * sizeof(GPU::Quad);
                } else  {
                    entry->end += sizeof(GPU::Quad);
                    
                    if (quad->iz & GPU::Quad::kMolecule) {
                        GPU::Molecules::Range *mr = & ctx->gpu.molecules.ranges.base[quad->super.begin];
                        GPU::Molecules::Cell *mc = & ctx->gpu.molecules.cells.base[mr->begin];
                        for (k = mr->begin; k < mr->end; k++, cell++, mc++) {
                            cell->cell = quad->super.cell;
                            cell->cell.ux = mc->ux;
                            cell->im = quad->super.iy;
                            cell->base = int(quad->super.end);
                            int ic = int(cell - c0);
                            for (j = mc->begin; j < mc->end; fast++)
                                fast->ic = ic, fast->i0 = j, j += kFastSegments, fast->i1 = j;
                            (fast - 1)->i1 = mc->end;
                        }
                    } else if (quad->iz & GPU::Quad::kEdge) {
                        cell->cell = quad->super.cell;
                        int ic = int(cell - c0);
                        if (quad->super.iy < 0) {
                            cell->im = quad->super.iy;
                            cell->base = int(quad->super.end);
                            for (j = 0; j < quad->super.count; fast++)
                                fast->ic = ic, fast->i0 = j, j += kFastSegments, fast->i1 = j;
                            (fast - 1)->i1 = quad->super.count;
                        } else {
                            cell->im = 0;
                            cell->base = int(sbegins[quad->super.iy] + quad->super.end);
                            Index *is = ctx->gpu.indices.base + quad->super.begin;
                            for (j = 0; j < quad->super.count; j++, edge++) {
                                edge->ic = ic, edge->i0 = uint16_t(is++->i);
                                if (++j < quad->super.count)
                                    edge->i1 = uint16_t(is++->i);
                                else
                                    edge->i1 = kNullIndex;
                            }
                        }
                        cell++;
                    }
                }
            }
        }
        for (int k = 0; k < idxes.size(); k++)
            if (entries[k].type == Buffer::Entry::kQuads)
                memcpy(buffer.data.base + entries[k].begin, ctx->gpu.quads.base + idxes[k], entries[k].end - entries[k].begin);
        
        ctx->gpu.empty();
        for (j = 0; j < ctx->segments.size(); j++)
            ctx->segments[j].empty();
    }
};
