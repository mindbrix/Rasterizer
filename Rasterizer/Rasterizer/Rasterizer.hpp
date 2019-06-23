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
        static Transform identity() { return { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f }; }
        static Transform nullclip() { return { 1e12f, 0.f, 0.f, 1e12f, -5e11f, -5e11f }; }
        inline Transform concat(Transform t, float ax, float ay) const {
            Transform inv = invert();
            float ix = ax * inv.a + ay * inv.c + inv.tx, iy = ax * inv.b + ay * inv.d + inv.ty;
            Transform m = Transform(a, b, c, d, ix * a + iy * c + tx, ix * b + iy * d + ty).concat(t);
            return Transform(m.a, m.b, m.c, m.d, m.tx - ix * m.a - iy * m.c, m.ty - ix * m.b - iy * m.d);
        }
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
        inline Bounds integral() const { return { floorf(lx), floorf(ly), ceilf(ux), ceilf(uy) }; }
        inline Bounds intersect(Bounds b) const {
            return {
                lx < b.lx ? b.lx : lx > b.ux ? b.ux : lx, ly < b.ly ? b.ly : ly > b.uy ? b.uy : ly,
                ux < b.lx ? b.lx : ux > b.ux ? b.ux : ux, uy < b.ly ? b.ly : uy > b.uy ? b.uy : uy
            };
        }
        inline Transform unit(Transform t) const {
            return { t.a * (ux - lx), t.b * (ux - lx), t.c * (uy - ly), t.d * (uy - ly), lx * t.a + ly * t.c + t.tx, lx * t.b + ly * t.d + t.ty };
        }
        float lx, ly, ux, uy;
    };
    static bool isVisible(Bounds bounds, Transform ctm, Transform clip, Bounds device) {
        Transform unit = bounds.unit(ctm);
        Bounds dev = Bounds(unit).intersect(device.intersect(Bounds(clip)));
        Bounds clu = Bounds(clip.invert().concat(unit));
        return dev.lx != dev.ux && dev.ly != dev.uy && clu.ux >= 0.f && clu.lx < 1.f && clu.uy >= 0.f && clu.ly < 1.f;
    }
    struct Colorant {
        Colorant() {}
        Colorant(uint8_t src0, uint8_t src1) : src0(src0), src1(src0), src2(src0), src3(src1) {}
        Colorant(uint8_t src0, uint8_t src1, uint8_t src2, uint8_t src3) : src0(src0), src1(src1), src2(src2), src3(src3) {}
        uint8_t src0, src1, src2, src3;
    };
    struct Geometry {
        struct Atom {
            enum Type { kNull = 0, kMove, kLine, kQuadratic, kCubic, kClose, kCountSize, kCapacity = 15 };
            Atom() { bzero(points, sizeof(points)), bzero(types, sizeof(types)); }
            float       points[30];
            uint8_t     types[8];
        };
        Geometry() : end(Atom::kCapacity), atomsCount(0), shapesCount(0), px(0), py(0), bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX), shapes(nullptr), circles(nullptr), isGlyph(false), isPolygon(true), refCount(0), hash(0) { bzero(counts, sizeof(counts)); }
        ~Geometry() { if (shapes) free(shapes), free(circles); }
        
        bool isDrawable() {
            return !((atomsCount < 3 && shapesCount == 0) || bounds.lx == FLT_MAX);
        }
        float *alloc(Atom::Type type, size_t size) {
            if (end + size > Atom::kCapacity)
                end = 0, atoms.emplace_back();
            atoms.back().types[end / 2] |= (uint8_t(type) << ((end & 1) * 4));
            end += size, atomsCount += size;
            return atoms.back().points + (end - size) * 2;
        }
        void update(Atom::Type type, size_t size, float *p) {
            counts[type]++, hash = ::crc64(::crc64(hash, & type, sizeof(type)), p, size * 2 * sizeof(float));
            if (type == Atom::kMove)
                molecules.emplace_back(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
            isPolygon &= type != Atom::kQuadratic && type != Atom::kCubic;
            while (size--)
                bounds.extend(p[0], p[1]), molecules.back().extend(p[0], p[1]), p += 2;
        }
        void allocShapes(size_t count) {
            shapesCount = count, shapes = (Transform *)calloc(count, sizeof(Transform)), circles = (bool *)calloc(count, sizeof(bool));
        }
        void setShapesBounds(size_t count) {
            bounds = Bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
            Bounds sb;
            for (int i = 0; i < shapesCount; i++)
                sb = Bounds(shapes[i]), bounds.extend(sb.lx, sb.ly), bounds.extend(sb.ux, sb.uy);
        }
        void addBounds(Bounds b) { moveTo(b.lx, b.ly), lineTo(b.ux, b.ly), lineTo(b.ux, b.uy), lineTo(b.lx, b.uy), close(); }
        void addEllipse(Bounds b) {
            const float t0 = 0.5f - 2.f / 3.f * (sqrtf(2.f) - 1.f), t1 = 1.f - t0, mx = 0.5f * (b.lx + b.ux), my = 0.5f * (b.ly + b.uy);
            moveTo(b.ux, my);
            cubicTo(b.ux, t0 * b.ly + t1 * b.uy, t0 * b.lx + t1 * b.ux, b.uy, mx, b.uy);
            cubicTo(t1 * b.lx + t0 * b.ux, b.uy, b.lx, t0 * b.ly + t1 * b.uy, b.lx, my);
            cubicTo(b.lx, t1 * b.ly + t0 * b.uy, t1 * b.lx + t0 * b.ux, b.ly, mx, b.ly);
            cubicTo(t0 * b.lx + t1 * b.ux, b.ly, b.ux, t1 * b.ly + t0 * b.uy, b.ux, my);
        }
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
            update(Atom::kClose, 0, alloc(Atom::kClose, 1));
        }
        size_t refCount, atomsCount, shapesCount, hash, end, counts[Atom::kCountSize];
        std::vector<Atom> atoms;
        std::vector<Bounds> molecules;
        float px, py;
        Transform *shapes;
        bool *circles, isGlyph, isPolygon;
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
    
    struct Scene {
        Bounds addBounds(Bounds bounds, Transform ctm, Colorant col) {
            Path path;
            path.ref->addBounds(bounds);
            return addPath(path, ctm, col);
        }
        Bounds addPath(Path path, Transform ctm, Colorant colorant) {
            paths.emplace_back(path), ctms.emplace_back(ctm), colors.emplace_back(colorant);
            Bounds user = Bounds(path.ref->bounds.unit(ctm));
            bounds.extend(user.lx, user.ly), bounds.extend(user.ux, user.uy);
            return user;
        }
        size_t refCount = 0;
        std::vector<Path> paths;  std::vector<Transform> ctms;  std::vector<Colorant> colors;
        Bounds bounds = Bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
    };
    struct SceneList {
        SceneList& empty() {
            scenes.resize(0), ctms.resize(0), clips.resize(0);
            return *this;
        }
        Scene& addScene() {
            scenes.emplace_back(Ref<Scene>()), ctms.emplace_back(Transform(1.f, 0.f, 0.f, 1.f, 0.f, 0.f)), clips.emplace_back(Transform::nullclip());
            return *scenes.back().ref;
        }
        size_t writeVisibles(Transform view, Bounds bounds, SceneList& visibles) {
            size_t pathsCount = 0;
            for (int i = 0; i < scenes.size(); i++)
                if (scenes[i].ref->paths.size() && isVisible(scenes[i].ref->bounds, view.concat(ctms[i]), view.concat(clips[i]), bounds))
                    pathsCount += scenes[i].ref->paths.size(), visibles.scenes.emplace_back(scenes[i]), visibles.ctms.emplace_back(ctms[i]), visibles.clips.emplace_back(clips[i]);
            return pathsCount;
        }
        std::vector<Ref<Scene>> scenes;  std::vector<Transform> ctms, clips;
    };
    template<typename T>
    struct Memory {
        ~Memory() { if (addr) free(addr); }
        void resize(size_t n) { size = n, addr = (T *)realloc(addr, size * sizeof(T)); }
        size_t refCount = 0, size = 0;
        T *addr = nullptr;
    };
    template<typename T>
    struct Row {
    public:
        Row<T>& operator+(const T *src) { if (src) { do *(alloc(1)) = *src; while (*src++);  --end; } return *this; }
        Row<T>& operator+(const int n) { char buf[32]; bzero(buf, sizeof(buf)), sprintf(buf, "%d", n); operator+((T *)buf); return *this; }
        Row<T>& empty() { end = idx = 0; return *this; }
        void reset() { end = idx = 0, base = nullptr, memory = Ref<Memory<T>>(); }
        inline T *alloc(size_t n) {
            end += n;
            if (memory.ref->size < end)
                memory.ref->resize(end * 1.5), base = memory.ref->addr;
            return base + end - n;
        }
        Ref<Memory<T>> memory;
        size_t end = 0, idx = 0;
        T *base = nullptr;
    };
    template<typename T>
    struct Pages {
        static constexpr size_t kPageSize = 4096;
        ~Pages() { if (base) free(base); }
        T *resize(size_t n) {
            size_t allocation = (n * sizeof(T) + kPageSize - 1) / kPageSize * kPageSize;
            if (size < allocation) {
                this->~Pages();
                posix_memalign((void **)& base, kPageSize, allocation);
                size = allocation;
            }
            return base;
        }
        size_t size = 0;
        T *base = nullptr;
    };
    struct Range {
        Range(size_t begin, size_t end) : begin(int(begin)), end(int(end)) {}
        int begin, end;
    };
    struct Segment {
        Segment(float x0, float y0, float x1, float y1) : x0(x0), y0(y0), x1(x1), y1(y1) {}
        float x0, y0, x1, y1;
    };
    struct Info {
        Info(void *info) : info(info), stride(0) {}
        Info(float *deltas, uint32_t stride) : deltas(deltas), stride(stride) {}
        Info(Row<Segment> *segments, size_t offset) : segments(segments), stride(uint32_t(offset)) {}
        union { void *info;  float *deltas;  Row<Segment> *segments; };
        uint32_t stride;
    };
    struct Cache {
        struct Entry {
            Entry(size_t hash, size_t begin, size_t end, size_t cbegin, size_t cend, Transform ctm) : hash(hash), seg(begin, end), cnt(cbegin, cend), ctm(ctm), hit(true) {}
            size_t hash; Range seg, cnt; Transform ctm; bool hit;
        };
        struct Grid {
            static constexpr size_t kSize = 4096, kMask = kSize - 1;
            struct Element {
                Element(size_t hash, size_t index) : hash(hash), index(index) {}
                size_t hash, index;
            };
            void empty()                    { for (Row<Element>& row : grid) row.empty(); }
            void reset()                    { for (Row<Element>& row : grid) row.reset(); }
            Element *alloc(size_t hash)     { return grid[hash & kMask].alloc(1); }
            Element *find(size_t hash) {
                Row<Element>& row = grid[hash & kMask];
                for (Element *e = row.base, *ue = e + row.end; e < ue; e++)
                    if (e->hash == hash)
                        return e;
                return nullptr;
            }
            Row<Element> grid[kSize];
        };
        void compact() {
            grid.empty();
            int count = 0, ccount = 0, i = 0;
            int *csrc = counts.base, *cdst = csrc;
            Segment *ssrc = segments.base, *sdst = ssrc;
            Entry *src = entries.base, *dst = src, *end = src + entries.end;
            for (; src < end; csrc += ccount, ssrc += count, src++) {
                count = src->seg.end - src->seg.begin, ccount = src->cnt.end - src->cnt.begin;
                if (src->hit) {
                    if (src != dst) {
                        dst->hash = src->hash, dst->seg.begin = int(sdst - segments.base), dst->seg.end = dst->seg.begin + count;
                        dst->cnt.begin = int(cdst - counts.base), dst->cnt.end = dst->cnt.begin + ccount, dst->ctm = src->ctm;
                        memmove(sdst, ssrc, count * sizeof(Segment)), memmove(cdst, csrc, ccount * sizeof(int));
                    }
                    new (grid.grid[src->hash & Grid::kMask].alloc(1)) Grid::Element(src->hash, i);
                    dst->hit = false, cdst += ccount, sdst += count, dst++, i++;
                }
            }
            entries.end = dst - entries.base;
            counts.idx = counts.end = cdst - counts.base;
            segments.idx = segments.end = sdst - segments.base;
        }
        void reset() { segments.reset(), grid.reset(), entries.reset(), counts.reset(); }
        
        Entry *getPath(Path& path, Transform ctm, Transform *m) {
            uint64_t hash = path.ref->hash;
            if (!path.ref->isPolygon) {
                Transform unit = path.ref->bounds.unit(ctm);
                hash += uint64_t(1 + log2f(fabsf(unit.a * unit.d - unit.b * unit.c)));
            }
            Grid::Element *el = grid.find(hash);
            if (el) {
                Entry *srch = entries.base + el->index;
                *m = ctm.concat(srch->ctm);
                bool hit = m->a == m->d && m->b == -m->c;
                srch->hit |= hit;
                return hit ? srch : nullptr;
            }
            size_t begin = segments.idx, cbegin = counts.idx;
            writePath(path, ctm, Bounds(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX), writeOutlineSegment, Info(& segments));
            segments.idx = segments.end;
            int *c = counts.alloc(path.ref->molecules.size());
            for (Segment *ls = segments.base + begin, *us = segments.base + segments.end, *s = ls; s < us; s++)
                if (s->x0 == FLT_MAX)
                    *c++ = int(s - ls);
            counts.idx = counts.end;
            new (grid.alloc(hash)) Grid::Element(hash, entries.end);
            return new (entries.alloc(1)) Entry(hash, begin, segments.end, cbegin, counts.end, ctm.invert());
        }
        void writeCachedOutline(Entry *e, Transform m, Bounds clip, Info info) {
            float x0, y0, x1, y1, iy0, iy1;
            Segment *s = segments.base + e->seg.begin, *end = segments.base + e->seg.end;
            for (x0 = s->x0 * m.a + s->y0 * m.c + m.tx, y0 = s->x0 * m.b + s->y0 * m.d + m.ty, y0 = y0 < clip.ly ? clip.ly : y0 > clip.uy ? clip.uy : y0, iy0 = floorf(y0 * krfh); s < end; s++, x0 = x1, y0 = y1, iy0 = iy1) {
                x1 = s->x1 * m.a + s->y1 * m.c + m.tx, y1 = s->x1 * m.b + s->y1 * m.d + m.ty, y1 = y1 < clip.ly ? clip.ly : y1 > clip.uy ? clip.uy : y1, iy1 = floorf(y1 * krfh);
                if (s->x0 != FLT_MAX) {
                    if (iy0 == iy1 && y0 != y1)
                        new (info.segments[size_t(iy0) - info.stride].alloc(1)) Segment(x0, y0, x1, y1);
                    else
                        writeClippedSegment(x0, y0, x1, y1, & info);
                } else if (s < end - 1)
                    x1 = (s + 1)->x0 * m.a + (s + 1)->y0 * m.c + m.tx, y1 = (s + 1)->x0 * m.b + (s + 1)->y0 * m.d + m.ty, iy1 = floorf(y1 * krfh);
            }
        }
        Grid grid;
        Row<Entry> entries;
        Row<Segment> segments;
        Row<int> counts;
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
                        b = & molecules, hght = kMoleculesHeight;
                    else
                        b = & fast, hght = kFastHeight;
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
            inline void countInstance() {
                Pass *pass = passes.end ? & passes.base[passes.end - 1] : new (passes.alloc(1)) Pass(0);
                pass->ui++;
            }
            size_t width, height;
            Row<Pass> passes;
            Bounds sheet, strip, fast, molecules;
        };
        struct Molecules {
            struct Molecule {
                Molecule(float ux, int begin, int end) : ux(ux), begin(begin), end(end) {}
                float ux;
                int begin, end;
            };
            Molecule *alloc(size_t size) {
                new (ranges.alloc(1)) Range(cells.idx, cells.idx + size), cells.idx += size;
                return cells.alloc(size);
            }
            void empty() { ranges.empty(), cells.empty(); }
            void reset() { ranges.reset(), cells.reset(); }
            Row<Range> ranges;
            Row<Molecule> cells;
        };
        struct Cell {
            Cell(float lx, float ly, float ux, float uy, float ox, float oy) : lx(lx), ly(ly), ux(ux), uy(uy), ox(ox), oy(oy) {}
            uint16_t lx, ly, ux, uy, ox, oy;
        };
        struct Quad {
            Quad(float lx, float ly, float ux, float uy, float ox, float oy, float cover, int iy, int end, int begin, size_t count) : cell(lx, ly, ux, uy, ox, oy), cover(short(roundf(cover))), count(uint16_t(count)), iy(iy), begin(begin), end(end) {}
            Cell cell;
            short cover;
            uint16_t count;
            int iy, begin, end;
        };
        struct Outline {
            Outline(Segment *s, float width) : s(*s), width(width), prev(-1), next(1) {}
            Outline(size_t begin, size_t end, float width) : r(begin, end), width(width), prev(-1), next(1) {}
            union { Segment s;  Range r; };
            float width;
            short prev, next;
        };
        struct Instance {
            enum Type { kRect = 1 << 24, kCircle = 1 << 25, kEdge = 1 << 26, kSolidCell = 1 << 27, kShapes = 1 << 28, kOutlines = 1 << 29, kOpaque = 1 << 30, kMolecule = 1 << 31 };
            Instance(Quad quad, size_t iz, int type) : quad(quad), iz((uint32_t)iz | type) {}
            Instance(Transform unit, size_t iz, int type) : unit(unit), iz((uint32_t)iz | type) {}
            Instance(Outline outline, size_t iz, int type) : outline(outline), iz((uint32_t)iz | type) {}
            
            union { Quad quad;  Transform unit;  Outline outline; };
            uint32_t iz;
        };
        struct EdgeCell {
            Cell cell;
            uint32_t im, base;
        };
        struct Edge {
            uint32_t ic;
            uint16_t i0, i1;
        };
        void empty() {
            shapesCount = shapePaths = outlinePaths = 0, indices.empty(), blends.empty(), opaques.empty(), outlines.empty(), ctms = nullptr, molecules.empty(), cache.compact();
        }
        void reset() {
            shapesCount = shapePaths = outlinePaths = 0, indices.reset(), blends.reset(), opaques.reset(), outlines.reset(), ctms = nullptr, molecules.reset(), cache.reset();
        }
        size_t shapesCount = 0, shapePaths = 0, outlinePaths = 0;
        Allocator allocator;
        Molecules molecules;
        Row<Index> indices;
        Row<Instance> blends, opaques;
        Row<Segment> outlines;
        Transform *ctms = nullptr;
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
            new (info->segments[size_t(iy0) - info->stride].alloc(1)) Segment(x0, y0, x1, y1);
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
                new (info->segments[s - info->stride].alloc(1)) Segment(x0, y0, sx1, sy1);
                x0 = sx1, y0 = sy1, s += ds;
            }
            new (info->segments[s - info->stride].alloc(1)) Segment(x0, y0, x1, y1);
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
        void clear(Colorant color) { memset_pattern4(data, & color.src0, stride * height); }
        inline uint8_t *pixelAddress(short x, short y) { return data + stride * (height - 1 - y) + x * bytespp; }
        uint8_t *data;
        size_t width, height, stride, bpp, bytespp;
    };
    struct Context {
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
        }
        void drawScenes(SceneList& list, Transform *ctms, bool even, Colorant *colors, Transform *clips, float width, size_t iz, size_t end) {
            for (size_t count = 0, base = 0, i = 0, liz, eiz; i < list.scenes.size(); base = count, i++) {
                count += list.scenes[i].ref->paths.size();
                if ((liz = base < iz ? iz : base > end ? end : base) != (eiz = count < iz ? iz : count > end ? end : count))
                    drawPaths(& list.scenes[i].ref->paths[0] + liz - base, ctms + liz, even, colors + liz, clips[liz], width, liz, eiz);
            }
        }
        void drawPaths(Path *paths, Transform *ctms, bool even, Colorant *colors, Transform clip, float width, size_t iz, size_t eiz) {
            Transform inv = bitmap.width ? Transform::nullclip().invert() : clip.invert();
            Bounds device = Bounds(clip).integral().intersect(bounds);
            for (; iz < eiz; iz++, paths++, ctms++, colors++) {
                Transform unit = paths->ref->bounds.unit(*ctms);
                Bounds dev = Bounds(unit).integral(), clip = dev.intersect(device), clu = Bounds(inv.concat(unit));
                bool unclipped = dev.lx == clip.lx && dev.ly == clip.ly && dev.ux == clip.ux && dev.uy == clip.uy, hit = clu.lx < 0.f || clu.ux > 1.f || clu.ly < 0.f || clu.uy > 1.f;
                if (clip.lx != clip.ux && clip.ly != clip.uy && clu.ux >= 0.f && clu.lx < 1.f && clu.uy >= 0.f && clu.ly < 1.f) {
                    if (bitmap.width == 0)
                        writeGPUPath(*paths, *ctms, even, & colors->src0, iz, unclipped, clip, hit, width, Info(& segments[0], clip.ly * krfh), gpu);
                    else {
                        if (paths->ref->shapesCount == 0)
                            writeBitmapPath(*paths, *ctms, even, & colors->src0, clip, hit, Info(& segments[0], clip.ly * krfh), deltas.base, deltas.end, & bitmap);
                        else {
                            for (int i = 0; i < paths->ref->shapesCount; i++) {
                                Transform shape = ctms->concat(paths->ref->shapes[i]);
                                Bounds shapeClip = Bounds(shape).integral().intersect(clip);
                                if (shapeClip.lx != shapeClip.ux && shapeClip.ly != shapeClip.uy)
                                    writeShape(shapeClip, ctms->concat(paths->ref->shapes[i]), paths->ref->circles[i], & colors->src0, & bitmap);
                            }
                        }
                    }
                }
            }
        }
        void reset() { gpu.reset(), deltas.reset(), segments.resize(0); }
        GPU gpu;
        Bitmap bitmap;
        Bounds bounds;
        Row<float> deltas;
        std::vector<Row<Segment>> segments;
    };
    static void writeBitmapPath(Path& path, Transform ctm, bool even, uint8_t *src, Bounds clip, bool hit, Info sgmnts, float *deltas, size_t deltasSize, Bitmap *bm) {
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
        if (path.ref->shapesCount) {
            new (gpu.blends.alloc(1)) GPU::Instance(ctm, iz, GPU::Instance::kShapes);
            gpu.shapePaths++, gpu.shapesCount += path.ref->shapesCount, gpu.allocator.countInstance();
        } else {
            if (width) {
                writePath(path, ctm, Bounds(clip.lx - width, clip.ly - width, clip.ux + width, clip.uy + width), writeOutlineSegment, Info(& gpu.outlines));
                new (gpu.blends.alloc(1)) GPU::Instance(GPU::Outline(gpu.outlines.idx, gpu.outlines.end, width), iz, GPU::Instance::kOutlines);
                gpu.outlines.idx = gpu.outlines.end, gpu.outlinePaths++, gpu.allocator.countInstance();
            } else {
                Cache::Entry *entry = nullptr;
                Transform m = { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f };
                bool slow = clip.uy - clip.ly > kMoleculesHeight || clip.ux - clip.lx > kMoleculesHeight;
                if (!slow || unclipped)
                    entry = gpu.cache.getPath(path, ctm, & m);
                if (entry == nullptr)
                    writePath(path, ctm, clip, writeClippedSegment, segments);
                else if (slow)
                    gpu.cache.writeCachedOutline(entry, m, clip, segments);
                if (entry && !slow) {
                    size_t midx = gpu.molecules.ranges.end, count = entry->seg.end - entry->seg.begin, ccount = entry->cnt.end - entry->cnt.begin, molecules = path.ref->molecules.size(), instances = 0;
                    gpu.ctms[iz] = m;
                    GPU::Molecules::Molecule *dst = gpu.molecules.alloc(molecules);
                    Bounds *b = & path.ref->molecules[0];
                    float ta, tc, ux;
                    for (int bc = 0, *lc = gpu.cache.counts.base + entry->cnt.begin, *uc = lc + ccount, *c = lc; c < uc; c++) {
                        ta = ctm.a * (b->ux - b->lx), tc = ctm.c * (b->uy - b->ly);
                        ux = ceilf(b->lx * ctm.a + b->ly * ctm.c + ctm.tx + (ta > 0.f ? ta : 0.f) + (tc > 0.f ? tc : 0.f));
                        new (dst) GPU::Molecules::Molecule(ux < clip.lx ? clip.lx : ux > clip.ux ? clip.ux : ux, bc, *c);
                        instances += (*c - bc + kFastSegments - 1) / kFastSegments, bc = *c + 1, b++, dst++;
                    }
                    writeInstances(clip.lx, clip.ly, clip.ux, clip.uy, iz, molecules, instances, true, GPU::Instance::kMolecule, 0.f, int(iz), entry->seg.begin, int(midx), count, gpu);
                } else
                    writeSegments(segments.segments, clip, even, iz, src[3] == 255 && !hit, gpu);
            }
        }
    }
    static void writeInstances(float lx, float ly, float ux, float uy, size_t iz, size_t cells, size_t instances, bool fast, int type, float cover, int iy, int end, int begin, size_t count, GPU& gpu) {
        float ox, oy;
        gpu.allocator.alloc(ux - lx, uy - ly, gpu.blends.end, cells, instances, fast, ox, oy);
        new (gpu.blends.alloc(1)) GPU::Instance(GPU::Quad(lx, ly, ux, uy, ox, oy, cover, iy, end, begin, count), iz, type);
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
                        tx0 = (ax * t0 + bx) * t0 + x0, ty0 = (ay * t0 + by) * t0 + y0;
                        tx2 = (ax * t1 + bx) * t1 + x0, ty2 = (ay * t1 + by) * t1 + y0;
                        tx1 = 2.f * x - 0.5f * (tx0 + tx2), ty1 = 2.f * y - 0.5f * (ty0 + ty2);
                        ty0 = ty0 < clip.ly ? clip.ly : ty0 > clip.uy ? clip.uy : ty0;
                        ty2 = ty2 < clip.ly ? clip.ly : ty2 > clip.uy ? clip.uy : ty2;
                        if (x >= clip.lx && x < clip.ux) {
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
                        tx0 = ((ax * t0 + bx) * t0 + cx) * t0 + x0, ty0 = ((ay * t0 + by) * t0 + cy) * t0 + y0;
                        tx3 = ((ax * t1 + bx) * t1 + cx) * t1 + x0, ty3 = ((ay * t1 + by) * t1 + cy) * t1 + y0;
                        ty0 = ty0 < clip.ly ? clip.ly : ty0 > clip.uy ? clip.uy : ty0;
                        ty3 = ty3 < clip.ly ? clip.ly : ty3 > clip.uy ? clip.uy : ty3;
                        x = ((ax * t + bx) * t + cx) * t + x0;
                        if (x >= clip.lx && x < clip.ux) {
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
        size_t ily = floorf(clip.ly * krfh), iuy = ceilf(clip.uy * krfh), iy, count, i, begin;
        uint16_t counts[256];
        float ly, uy, scale, cover, winding, lx, ux, x;
        bool single = clip.ux - clip.lx < 256.f;
        uint32_t range = single ? powf(2.f, ceilf(log2f(clip.ux - clip.lx + 1.f))) : 256;
        Segment *segment;
        Row<Index>& indices = gpu.indices;    Index *index;
        for (iy = ily; iy < iuy; iy++, indices.idx = indices.end, segments->idx = segments->end, segments++)
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
                            writeInstances(lx, ly, ux, uy, iz, 1, (i - begin + 1) / 2, false, GPU::Instance::kEdge, cover, int(iy - ily), int(segments->idx), int(begin), i - begin, gpu);
                        begin = i;
                        if (alphaForCover(winding, even) > 0.998f) {
                            if (opaque)
                                new (gpu.opaques.alloc(1)) GPU::Instance(GPU::Quad(ux, ly, index->x, uy, 0.f, 0.f, 1.f, 0, 0, 0, 0), iz, GPU::Instance::kOpaque);
                            else {
                                new (gpu.blends.alloc(1)) GPU::Instance(GPU::Quad(ux, ly, index->x, uy, 0.f, 0.f, 1.f, 0, 0, 0, 0), iz, GPU::Instance::kSolidCell);
                                 gpu.allocator.countInstance();
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
                    writeInstances(lx, ly, ux, uy, iz, 1, (i - begin + 1) / 2, false, GPU::Instance::kEdge, cover, int(iy - ily), int(segments->idx), int(begin), i - begin, gpu);
            }
    }
    static void writeSegments(Row<Segment> *segments, Bounds clip, bool even, Info info, uint8_t *src, Bitmap *bitmap) {
        size_t ily = floorf(clip.ly * krfh), iuy = ceilf(clip.uy * krfh), iy, i;
        uint16_t counts[256];
        float src0 = src[0], src1 = src[1], src2 = src[2], srcAlpha = src[3] * 0.003921568627f, ly, uy, scale, cover, lx, ux, x, y, *delta;
        Segment *segment;
        Row<Index> indices;    Index *index;
        for (iy = ily; iy < iuy; iy++, segments++) {
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
                        writeDeltas(info, Bounds(lx, ly, ux, uy), even, src, bitmap);
                        lx = ux, ux = index->x;
                        if (alphaForCover(cover, even) > 0.998f)
                            for (delta = info.deltas, y = ly; y < uy; y++, delta += info.stride) {
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
                    writeDeltaSegment(segment->x0 - lx, segment->y0 - ly, segment->x1 - lx, segment->y1 - ly, & info);
                }
                writeDeltas(info, Bounds(lx, ly, ux, uy), even, src, bitmap);
                indices.empty();
                segments->empty();
            }
        }
    }
    static void writeDeltas(Info info, Bounds clip, bool even, uint8_t *src, Bitmap *bitmap) {
        float *deltas = info.deltas;
        if (clip.lx == clip.ux)
            for (float y = clip.ly; y < clip.uy; y++, deltas += info.stride)
                *deltas = 0.f;
        else {
            uint8_t *pixelAddress = pixelAddress = bitmap->pixelAddress(clip.lx, clip.ly), *pixel;
            float src0 = src[0], src1 = src[1], src2 = src[2], srcAlpha = src[3] * 0.003921568627f, y, x, cover, alpha, *delta;
            for (y = clip.ly; y < clip.uy; y++, deltas += info.stride, pixelAddress -= bitmap->stride, *delta = 0.f)
                for (cover = alpha = 0.f, delta = deltas, pixel = pixelAddress, x = clip.lx; x < clip.ux; x++, delta++, pixel += bitmap->bytespp) {
                    if (*delta)
                        cover += *delta, *delta = 0.f, alpha = alphaForCover(cover, even);
                    if (alpha > 0.003921568627f)
                        writePixel(src0, src1, src2, alpha * srcAlpha, pixel);
                }
        }
    }
    static void writeShape(Bounds clip, Transform ctm, bool circle, uint8_t *src, Bitmap *bitmap) {
        float rl0, rl1, det, bx, by, bd0, bd2, dx0, dx1, dy0, dy1, r;
        float src0 = src[0], src1 = src[1], src2 = src[2], srcAlpha = src[3] * 0.003921568627f, y, x, del0, del1, d0, d1, d2, d3, cx, cy, alpha;
        det = ctm.a * ctm.d - ctm.b * ctm.c, rl0 = 1.f / sqrtf(ctm.c * ctm.c + ctm.d * ctm.d), rl1 = 1.f / sqrtf(ctm.a * ctm.a + ctm.b * ctm.b);
        dx0 = rl0 * -ctm.d, dy0 = rl0 * ctm.c, dx1 = rl1 * -ctm.b, dy1 = rl1 * ctm.a;
        bx = clip.lx - ctm.tx, by = clip.ly - ctm.ty, bd0 = rl0 * (ctm.c * by - ctm.d * bx), bd2 = rl1 * (ctm.a * by - ctm.b * bx);
        r = fmaxf(1.f, fminf(1.f + rl0 * det, 1.f + rl1 * det) * 0.5f);
        uint8_t *pixelAddress = pixelAddress = bitmap->pixelAddress(clip.lx, clip.ly), *pixel;
        for (y = clip.ly; y < clip.uy; y++, pixelAddress -= bitmap->stride) {
            del0 = bd0 + 0.5f * dx0 + (y - clip.ly + 0.5f) * dy0, del1 = bd2 + 0.5f * dx1 + (y - clip.ly + 0.5f) * dy1;
            d0 = 0.5f - del0, d1 = 0.5f + del0 + rl0 * det, d2 = 0.5f + del1, d3 = 0.5f - (del1 - rl1 * det);
            for (pixel = pixelAddress, x = clip.lx; x < clip.ux; x++, pixel += bitmap->bytespp, d0 -= dx0, d1 += dx0, d2 += dx1, d3 -= dx1) {
                if (circle)
                    cx = r - fminf(r, fminf(d0, d1)), cy = r - fminf(r, fminf(d2, d3)), alpha = fmaxf(0.f, fminf(1.f, r - sqrtf(cx * cx + cy * cy)));
                else
                    alpha = (d0 < 0.f ? 0.f : d0 > 1.f ? 1.f : d0) * (d1 < 0.f ? 0.f : d1 > 1.f ? 1.f : d1) * (d2 < 0.f ? 0.f : d2 > 1.f ? 1.f : d2) * (d3 < 0.f ? 0.f : d3 > 1.f ? 1.f : d3);
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
        Entry *writeEntry(Entry::Type type, size_t begin, size_t size, void *src, size_t stride) {
            for (uint8_t *dst = data.base + begin, *end = dst + size; dst < end; dst += (stride ?: size))
                memcpy(dst, src, stride ?: size);
            return new (entries.alloc(1)) Entry(type, begin, begin + size);
        }
        Pages<uint8_t> data;
        Row<Entry> entries;
        Colorant clearColor;
    };
    static size_t writeContextsToBuffer(Context *contexts, size_t count,
                                        Transform *ctms,
                                        Colorant *colorants,
                                        Transform *clips,
                                        size_t pathsCount,
                                        size_t *begins,
                                        Buffer& buffer) {
        size_t size, sz, i, j, begin, end, cells, instances;
        size = pathsCount * (sizeof(Colorant) + 2 * sizeof(Transform));
        for (i = 0; i < count; i++)
            size += contexts[i].gpu.opaques.end * sizeof(GPU::Instance);
        for (i = 0; i < count; i++) {
            begins[i] = size;
            GPU& gpu = contexts[i].gpu;
            for (cells = 0, instances = 0, j = 0; j < gpu.allocator.passes.end; j++)
                cells += gpu.allocator.passes.base[j].cells, instances += gpu.allocator.passes.base[j].edgeInstances, instances += gpu.allocator.passes.base[j].fastInstances;
            size += instances * sizeof(GPU::Edge) + cells * sizeof(GPU::EdgeCell) + (gpu.outlines.end - gpu.outlinePaths + gpu.shapesCount - gpu.shapePaths + gpu.blends.end) * sizeof(GPU::Instance) + gpu.cache.segments.end * sizeof(Segment);
            for (j = 0; j < contexts[i].segments.size(); j++)
                size += contexts[i].segments[j].end * sizeof(Segment);
        }
        buffer.data.resize(size);
        
        Buffer::Entry *entry = buffer.writeEntry(Buffer::Entry::kColorants, 0, pathsCount * sizeof(Colorant), colorants, 0);
        entry = buffer.writeEntry(Buffer::Entry::kAffineTransforms, entry->end, pathsCount * sizeof(Transform), contexts[0].gpu.ctms, 0);
        entry = buffer.writeEntry(Buffer::Entry::kClips, entry->end, pathsCount * sizeof(Transform), clips, 0);
    
        for (begin = end = entry->end, i = 0; i < count; i++)
            if ((sz = contexts[i].gpu.opaques.end * sizeof(GPU::Instance)))
                memcpy(buffer.data.base + end, contexts[i].gpu.opaques.base, sz), end += sz;
        if (begin != end)
            new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::Entry::kOpaques, begin, end);
        return size;
    }
    static void writeContextToBuffer(Context *ctx,
                                     SceneList& list,
                                     Transform *ctms,
                                     Colorant *colorants,
                                     size_t begin,
                                     size_t liz,
                                     std::vector<Buffer::Entry>& entries,
                                     Buffer& buffer) {
        size_t j, iz, sbegins[ctx->segments.size()], size, base, count;
        std::vector<size_t> idxes;
        Ref<Scene> *scene = & list.scenes[0], *uscene = scene + list.scenes.size();
        for (base = 0, count = 0; scene < uscene; base = count, scene++) {
            count += scene->ref->paths.size();
            if (liz < count)
                break;
        }
        if (scene == uscene)
            return;
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
        for (GPU::Allocator::Pass *pass = ctx->gpu.allocator.passes.base, *upass = pass + ctx->gpu.allocator.passes.end; pass < upass; pass++, begin = entries.back().end) {
            GPU::EdgeCell *cell = (GPU::EdgeCell *)(buffer.data.base + begin), *c0 = cell;
            entries.emplace_back(Buffer::Entry::kEdgeCells, begin, begin + pass->cells * sizeof(GPU::EdgeCell)), idxes.emplace_back(0), begin = entries.back().end;
            
            GPU::Edge *edge = (GPU::Edge *)(buffer.data.base + begin);
            entries.emplace_back(Buffer::Entry::kEdges, begin, begin + pass->edgeInstances * sizeof(GPU::Edge)), idxes.emplace_back(0), begin = entries.back().end;

            GPU::Edge *fast = (GPU::Edge *)(buffer.data.base + begin);
            entries.emplace_back(Buffer::Entry::kFastEdges, begin, begin + pass->fastInstances * sizeof(GPU::Edge)), idxes.emplace_back(0), begin = entries.back().end;
            
            Buffer::Entry *entry;
            GPU::Instance *linst = ctx->gpu.blends.base + pass->li, *uinst = ctx->gpu.blends.base + pass->ui, *inst;
            int type = linst->iz & GPU::Instance::kShapes || linst->iz & GPU::Instance::kOutlines ? Buffer::Entry::kShapes : Buffer::Entry::kQuads;
            entries.emplace_back(Buffer::Entry::Type(type), begin, begin), idxes.emplace_back(pass->li), entry = & entries.back();
            for (inst = linst; inst < uinst; inst++) {
                for (iz = inst->iz & kPathIndexMask; iz - base >= scene->ref->paths.size(); scene++)
                    base += scene->ref->paths.size();
                
                type = inst->iz & GPU::Instance::kShapes || inst->iz & GPU::Instance::kOutlines ? Buffer::Entry::kShapes : Buffer::Entry::kQuads;
                if (type != entry->type)
                    begin = entry->end, entries.emplace_back(Buffer::Entry::Type(type), begin, begin), idxes.emplace_back(inst - ctx->gpu.blends.base), entry = & entries.back();
                if (inst->iz & GPU::Instance::kShapes) {
                    Path& path = scene->ref->paths[iz - base];
                    GPU::Instance *dst = (GPU::Instance *)(buffer.data.base + entry->end);
                    for (int k = 0; k < path.ref->shapesCount; k++, dst++)
                        new (dst) GPU::Instance(path.ref->shapes[k], iz, path.ref->circles[k] ? GPU::Instance::kCircle : GPU::Instance::kRect);
                    entry->end += path.ref->shapesCount * sizeof(GPU::Instance);
                } else if (inst->iz & GPU::Instance::kOutlines) {
                    size_t count = inst->outline.r.end - inst->outline.r.begin;
                    Segment *src = ctx->gpu.outlines.base + inst->outline.r.begin, *es = src + count;
                    GPU::Instance *dst = (GPU::Instance *)(buffer.data.base + entry->end), *dst0;
                    for (dst0 = dst; src < es; src++, dst++) {
                        new (dst) GPU::Instance(GPU::Outline(src, inst->outline.width), iz, GPU::Instance::kOutlines);
                        if (src->x0 == FLT_MAX && dst - dst0 > 1)
                            dst0->outline.prev = (int)(dst - dst0 - 1), (dst - 1)->outline.next = -dst0->outline.prev, dst0 = dst + 1;
                    }
                    entry->end += count * sizeof(GPU::Instance);
                } else {
                    entry->end += sizeof(GPU::Instance);
                    if (inst->iz & GPU::Instance::kMolecule) {
                        Range *mr = & ctx->gpu.molecules.ranges.base[inst->quad.begin];
                        GPU::Molecules::Molecule *mc = & ctx->gpu.molecules.cells.base[mr->begin];
                        for (uint32_t ic = uint32_t(cell - c0), c = mr->begin; c < mr->end; c++, ic++, cell++, mc++) {
                            cell->cell = inst->quad.cell, cell->cell.ux = mc->ux, cell->im = inst->quad.iy, cell->base = uint32_t(inst->quad.end);
                            for (j = mc->begin; j < mc->end; fast++)
                                fast->ic = ic, fast->i0 = j, j += kFastSegments, fast->i1 = j;
                            (fast - 1)->i1 = mc->end;
                        }
                    } else if (inst->iz & GPU::Instance::kEdge) {
                        cell->cell = inst->quad.cell, cell->im = 0, cell->base = uint32_t(sbegins[inst->quad.iy] + inst->quad.end);
                        uint32_t ic = uint32_t(cell - c0);
                        Index *is = ctx->gpu.indices.base + inst->quad.begin;
                        for (j = 0; j < inst->quad.count; j++, edge++) {
                            edge->ic = ic, edge->i0 = uint16_t(is++->i);
                            if (++j < inst->quad.count)
                                edge->i1 = uint16_t(is++->i);
                            else
                                edge->i1 = kNullIndex;
                        }
                        cell++;
                    }
                }
            }
        }
        for (int k = 0; k < idxes.size(); k++)
            if (entries[k].type == Buffer::Entry::kQuads)
                memcpy(buffer.data.base + entries[k].begin, ctx->gpu.blends.base + idxes[k], entries[k].end - entries[k].begin);
        ctx->gpu.empty();
        for (j = 0; j < ctx->segments.size(); j++)
            ctx->segments[j].empty();
    }
};
