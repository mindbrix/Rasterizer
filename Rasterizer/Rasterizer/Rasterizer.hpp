//
//  Rasterizer.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 17/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "Rasterizer.h"
#import "xxhash.h"
#import <unordered_map>
#import <vector>
#pragma clang diagnostic ignored "-Wcomma"

struct Rasterizer {
    struct Transform {
        Transform() : a(1.f), b(0.f), c(0.f), d(1.f), tx(0.f), ty(0.f) {}
        Transform(float a, float b, float c, float d, float tx, float ty) : a(a), b(b), c(c), d(d), tx(tx), ty(ty) {}
        static Transform rst(float theta, float sx = 1.f, float sy = 1.f, float tx = 0.f, float ty = 0.f) {
            float sine, cosine;  __sincosf(theta, & sine, & cosine);
            return { sx * cosine, sy * sine, sx * -sine, sy * cosine, tx, ty };
        }
        inline Transform concat(Transform t) const {
            return {
                t.a * a + t.b * c, t.a * b + t.b * d,
                t.c * a + t.d * c, t.c * b + t.d * d,
                t.tx * a + t.ty * c + tx, t.tx * b + t.ty * d + ty
            };
        }
        inline Transform preconcat(Transform t, float ax, float ay) const {
            return Transform(t.a, t.b, t.c, t.d, t.tx + ax, t.ty + ay).concat(Transform(a, b, c, d, tx - ax, ty - ay));
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
        inline float det() const { return a * d - b * c; }
        float a, b, c, d, tx, ty;
    };
    struct Bounds {
        Bounds() : lx(FLT_MAX), ly(FLT_MAX), ux(-FLT_MAX), uy(-FLT_MAX) {}
        Bounds(float lx, float ly, float ux, float uy) : lx(lx), ly(ly), ux(ux), uy(uy) {}
        Bounds(Transform t) :
            lx(t.tx + (t.a < 0.f ? t.a : 0.f) + (t.c < 0.f ? t.c : 0.f)),
            ly(t.ty + (t.b < 0.f ? t.b : 0.f) + (t.d < 0.f ? t.d : 0.f)),
            ux(t.tx + (t.a > 0.f ? t.a : 0.f) + (t.c > 0.f ? t.c : 0.f)),
            uy(t.ty + (t.b > 0.f ? t.b : 0.f) + (t.d > 0.f ? t.d : 0.f)) {}
        inline bool contains(Bounds b) const {
            return lx <= b.lx && ux >= b.ux && ly <= b.ly && uy >= b.uy;
        }
        inline void extend(float x, float y) {
            if (x < lx) lx = x;  if (x > ux) ux = x;  if (y < ly) ly = y;  if (y > uy) uy = y;
        }
        inline void extend(Bounds b) {
            extend(b.lx, b.ly), extend(b.ux, b.uy);
        }
        inline Transform fit(Bounds b) const {
            float sx = (ux - lx) / (b.ux - b.lx), sy = (uy - ly) / (b.uy - b.ly), s = sx < sy ? sx : sy;
            return { s, 0.f, 0.f, s, -s * b.lx, -s * b.ly };
        }
        inline Bounds inset(float dx, float dy) const {
            return dx * 2.f < ux - lx && dy * 2.f < uy - ly ? Bounds(lx + dx, ly + dy, ux - dx, uy - dy) : *this;
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
    struct Colorant {
        Colorant(uint8_t b, uint8_t g, uint8_t r, uint8_t a) : b(b), g(g), r(r), a(a) {}
        uint8_t b, g, r, a;
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
        T* operator->() { return ref; }
        T *ref = nullptr;
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
        inline T *alloc(size_t n) {
            end += n;
            if (memory->size < end)
                memory->resize(end * 1.5), base = memory->addr;
            return base + end - n;
        }
        inline T& back() { return base[end - 1]; }
        Row<T>& empty() { end = idx = 0; return *this; }
        void reset() { end = idx = 0, base = nullptr, memory = Ref<Memory<T>>(); }
        Row<T>& operator+(const T *src) { if (src) { do *(alloc(1)) = *src; while (*src++);  --end; } return *this; }
        Row<T>& operator+(const int n) { char buf[32]; bzero(buf, sizeof(buf)), sprintf(buf, "%d", n); operator+((T *)buf); return *this; }

        Ref<Memory<T>> memory;
        size_t end = 0, idx = 0;
        T *base = nullptr;
    };
    struct Geometry {
        static float normalizeRadians(float a) { return fmodf(a >= 0.f ? a : (kTau - (fmodf(-a, kTau))), kTau); }
        struct Point16 {
            Point16(uint16_t x, uint16_t y) : x(x), y(y) {}
            int16_t x, y;
        };
        enum Type { kMove, kLine, kQuadratic, kCubic, kClose, kCountSize };
        
        void prepare(size_t moveCount, size_t lineCount, size_t quadCount, size_t cubicCount, size_t closeCount) {
            size_t size = moveCount + lineCount + 2 * quadCount + 3 * cubicCount + closeCount;
            types.alloc(size), types.empty(), points.alloc(size * 2), points.empty(), molecules.alloc(moveCount), molecules.empty();
            size_t p16sSize = 5 * moveCount + lineCount + 2 * quadCount + 3 * cubicCount;
            p16s.alloc(p16sSize), p16s.empty(), p16cnts.alloc(p16sSize / kFastSegments), p16cnts.empty();
        }
        void update(Type type, size_t size, float *p) {
            counts[type]++;
            uint8_t *tp = types.alloc(size);
            for (int i = 0; i < size; i++, p += 2)
                tp[i] = type, bounds.extend(p[0], p[1]), molecules.back().extend(p[0], p[1]);
        }
        void validate() {
            Bounds *b = molecules.end ? & molecules.back() : nullptr;
            if (b && b->lx == b->ux && b->ly == b->uy) {
                for (uint8_t *type = types.base + points.idx / 2, *end = types.base + points.end / 2; type < end;)
                    counts[*type]--, type += *type == kMove || *type == kLine || *type == kClose ? 1 : *type == kQuadratic ? 2 : 3;
                molecules.end--, types.end = points.idx / 2, points.end = points.idx, bounds = Bounds();
                for (int i = 0; i < molecules.end; i++)
                    bounds.extend(molecules.base[i]);
            }
        }
        size_t upperBound(float det) {
            float s = sqrtf(sqrtf(det < 1e-2f ? 1e-2f : det));
            size_t cubics = cubicSums == 0 ? 0 : (det < 1.f ? ceilf(s * (cubicSums + 2.f)) : ceilf(s) * cubicSums);
            return cubics + 2 * (molecules.end + counts[kLine] + counts[kQuadratic] + counts[kCubic]);
        }
        void addBounds(Bounds b) { moveTo(b.lx, b.ly), lineTo(b.ux, b.ly), lineTo(b.ux, b.uy), lineTo(b.lx, b.uy), lineTo(b.lx, b.ly); }
        void addEllipse(Bounds b) {
            const float t = 0.5f - 2.f / 3.f * (M_SQRT2 - 1.f), s = 1.f - t, mx = 0.5f * (b.lx + b.ux), my = 0.5f * (b.ly + b.uy);
            moveTo(b.ux, my);
            cubicTo(b.ux, t * b.ly + s * b.uy, t * b.lx + s * b.ux, b.uy, mx, b.uy);
            cubicTo(s * b.lx + t * b.ux, b.uy, b.lx, t * b.ly + s * b.uy, b.lx, my);
            cubicTo(b.lx, s * b.ly + t * b.uy, s * b.lx + t * b.ux, b.ly, mx, b.ly);
            cubicTo(t * b.lx + s * b.ux, b.ly, b.ux, s * b.ly + t * b.uy, b.ux, my);
        }
        void addArc(float x, float y, float r, float a0, float a1) {
            float da, f, ax, ay, bx, by, sx, sy, ex, ey;
            a0 = normalizeRadians(a0), a1 = normalizeRadians(a1);
            da = a1 > a0 ? a1 - a0 : a1 - a0 + kTau, f = 4.f * tanf(da * 0.25f) / 3.f;
            ax = r * cosf(a0), ay = r * sinf(a0), bx = r * cosf(a1), by = r * sinf(a1);
            sx = x + ax, sy = y + ay, ex = x + bx, ey = y + by;
            moveTo(sx, sx), cubicTo(sx - f * ay, sy + f * ax, ex + f * by, ey - f * bx, ex, ey);
        }
        void moveTo(float x, float y) {
            validate(), new (molecules.alloc(1)) Bounds();
            points.idx = points.end;  float *pts = points.alloc(2);  x0 = pts[0] = x, y0 = pts[1] = y;
            update(kMove, 1, pts);
        }
        void lineTo(float x1, float y1) {
            if (x0 != x1 || y0 != y1) {
                float *pts = points.alloc(2);  x0 = pts[0] = x1, y0 = pts[1] = y1;
                update(kLine, 1, pts);
            }
        }
        void quadTo(float x1, float y1, float x2, float y2) {
            if (x0 != x2 || y0 != y2) {
                float ax = x2 - x1, ay = y2 - y1, bx = x1 - x0, by = y1 - y0, adot = ax * ax + ay * ay, bdot = bx * bx + by * by, det = bx * ay - by * ax, dot = ax * bx + ay * by;
                if (dot == 0.f || dot * dot / (adot * bdot) > 0.999695413509548f) {
                    if (dot < 0.f && det)
                        lineTo((x0 + x2) * 0.25f + x1 * 0.5f, (y0 + y2) * 0.25f + y1 * 0.5f);
                    lineTo(x2, y2);
                } else {
                    float *pts = points.alloc(4);  pts[0] = x1, pts[1] = y1, x0 = pts[2] = x2, y0 = pts[3] = y2;
                    update(kQuadratic, 2, pts);
                    ax -= bx, ay -= by, dot = ax * ax + ay * ay, maxDot = maxDot > dot ? maxDot : dot;
                }
            }
        }
        void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
            float bx, ax, by, ay, dot;
            bx = 3.f * (x2 - x1), ax = x3 - x0 - bx, by = 3.f * (y2 - y1), ay = y3 - y0 - by, dot = ax * ax + ay * ay;
            if (dot < 1e-2f)
                quadTo((3.f * (x1 + x2) - x0 - x3) * 0.25f, (3.f * (y1 + y2) - y0 - y3) * 0.25f, x3, y3);
            else {
                float *pts = points.alloc(6);  pts[0] = x1, pts[1] = y1, pts[2] = x2, pts[3] = y2, pts[4] = x3, pts[5] = y3;
                update(kCubic, 3, pts);
                bx -= 3.f * (x1 - x0), by -= 3.f * (y1 - y0), dot += bx * bx + by * by, x0 = x3, y0 = y3;
                cubicSums += ceilf(sqrtf(sqrtf(dot))), maxDot = maxDot > dot ? maxDot : dot;
            }
        }
        void close() {
            float *pts = points.alloc(2);  pts[0] = x0, pts[1] = y0;
            update(kClose, 1, pts);
        }
        size_t hash() {
            xxhash = xxhash ?: XXH64(points.base, points.end * sizeof(float), XXH64(types.base, types.end * sizeof(uint8_t), 0));
            return xxhash;
        }
        inline void writePoint16(float x, float y, Bounds& b, uint32_t curve) {
            uint16_t x16 = (x - b.lx) / (b.ux - b.lx) * 32767.f, y16 = (y - b.ly) / (b.uy - b.ly) * 32767.f;
            new (p16s.alloc(1)) Point16(x16 | ((curve & 2) << 14), y16 | ((curve & 1) << 15));
        }
        static void WriteSegment16(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
            Geometry *g = (Geometry *)info;  float bx, by, len;
            if (x0 != FLT_MAX) {
                bx = x1 - x0, by = y1 - y0, len = bx == 0.f && by == 0.f ? 1.f : sqrtf(bx * bx + by * by), bx /= len, by /= len;
                if (g->ax0 == FLT_MAX)
                    g->ax0 = bx, g->ay0 = by;
                else if (g->ax * bx + g->ay * by < -0.999847695156391f)
                    g->writePoint16(x0, y0, g->bounds, curve);
                g->writePoint16(x0, y0, g->bounds, curve), g->ax = bx, g->ay = by;
            } else {
                g->writePoint16(x1, y1, g->bounds, 0), g->ax0 = FLT_MAX;
                size_t end = (g->p16s.end + kFastSegments - 1) / kFastSegments * kFastSegments, icnt = (end - g->p16s.idx) / kFastSegments;
                uint8_t *cnt, *cend;  short *off, *off0;  int i, segcount = int(g->p16s.end - g->p16s.idx - 1); bool empty, last, skiplast;
                for (cnt = g->p16cnts.alloc(icnt), cend = cnt + icnt; cnt < cend; cnt++, segcount -= kFastSegments)
                    *cnt = segcount < 0 ? 0 : segcount > 4 ? 4 : segcount;
                skiplast = curve & 0x1, empty = cnt[-1] == 0, last = cnt[-1] == 1 && skiplast, cnt[-1] |= 0x80, cnt[empty ? -2 : -1] |= (skiplast ? 0x8 : 0x0);
                for (off0 = off = g->p16offs.alloc(2 * icnt), i = 0; i < icnt; i++, off += 2)
                    off[0] = -1, off[1] = 1;
                off0[0] = short((curve & 0x2) != 0) * (g->p16s.end - g->p16s.idx - 2), off[empty || last ? - 3 : -1] = -off0[0];
                g->p16s.alloc(end - g->p16s.end), g->p16s.idx = g->p16s.end;
            }
        }
        size_t refCount = 0, xxhash = 0, minUpper = 0, cubicSums = 0, counts[kCountSize] = { 0, 0, 0, 0, 0 };
        Row<uint8_t> types;  Row<float> points;
        Row<Point16> p16s;  Row<uint8_t> p16cnts;  Row<short> p16offs;  Row<Bounds> molecules;
        float x0 = 0.f, y0 = 0.f, maxDot = 0.f, ax0 = FLT_MAX, ay0, ax, ay;
        Bounds bounds;
    };
    typedef Ref<Geometry> Path;
    
    struct Scene {
        struct Cache {
            struct Entry {  size_t size;  bool hasMolecules;  float maxDot, *mols;  uint16_t *p16s;  uint8_t *p16cnts;  short *p16offs;  };
            size_t refCount = 0;
            Row<uint32_t> ips;  Row<Entry> entries;
            std::unordered_map<size_t, size_t> map;
        };
        template<typename T>
        struct Vector {
            uint64_t refCount;  T *base;  std::vector<T> src, dst;
            void add(T obj) {  src.emplace_back(obj), dst.emplace_back(obj), base = & dst[0]; }
        };
        enum Flags { kInvisible = 1 << 0, kFillEvenOdd = 1 << 1, kRoundCap = 1 << 2, kSquareCap = 1 << 3 };
        void addPath(Path path, Transform ctm, Colorant color, float width, uint8_t flag) {
            path->validate();
            if (path->types.end > 1 && *path->types.base == Geometry::kMove && (path->bounds.lx != path->bounds.ux || path->bounds.ly != path->bounds.uy)) {
                count++, weight += path->types.end;
                auto it = cache->map.find(path->hash());
                if (it != cache->map.end())
                    *(cache->ips.alloc(1)) = uint32_t(it->second);
                else {
                    if (path->p16s.end == 0) {
                        float w = path->bounds.ux - path->bounds.lx, h = path->bounds.uy - path->bounds.ly, dim = w > h ? w : h;
                        divideGeometry(path.ref, Transform(), Bounds(), true, true, true, path.ref, Geometry::WriteSegment16, bisectQuadratic, 0.f, divideCubic, -kCubicPrecision / (dim > kMoleculesHeight ? 1.f : kMoleculesHeight / dim));
                    }
                    Cache::Entry *e = cache->entries.alloc(1);  e->size = path->p16s.end, e->hasMolecules = path->molecules.end > 1, e->maxDot = path->maxDot, e->mols = (float *)path->molecules.base, e->p16s = (uint16_t *)path->p16s.base, e->p16cnts = path->p16cnts.base, e->p16offs = path->p16offs.base;
                    *(cache->ips.alloc(1)) = uint32_t(cache->map.size()), cache->map.emplace(path->hash(), cache->map.size());
                }
                path->minUpper = path->minUpper ?: path->upperBound(kMinUpperDet), xxhash = XXH64(& path->xxhash, sizeof(path->xxhash), xxhash);
                paths->dst.emplace_back(path), paths->base = & paths->dst[0], bnds->add(path->bounds), ctms->add(ctm), colors->add(color), widths->add(width), flags->add(flag);
            }
        }
        Bounds bounds() {
            Bounds b;
            for (int i = 0; i < count; i++)
                if ((flags->base[i] & kInvisible) == 0)
                    b.extend(Bounds(bnds->base[i].inset(-0.5f * widths->base[i], -0.5f * widths->base[i]).unit(ctms->base[i])));
            return b;
        }
        size_t count = 0, xxhash = 0, weight = 0;  uint64_t tag = 1;
        Ref<Cache> cache;  Ref<Vector<Path>> paths;
        Ref<Vector<Transform>> ctms;  Ref<Vector<Bounds>> bnds;  Ref<Vector<Colorant>> colors;  Ref<Vector<float>> widths;  Ref<Vector<uint8_t>> flags;
    };
    struct SceneList {
        Bounds bounds() {
            Bounds b;
            for (int i = 0; i < scenes.size(); i++)
                b.extend(Bounds(clips[i]).intersect(Bounds(scenes[i].bounds().unit(ctms[i]))));
            return b;
        }
        SceneList& empty() {
            pathsCount = 0, scenes.resize(0), ctms.resize(0), clips.resize(0);
            return *this;
        }
        SceneList& addList(SceneList& list) {
            for (int i = 0; i < list.scenes.size(); i++)
                addScene(list.scenes[i], list.ctms[i], list.clips[i]);
            return *this;
        }
        SceneList& addScene(Scene scene, Transform ctm = Transform(), Transform clip = Transform(1e12f, 0.f, 0.f, 1e12f, -5e11f, -5e11f)) {
            if (scene.weight)
                pathsCount += scene.count, scenes.emplace_back(scene), ctms.emplace_back(ctm), clips.emplace_back(clip);
            return *this;
        }
        size_t pathsCount = 0;  std::vector<Scene> scenes;  std::vector<Transform> ctms, clips;
    };
    struct Range {
        Range(size_t begin, size_t end) : begin(int(begin)), end(int(end)) {}
        int begin, end;
    };
    struct Index {
        Index(uint16_t x, uint16_t i) : x(x), i(i) {}
        uint16_t x, i;
        inline bool operator< (const Index& other) const { return x < other.x; }
    };
    struct Segment {
        Segment(float x0, float y0, float x1, float y1, uint32_t curve) : ix0((*((uint32_t *)& x0) & ~3) | curve), y0(y0), x1(x1), y1(y1) {}
        union { float x0; uint32_t ix0; };  float y0, x1, y1;
    };
    struct Cell {
        uint16_t lx, ly, ux, uy, ox, oy;
    };
    struct Quad {
        Cell cell;  short cover;  int base, biid;
    };
    struct Outline {
        Segment s;  short prev, next;
    };
    struct Instance {
        enum Type { kPCurve = 1 << 24, kEvenOdd = 1 << 24, kRoundCap = 1 << 25, kEdge = 1 << 26, kNCurve = 1 << 27, kSquareCap = 1 << 28, kOutlines = 1 << 29, kFastEdges = 1 << 30, kMolecule = 1 << 31 };
        Instance(size_t iz) : iz(uint32_t(iz)) {}
        uint32_t iz;  union { Quad quad;  Outline outline; };
    };
    struct Blend : Instance {
        Blend(size_t iz) : Instance(iz) {}
        union { struct { int count, iy, begin, idx; } data;  Bounds clip; };
    };
    struct Edge {
        uint32_t ic;  enum Flags { a0 = 1 << 31, a1 = 1 << 30, ue0 = 0xF << 26, ue1 = 0xF << 22, kMask = ~(a0 | a1 | ue0 | ue1) };
        union { uint16_t i0;  short prev; };  union { uint16_t ux;  short next; };
    };
    struct Buffer {
        enum Type { kQuadEdges, kFastEdges, kFastOutlines, kQuadOutlines, kFastMolecules, kQuadMolecules, kOpaques, kInstances, kInstanceTransforms, kSegmentsBase, kPointsBase, kInstancesBase, kTransformBase, kP16Outlines, kP16Miters };
        struct Entry {
            Entry(Type type, size_t begin, size_t end) : type(type), begin(begin), end(end) {}
            Type type;  size_t begin, end;
        };
        ~Buffer() { if (base) free(base); }
        void prepare(size_t pathsCount) {
            size_t szcolors = pathsCount * sizeof(Colorant), szctms = pathsCount * sizeof(Transform), szwidths = pathsCount * sizeof(float), szbounds = pathsCount * sizeof(Bounds);
            headerSize = szcolors + 2 * szctms + szwidths + szbounds;
            this->pathsCount = pathsCount, colors = 0, ctms = colors + szcolors, clips = ctms + szctms, widths = clips + szctms, bounds = widths + szwidths;
            resize(headerSize), entries.empty();
        }
        void resize(size_t n, size_t copySize = 0) {
            size_t allocation = (n + kPageSize - 1) / kPageSize * kPageSize;
            if (size < allocation) {
                size = allocation;
                uint8_t *resized;  posix_memalign((void **)& resized, kPageSize, size);
                if (base) {
                    if (copySize)
                        memcpy(resized, base, copySize);
                    free(base);
                }
                base = resized;
            }
        }
        uint8_t *base = nullptr;
        Row<Entry> entries;
        bool useCurves = false, fastOutlines = false, p16Outlines = true;
        Colorant clearColor = Colorant(255, 255, 255, 255);
        size_t colors, ctms, clips, widths, bounds, pathsCount, headerSize, size = 0;
    };
    struct Allocator {
        struct Pass {
            Pass(size_t idx) : idx(idx) {}
            size_t idx, size = 0, counts[7] = { 0, 0, 0, 0, 0, 0, 0 };
            size_t count() { return counts[0] + counts[1] + counts[2] + counts[3] + counts[4] + counts[5] + counts[6]; }
        };
        void empty(Bounds device) {
            full = device, sheet = strip = fast = molecules = Bounds(0.f, 0.f, 0.f, 0.f), passes.empty(), new (passes.alloc(1)) Pass(0);
        }
        inline void alloc(float w, float h, size_t idx, Cell *cell) {
            Bounds *b;  float hght;
            if (h <= kfh)
                b = & strip, hght = kfh;
            else if (h <= kFastHeight)
                b = & fast, hght = kFastHeight;
            else
                b = & molecules, hght = kMoleculesHeight;
            if (b->ux - b->lx < w) {
                if (sheet.uy - sheet.ly < hght)
                    sheet = full, strip = fast = molecules = Bounds(0.f, 0.f, 0.f, 0.f), new (passes.alloc(1)) Pass(idx);
                b->lx = sheet.lx, b->ly = sheet.ly, b->ux = sheet.ux, b->uy = sheet.ly + hght, sheet.ly = b->uy;
            }
            cell->ox = b->lx, cell->oy = b->ly, b->lx += w;
        }
        Row<Pass> passes;  enum CountType { kFastEdges, kQuadEdges, kFastOutlines, kQuadOutlines, kFastMolecules, kQuadMolecules, kP16Outlines };
        Bounds full, sheet, strip, fast, molecules;
    };
    struct Context {
        void prepare(Bounds dev, size_t pathsCount, size_t slz, size_t suz) {
            device = dev, empty(), allocator.empty(device), this->slz = slz, this->suz = suz;
            size_t fatlines = 1.f + ceilf((dev.uy - dev.ly) * krfh);
            if (indices.size() != fatlines)
                indices.resize(fatlines), uxcovers.resize(fatlines);
            bzero(fasts.alloc(pathsCount), pathsCount * sizeof(*fasts.base));
        }
        void drawList(SceneList& list, Transform view, uint32_t *idxs, Transform *ctms, Colorant *colors, Transform *clipctms, float *widths, Bounds *bounds, Buffer *buffer) {
            size_t lz, uz, i, clz, cuz, iz, is, ip, size;  Scene *scene = & list.scenes[0];  uint8_t flags;
            float clipx, clipy, ax, ay, cx, cy, cliph, clipw, diam, err, e0, e1, det, width, uw;
            for (lz = uz = i = 0; i < list.scenes.size(); i++, scene++, lz = uz) {
                Transform ctm = view.concat(list.ctms[i]), clipctm = view.concat(list.clips[i]), inv = clipctm.invert(), m, unit;
                Bounds clipbnds = Bounds(clipctm).integral().intersect(device), dev, clip, *b;
                clipx = 0.5f * (clipbnds.lx + clipbnds.ux), clipy = 0.5f * (clipbnds.ly + clipbnds.uy);
                clipw = 0.5f * (clipbnds.ux - clipbnds.lx), cliph = 0.5f * (clipbnds.uy - clipbnds.ly);
                err = fminf(1e-2f, 1e-2f / sqrtf(fabsf(clipctm.det()))), e0 = -err, e1 = 1.f + err;
                uz = lz + scene->count, clz = lz < slz ? slz : lz > suz ? suz : lz, cuz = uz < slz ? slz : uz > suz ? suz : uz;
                for (is = clz - lz, iz = clz; iz < cuz; iz++, is++) {
                    if ((flags = scene->flags->base[is]) & Scene::Flags::kInvisible)
                        continue;
                    m = ctm.concat(scene->ctms->base[is]), det = fabsf(m.det()), uw = scene->widths->base[is], b = & scene->bnds->base[is];
                    if (buffer->p16Outlines) {
                        if (uw) {
                            ax = b->ux - b->lx, ay = b->uy - b->ly, cx = 0.5f * (b->lx + b->ux), cy = 0.5f * (b->ly + b->uy), diam = sqrtf((ax * ax + ay * ay) * det);
                            if (fabsf(cx * m.a + cy * m.c + m.tx - clipx) > clipw + diam || fabsf(cx * m.b + cy * m.d + m.ty - clipy) > cliph + diam)
                                continue;
                            ctms[iz] = m, bounds[iz] = *b, widths[iz] = uw * (uw > 0.f ? sqrtf(det) : -1.f), clipctms[iz] = clipctm, idxs[iz] = uint32_t((i << 20) | is);
    
                            Blend *inst = new (blends.alloc(1)) Blend(iz | Instance::kOutlines | bool(flags & Scene::kRoundCap) * Instance::kRoundCap | bool(flags & Scene::kSquareCap) * Instance::kSquareCap);
                            ip = scene->cache->ips.base[is], size = scene->cache->entries.base[ip].size;
                            if (fasts.base[lz + ip]++ == 0)
                                p16total += size;
                            inst->data.idx = int(lz + ip);
                            Allocator::Pass& pass = allocator.passes.back();  pass.size++, pass.counts[Allocator::kP16Outlines] += size / kFastSegments;
                            outlineInstances++;
                        }
                        continue;
                    }
                    width = uw * (uw > 0.f ? sqrtf(det) : -1.f);
                    unit = b->unit(m), dev = Bounds(unit).inset(-width, -width), clip = dev.integral().intersect(clipbnds);
                    if (clip.lx != clip.ux && clip.ly != clip.uy) {
                        ctms[iz] = m, widths[iz] = width, clipctms[iz] = clipctm, idxs[iz] = uint32_t((i << 20) | is);
                        Geometry *g = scene->paths->base[is].ref;
                        bool useMolecules = clip.uy - clip.ly <= kMoleculesHeight && clip.ux - clip.lx <= kMoleculesHeight;
                        if (width && !(buffer->fastOutlines && useMolecules && width <= 2.f)) {
                           Blend *inst = new (blends.alloc(1)) Blend(iz | Instance::kOutlines | bool(flags & Scene::kRoundCap) * Instance::kRoundCap | bool(flags & Scene::kSquareCap) * Instance::kSquareCap);
                           inst->clip = clip.contains(dev) ? Bounds(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX) : clip.inset(-width, -width);
                           if (det > 1e2f) {
                               size_t count = 0;
                               divideGeometry(g, m, inst->clip, false, false, true, & count, Outliner::CountSegment);
                               outlineInstances += count;
                           } else
                               outlineInstances += det < kMinUpperDet ? g->minUpper : g->upperBound(det);
                           outlinePaths++, allocator.passes.back().size++;
                       } else if (useMolecules) {
                            bounds[iz] = *b, ip = scene->cache->ips.base[is], size = scene->cache->entries.base[ip].size;
                            if (fasts.base[lz + ip]++ == 0)
                                p16total += size;
                            bool fast = !buffer->useCurves || det * scene->cache->entries.base[ip].maxDot < 16.f;
                            Blend *inst = new (blends.alloc(1)) Blend(iz | Instance::kMolecule | bool(flags & Scene::kFillEvenOdd) * Instance::kEvenOdd | fast * Instance::kFastEdges);
                            inst->quad.cell.lx = clip.lx, inst->quad.cell.ly = clip.ly, inst->quad.cell.ux = clip.ux, inst->quad.cell.uy = clip.uy, inst->quad.cover = 0, inst->data.idx = int(lz + ip);
                            allocator.alloc(clip.ux - clip.lx, clip.uy - clip.ly, blends.end - 1, & inst->quad.cell);
                            Allocator::Pass& pass = allocator.passes.back();  pass.size++, pass.counts[width ? (fast ? Allocator::kFastOutlines : Allocator::kQuadOutlines) : (fast ? Allocator::kFastMolecules : Allocator::kQuadMolecules)] += size / kFastSegments;
                        } else {
                            CurveIndexer idxr;  idxr.clip = clip, idxr.indices = & indices[0] - int(clip.ly * krfh), idxr.uxcovers = & uxcovers[0] - int(clip.ly * krfh), idxr.useCurves = buffer->useCurves, idxr.dst = segments.alloc(det < kMinUpperDet ? g->minUpper : g->upperBound(det));
                            float sx = 1.f - 2.f * kClipMargin / (clip.ux - clip.lx), sy = 1.f - 2.f * kClipMargin / (clip.uy - clip.ly);
                            m = { m.a * sx, m.b * sy, m.c * sx, m.d * sy, m.tx * sx + clip.lx * (1.f - sx) + kClipMargin, m.ty * sy + clip.ly * (1.f - sy) + kClipMargin };
                            divideGeometry(g, m, clip, clip.contains(dev), true, false, & idxr, CurveIndexer::WriteSegment);
                            Bounds clu = Bounds(inv.concat(unit));
                            bool opaque = colors[iz].a == 255 && !(clu.lx < e0 || clu.ux > e1 || clu.ly < e0 || clu.uy > e1);
                            bool fast = !buffer->useCurves || (g->counts[Geometry::kQuadratic] == 0 && g->counts[Geometry::kCubic] == 0);
                            writeSegmentInstances(clip, flags & Scene::kFillEvenOdd, iz, opaque, fast, *this);
                            segments.idx = segments.end = idxr.dst - segments.base;
                        }
                    }
                }
            }
        }
        void empty() { outlinePaths = outlineInstances = p16total = 0, blends.empty(), fasts.empty(), opaques.empty(), segments.empty();  for (int i = 0; i < indices.size(); i++)  indices[i].empty(), uxcovers[i].empty();  }
        void reset() { outlinePaths = outlineInstances = p16total = 0, blends.reset(), fasts.reset(), opaques.reset(), segments.reset(), indices.resize(0), uxcovers.resize(0); }
        size_t slz, suz, outlinePaths = 0, outlineInstances = 0, p16total;
        Bounds device;  Allocator allocator;
        Row<uint32_t> fasts;  Row<Blend> blends;  Row<Instance> opaques;  Row<Segment> segments;
        std::vector<Row<Index>> indices;  std::vector<Row<int16_t>> uxcovers;
    };
    typedef void (*SegmentFunction)(float x0, float y0, float x1, float y1, uint32_t curve, void *info);
    typedef void (*QuadFunction)(float x0, float y0, float x1, float y1, float x2, float y2, SegmentFunction function, void *info, float s);
    typedef void (*CubicFunction)(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, SegmentFunction function, void *info, float s);
    
    static void divideGeometry(Geometry *g, Transform m, Bounds clip, bool unclipped, bool polygon, bool mark, void *info, SegmentFunction function, QuadFunction quadFunction = bisectQuadratic, float quadScale = 0.f, CubicFunction cubicFunction = divideCubic, float cubicScale = kCubicPrecision) {
        bool closed, closeSubpath = false;  float *p = g->points.base, sx = FLT_MAX, sy = FLT_MAX, x0 = FLT_MAX, y0 = FLT_MAX, x1, y1, x2, y2, x3, y3, ly, uy, lx, ux;
        for (uint8_t *type = g->types.base, *end = type + g->types.end; type < end; )
            switch (*type) {
                case Geometry::kMove:
                    if ((closed = (polygon || closeSubpath) && (sx != x0 || sy != y0)))
                        line(x0, y0, sx, sy, clip, unclipped, polygon, info, function);
                    if (mark && sx != FLT_MAX)
                        (*function)(FLT_MAX, FLT_MAX, sx, sy, (uint32_t(closeSubpath) << 1) | uint32_t((closed && !closeSubpath) || (!polygon && closeSubpath)), info);
                    sx = x0 = p[0] * m.a + p[1] * m.c + m.tx, sy = y0 = p[0] * m.b + p[1] * m.d + m.ty, p += 2, type++, closeSubpath = false;
                    break;
                case Geometry::kLine:
                    x1 = p[0] * m.a + p[1] * m.c + m.tx, y1 = p[0] * m.b + p[1] * m.d + m.ty;
                    line(x0, y0, x1, y1, clip, unclipped, polygon, info, function);
                    x0 = x1, y0 = y1, p += 2, type++;
                    break;
                case Geometry::kQuadratic:
                    x1 = p[0] * m.a + p[1] * m.c + m.tx, y1 = p[0] * m.b + p[1] * m.d + m.ty;
                    x2 = p[2] * m.a + p[3] * m.c + m.tx, y2 = p[2] * m.b + p[3] * m.d + m.ty;
                    if (unclipped)
                        (*quadFunction)(x0, y0, x1, y1, x2, y2, function, info, quadScale);
                    else {
                        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2;
                        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2;
                        if (ly < clip.uy && uy > clip.ly) {
                            lx = x0 < x1 ? x0 : x1, lx = lx < x2 ? lx : x2;
                            ux = x0 > x1 ? x0 : x1, ux = ux > x2 ? ux : x2;
                            if (polygon || !(ux < clip.lx || lx > clip.ux)) {
                                if (ly < clip.ly || uy > clip.uy || lx < clip.lx || ux > clip.ux)
                                    clipQuadratic(x0, y0, x1, y1, x2, y2, clip, lx, ly, ux, uy, polygon, function, quadFunction, info, quadScale);
                                else
                                    (*quadFunction)(x0, y0, x1, y1, x2, y2, function, info, quadScale);
                            }
                        }
                    }
                    x0 = x2, y0 = y2, p += 4, type += 2;
                    break;
                case Geometry::kCubic:
                    x1 = p[0] * m.a + p[1] * m.c + m.tx, y1 = p[0] * m.b + p[1] * m.d + m.ty;
                    x2 = p[2] * m.a + p[3] * m.c + m.tx, y2 = p[2] * m.b + p[3] * m.d + m.ty;
                    x3 = p[4] * m.a + p[5] * m.c + m.tx, y3 = p[4] * m.b + p[5] * m.d + m.ty;
                    if (unclipped)
                        (*cubicFunction)(x0, y0, x1, y1, x2, y2, x3, y3, function, info, cubicScale);
                    else {
                        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2, ly = ly < y3 ? ly : y3;
                        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2, uy = uy > y3 ? uy : y3;
                        if (ly < clip.uy && uy > clip.ly) {
                            lx = x0 < x1 ? x0 : x1, lx = lx < x2 ? lx : x2, lx = lx < x3 ? lx : x3;
                            ux = x0 > x1 ? x0 : x1, ux = ux > x2 ? ux : x2, ux = ux > x3 ? ux : x3;
                            if (polygon || !(ux < clip.lx || lx > clip.ux)) {
                                if (ly < clip.ly || uy > clip.uy || lx < clip.lx || ux > clip.ux)
                                    clipCubic(x0, y0, x1, y1, x2, y2, x3, y3, clip, lx, ly, ux, uy, polygon, function, cubicFunction, info, cubicScale);
                                else
                                    (*cubicFunction)(x0, y0, x1, y1, x2, y2, x3, y3, function, info, cubicScale);
                            }
                        }
                    }
                    x0 = x3, y0 = y3, p += 6, type += 3;
                    break;
                case Geometry::kClose:
                    p += 2, type++, closeSubpath = true;
                    break;
            }
        if ((closed = (polygon || closeSubpath) && (sx != x0 || sy != y0)))
            line(x0, y0, sx, sy, clip, unclipped, polygon, info, function);
        if (mark)
            (*function)(FLT_MAX, FLT_MAX, sx, sy, (uint32_t(closeSubpath) << 1) | uint32_t((closed && !closeSubpath) || (!polygon && closeSubpath)), info);
    }
    static inline void line(float x0, float y0, float x1, float y1, Bounds clip, bool unclipped, bool polygon, void *info, SegmentFunction function) {
        if (unclipped)
            (*function)(x0, y0, x1, y1, 0, info);
        else {
            float ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
            if (ly < clip.uy && uy > clip.ly) {
                if (ly < clip.ly || uy > clip.uy || (x0 < x1 ? x0 : x1) < clip.lx || (x0 > x1 ? x0 : x1) > clip.ux)
                    clipLine(x0, y0, x1, y1, clip, polygon, function, info);
                else
                    (*function)(x0, y0, x1, y1, 0, info);
            }
        }
    }
    static void clipLine(float x0, float y0, float x1, float y1, Bounds clip, bool polygon, SegmentFunction function, void *info) {
        float dx = x1 - x0, t0 = (clip.lx - x0) / dx, t1 = (clip.ux - x0) / dx, dy = y1 - y0, sy0, sy1, sx0, sx1, mx, vx, ts[4], *t;
        if (dy == 0.f)
            ts[0] = 0.f, ts[3] = 1.f;
        else
            ts[0] = ((y0 < clip.ly ? clip.ly : y0 > clip.uy ? clip.uy : y0) - y0) / dy,
            ts[3] = ((y1 < clip.ly ? clip.ly : y1 > clip.uy ? clip.uy : y1) - y0) / dy;
        if (dx == 0.f)
            ts[1] = ts[0], ts[2] = ts[3];
        else
            ts[1] = t0 < t1 ? t0 : t1, ts[1] = ts[1] < ts[0] ? ts[0] : ts[1] > ts[3] ? ts[3] : ts[1],
            ts[2] = t0 > t1 ? t0 : t1, ts[2] = ts[2] < ts[0] ? ts[0] : ts[2] > ts[3] ? ts[3] : ts[2];
        for (t = ts; t < ts + 3; t++)
            if (t[0] != t[1]) {
                sy0 = (1.f - t[0]) * y0 + t[0] * y1, sy0 = sy0 < clip.ly ? clip.ly : sy0 > clip.uy ? clip.uy : sy0;
                sy1 = (1.f - t[1]) * y0 + t[1] * y1, sy1 = sy1 < clip.ly ? clip.ly : sy1 > clip.uy ? clip.uy : sy1;
                mx = x0 + (t[0] + t[1]) * 0.5f * dx;
                if (mx >= clip.lx && mx < clip.ux) {
                    sx0 = (1.f - t[0]) * x0 + t[0] * x1, sx0 = sx0 < clip.lx ? clip.lx : sx0 > clip.ux ? clip.ux : sx0;
                    sx1 = (1.f - t[1]) * x0 + t[1] * x1, sx1 = sx1 < clip.lx ? clip.lx : sx1 > clip.ux ? clip.ux : sx1;
                    (*function)(sx0, sy0, sx1, sy1, 0, info);
                } else if (polygon)
                    vx = mx < clip.lx ? clip.lx : clip.ux, (*function)(vx, sy0, vx, sy1, 0, info);
            }
    }
    static float *solveQuadratic(double A, double B, double C, float *roots) {
        if (fabs(A) < 1e-3) {
            float t = -C / B;  if (t > 0.f && t < 1.f)  *roots++ = t;
        } else {
            double d = B * B - 4.0 * A * C, r = sqrt(d);
            if (d >= 0.0) {
                float t0 = (-B + r) * 0.5 / A;  if (t0 > 0.f && t0 < 1.f)  *roots++ = t0;
                float t1 = (-B - r) * 0.5 / A;  if (t1 > 0.f && t1 < 1.f)  *roots++ = t1;
            }
        }
        return roots;
    }
    static void clipQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, Bounds clip, float lx, float ly, float ux, float uy, bool polygon, SegmentFunction function, QuadFunction quadFunction, void *info, float prec) {
        float ax, bx, ay, by, roots[10], *root = roots, *t, s, w0, w1, w2, mt, mx, my, vx, x0t, y0t, x2t, y2t;
        ax = x0 + x2 - x1 - x1, bx = 2.f * (x1 - x0), ay = y0 + y2 - y1 - y1, by = 2.f * (y1 - y0);
        *root++ = 0.f;
        if (clip.ly >= ly && clip.ly < uy)
            root = solveQuadratic(ay, by, y0 - clip.ly, root);
        if (clip.uy >= ly && clip.uy < uy)
            root = solveQuadratic(ay, by, y0 - clip.uy, root);
        if (clip.lx >= lx && clip.lx < ux)
            root = solveQuadratic(ax, bx, x0 - clip.lx, root);
        if (clip.ux >= lx && clip.ux < ux)
            root = solveQuadratic(ax, bx, x0 - clip.ux, root);
        std::sort(roots + 1, root), *root = 1.f;
        for (x0t = x0, y0t = y0, t = roots; t < root; t++, x0t = x2t, y0t = y2t) {
            s = 1.f - t[1], w0 = s * s, w1 = 2.f * s * t[1], w2 = t[1] * t[1];
            x2t = w0 * x0 + w1 * x1 + w2 * x2, x2t = x2t < clip.lx ? clip.lx : x2t > clip.ux ? clip.ux : x2t;
            y2t = w0 * y0 + w1 * y1 + w2 * y2, y2t = y2t < clip.ly ? clip.ly : y2t > clip.uy ? clip.uy : y2t;
            mt = 0.5f * (t[0] + t[1]), mx = (ax * mt + bx) * mt + x0, my = (ay * mt + by) * mt + y0;
            if (my >= clip.ly && my < clip.uy) {
                if (mx >= clip.lx && mx < clip.ux)
                    (*quadFunction)(x0t, y0t, 2.f * mx - 0.5f * (x0t + x2t), 2.f * my - 0.5f * (y0t + y2t), x2t, y2t, function, info, prec);
                else if (polygon)
                    vx = mx <= clip.lx ? clip.lx : clip.ux, (*function)(vx, y0t, vx, y2t, 0, info);
            }
        }
    }
    static void bisectQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, SegmentFunction function, void *info, float s) {
        float x = 0.25f * (x0 + x2) + 0.5f * x1, y = 0.25f * (y0 + y2) + 0.5f * y1;
        (*function)(x0, y0, x, y, 1, info), (*function)(x, y, x2, y2, 2, info);
    }
    static float *solveCubic(double B, double C, double D, double A, float *roots) {
        if (fabs(A) < 1e-3)
            return solveQuadratic(B, C, D, roots);
        else {
            const double wq0 = 2.0 / 27.0, third = 1.0 / 3.0;
            double  p, q, q2, u1, v1, b3, discriminant, sd, t;
            B /= A, C /= A, D /= A, p = C - B * B * third, q = B * (wq0 * B * B - third * C) + D;
            q2 = q * 0.5, b3 = B * third, discriminant = q2 * q2 + p * p * p / 27.0;
            if (discriminant < 0) {
                double mp3 = -p / 3, mp33 = mp3 * mp3 * mp3, r = sqrt(mp33), tcos = -q / (2 * r), crtr = 2 * copysign(cbrt(fabs(r)), r), sine, cosine;
                __sincos(acos(tcos < -1 ? -1 : tcos > 1 ? 1 : tcos) / 3, & sine, & cosine);
                t = crtr * cosine - b3; if (t > 0.f && t < 1.f)  *roots++ = t;
                t = crtr * (-0.5 * cosine - 0.866025403784439 * sine) - b3; if (t > 0.f && t < 1.f)  *roots++ = t;
                t = crtr * (-0.5 * cosine + 0.866025403784439 * sine) - b3; if (t > 0.f && t < 1.f)  *roots++ = t;
            } else if (discriminant == 0) {
                u1 = copysign(cbrt(fabs(q2)), q2);
                t = 2 * u1 - b3; if (t > 0.f && t < 1.f)  *roots++ = t;
                t = -u1 - b3; if (t > 0.f && t < 1.f)  *roots++ = t;
            } else {
                sd = sqrt(discriminant), u1 = copysign(cbrt(fabs(sd - q2)), sd - q2), v1 = copysign(cbrt(fabs(sd + q2)), sd + q2);
                t = u1 - v1 - b3; if (t > 0.f && t < 1.f)  *roots++ = t;
            }
        }
        return roots;
    }
    static void clipCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Bounds clip, float lx, float ly, float ux, float uy, bool polygon, SegmentFunction function, CubicFunction cubicFunction, void *info, float prec) {
        float cx, bx, ax, cy, by, ay, roots[14], *root = roots, *t, s, w0, w1, w2, w3, mt, mx, my, vx, x0t, y0t, x1t, y1t, x2t, y2t, x3t, y3t, fx, gx, fy, gy;
        cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1), ax = x3 - x0 - bx, bx -= cx;
        cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1), ay = y3 - y0 - by, by -= cy;
        *root++ = 0.f;
        if (clip.ly >= ly && clip.ly < uy)
            root = solveCubic(by, cy, y0 - clip.ly, ay, root);
        if (clip.uy >= ly && clip.uy < uy)
            root = solveCubic(by, cy, y0 - clip.uy, ay, root);
        if (clip.lx >= lx && clip.lx < ux)
            root = solveCubic(bx, cx, x0 - clip.lx, ax, root);
        if (clip.ux >= lx && clip.ux < ux)
            root = solveCubic(bx, cx, x0 - clip.ux, ax, root);
        std::sort(roots + 1, root), *root = 1.f;
        for (x0t = x0, y0t = y0, t = roots; t < root; t++, x0t = x3t, y0t = y3t) {
            s = 1.f - t[1], w0 = s * s * s, w1 = 3.f * s * s * t[1], w2 = 3.f * s * t[1] * t[1], w3 = t[1] * t[1] * t[1];
            x3t = w0 * x0 + w1 * x1 + w2 * x2 + w3 * x3, x3t = x3t < clip.lx ? clip.lx : x3t > clip.ux ? clip.ux : x3t;
            y3t = w0 * y0 + w1 * y1 + w2 * y2 + w3 * y3, y3t = y3t < clip.ly ? clip.ly : y3t > clip.uy ? clip.uy : y3t;
            mt = 0.5f * (t[0] + t[1]), mx = ((ax * mt + bx) * mt + cx) * mt + x0, my = ((ay * mt + by) * mt + cy) * mt + y0;
            if (my >= clip.ly && my < clip.uy) {
                if (mx >= clip.lx && mx < clip.ux) {
                    const float u = 1.f / 3.f, v = 2.f / 3.f, u3 = 1.f / 27.f, v3 = 8.f / 27.f;
                    mt = v * t[0] + u * t[1], x1t = ((ax * mt + bx) * mt + cx) * mt + x0, y1t = ((ay * mt + by) * mt + cy) * mt + y0;
                    mt = u * t[0] + v * t[1], x2t = ((ax * mt + bx) * mt + cx) * mt + x0, y2t = ((ay * mt + by) * mt + cy) * mt + y0;
                    fx = x1t - v3 * x0t - u3 * x3t, gx = x2t - u3 * x0t - v3 * x3t;
                    fy = y1t - v3 * y0t - u3 * y3t, gy = y2t - u3 * y0t - v3 * y3t;
                    (*cubicFunction)(
                        x0t, y0t,
                        3.f * fx - 1.5f * gx, 3.f * fy - 1.5f * gy,
                        3.f * gx - 1.5f * fx, 3.f * gy - 1.5f * fy,
                        x3t, y3t, function, info, prec
                    );
                } else if (polygon)
                    vx = mx <= clip.lx ? clip.lx : clip.ux, (*function)(vx, y0t, vx, y3t, 0, info);
            }
        }
    }
    static void divideCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, SegmentFunction function, void *info, float prec) {
        constexpr float multiplier = 10.3923048454; // 18/sqrt(3);
        float cx, bx, ax, cy, by, ay, adot, bdot, count, dt, dt2, f3x, f2x, f1x, f3y, f2y, f1y;
        cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1), ax = x3 - x0 - bx, bx -= cx;
        cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1), ay = y3 - y0 - by, by -= cy;
        adot = ax * ax + ay * ay, bdot = bx * bx + by * by;
        if (prec > 0.f && adot + bdot < 1.f)
            (*function)(x0, y0, x3, y3, 0, info);
        else {
            count = 2.f * ceilf(cbrtf(sqrtf(adot + 1e-12f) / (fabsf(prec) * multiplier))), dt = 1.f / count, dt2 = dt * dt;
            x1 = x0, bx *= dt2, ax *= dt2 * dt, f3x = 6.f * ax, f2x = f3x + 2.f * bx, f1x = ax + bx + cx * dt;
            y1 = y0, by *= dt2, ay *= dt2 * dt, f3y = 6.f * ay, f2y = f3y + 2.f * by, f1y = ay + by + cy * dt;
            while (--count) {
                x1 += f1x, f1x += f2x, f2x += f3x, y1 += f1y, f1y += f2y, f2y += f3y;
                (*function)(x0, y0, x1, y1, 2 - (int(count) & 1), info), x0 = x1, y0 = y1;
            }
            (*function)(x0, y0, x3, y3, 2, info);
        }
    }
    struct CurveIndexer {
        enum Flags { a = 1 << 15, c = 1 << 14, kMask = ~(a | c) };
        Segment *dst;  bool useCurves = false;  float px0, py0;  int is = 0;  Bounds clip;
        Row<Index> *indices;  Row<int16_t> *uxcovers;
        
        static void WriteSegment(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
            if (y0 != y1 || curve) {
                CurveIndexer *idxr = (CurveIndexer *)info;
                new (idxr->dst++) Segment(x0, y0, x1, y1, curve);
                if (curve == 0 || !idxr->useCurves)
                    idxr->indexLine(x0, y0, x1, y1), idxr->is++;
                else if (curve == 1)
                    idxr->px0 = x0, idxr->py0 = y0;
                else {
                    idxr->indexQuadratic(idxr->px0, idxr->py0, 0.25f * (idxr->px0 - x1) + x0, 0.25f * (idxr->py0 - y1) + y0, x0, y0), idxr->is++;
                    idxr->indexQuadratic(x0, y0, 0.25f * (x1 - idxr->px0) + x0, 0.25f * (y1 - idxr->py0) + y0, x1, y1), idxr->is++;
                }
            }
        }
        __attribute__((always_inline)) void indexLine(float x0, float y0, float x1, float y1) {
            if ((uint32_t(y0) & kFatMask) == (uint32_t(y1) & kFatMask))
                writeIndex(y0 * krfh, x0 < x1 ? x0 : x1, x0 > x1 ? x0 : x1, (y1 - y0) * kCoverScale, false);
            else {
                float lx, ux, ly, uy, m, c, y, ny, minx, maxx, scale;  int ir;
                lx = x0 < x1 ? x0 : x1, ux = x0 > x1 ? x0 : x1;
                ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1, scale = y0 < y1 ? kCoverScale : -kCoverScale;
                ir = ly * krfh, y = ir * kfh, m = (x1 - x0) / (y1 - y0), c = x0 - m * y0;
                minx = (y + (m < 0.f ? kfh : 0.f)) * m + c;
                maxx = (y + (m > 0.f ? kfh : 0.f)) * m + c;
                for (m *= kfh, y = ly; y < uy; y = ny, minx += m, maxx += m, ir++) {
                    ny = (ir + 1) * kfh, ny = uy < ny ? uy : ny;
                    writeIndex(ir, minx > lx ? minx : lx, maxx < ux ? maxx : ux, (ny - y) * scale, false);
                }
            }
        }
        __attribute__((always_inline)) void indexQuadratic(float x0, float y0, float x1, float y1, float x2, float y2) {
            float ay = y2 - y1, by = y1 - y0, ax, bx, iy;
            if ((ay > 0.f) == (by > 0.f) || fabsf(ay) < kMonotoneFlatness || fabsf(by) < kMonotoneFlatness) {
                if ((uint32_t(y0) & kFatMask) == (uint32_t(y2) & kFatMask))
                    writeIndex(y0 * krfh, x0 < x2 ? x0 : x2, x0 > x2 ? x0 : x2, (y2 - y0) * kCoverScale, true);
                else
                    ax = x2 - x1, bx = x1 - x0, indexCurve(y0, y2, ay - by, 2.f * by, y0, ax - bx, 2.f * bx, x0, true);
            } else {
                iy = y0 - by * by / (ay - by), iy = iy < clip.ly ? clip.ly : iy > clip.uy ? clip.uy : iy;
                ay -= by, by *= 2.f, ax = x2 - x1, bx = x1 - x0, ax -= bx, bx *= 2.f;
                indexCurve(y0, iy, ay, by, y0, ax, bx, x0, true);
                indexCurve(iy, y2, ay, by, y0, ax, bx, x0, false);
            }
        }
        __attribute__((always_inline)) void indexCurve(float w0, float w1, float ay, float by, float cy, float ax, float bx, float cx, bool a) {
            float y, uy, d2a, ity, d, t0, t1, itx, x0, x1, ny, sign = w1 < w0 ? -1.f : 1.f, lx, ux, ix;
            y = w0 < w1 ? w0 : w1, uy = w0 > w1 ? w0 : w1, d2a = 0.5f / ay, ity = -by * d2a, d2a *= sign, sign *= kCoverScale;
            itx = fabsf(ax) < kQuadraticFlatness ? FLT_MAX : -bx / ax * 0.5f;
            if (fabsf(ay) < kQuadraticFlatness)
                t0 = -(cy - y) / by;
            else
                d = by * by - 4.f * ay * (cy - y), t0 = ity + sqrtf(d < 0.f ? 0.f : d) * d2a;
            x0 = (ax * t0 + bx) * t0 + cx;
            for (int ir = y * krfh; y < uy; y = ny, ir++, x0 = x1, t0 = t1) {
                ny = (ir + 1) * kfh, ny = uy < ny ? uy : ny;
                if (fabsf(ay) < kQuadraticFlatness)
                    t1 = -(cy - ny) / by;
                else
                    d = by * by - 4.f * ay * (cy - ny), t1 = ity + sqrtf(d < 0.f ? 0.f : d) * d2a;
                x1 = (ax * t1 + bx) * t1 + cx;
                lx = x0 < x1 ? x0 : x1, ux = x0 > x1 ? x0 : x1;
                if ((t0 <= itx) == (itx <= t1))
                    ix = (ax * itx + bx) * itx + cx, lx = lx < ix ? lx : ix, ux = ux > ix ? ux : ix;
                writeIndex(ir, lx, ux, sign * (ny - y), a);
            }
        }
        __attribute__((always_inline)) void writeIndex(int ir, float lx, float ux, int16_t cover, bool a) {
            Row<Index>& row = indices[ir];  size_t i = row.end - row.idx;  new (row.alloc(1)) Index(lx, i);
            int16_t *dst = uxcovers[ir].alloc(kUXCoverSize);  dst[0] = int16_t(ceilf(ux)) | (a * Flags::a), dst[1] = cover, dst[2] = is & 0XFFFF, dst[3] = is >> 16;
        }
    };
    static void radixSort(uint32_t *in, int n, uint32_t lower, uint32_t range, bool single, uint16_t *counts) {
        range = range < 4 ? 4 : range;
        uint32_t tmp[n], mask = range - 1;
        memset(counts, 0, sizeof(uint16_t) * range);
        for (int i = 0; i < n; i++)
            counts[(in[i] - lower) & mask]++;
        uint64_t *sums = (uint64_t *)counts, sum = 0, count;
        for (int i = 0; i < range / 4; i++) {
            count = sums[i], sum += count + (count << 16) + (count << 32) + (count << 48), sums[i] = sum;
            sum = sum & 0xFFFF000000000000, sum = sum | (sum >> 16) | (sum >> 32) | (sum >> 48);
        }
        for (int i = n - 1; i >= 0; i--)
            tmp[--counts[(in[i] - lower) & mask]] = in[i];
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
    static void writeSegmentInstances(Bounds clip, bool even, size_t iz, bool opaque, bool fast, Context& ctx) {
        size_t ily = floorf(clip.ly * krfh), iuy = ceilf(clip.uy * krfh), iy, i, begin, size, edgeIz = iz | Instance::kEdge | even * Instance::kEvenOdd | fast * Instance::kFastEdges;
        uint16_t counts[256], ly, uy, lx, ux;  float h, cover, winding, wscale;
        Allocator::CountType type = fast ? Allocator::kFastEdges : Allocator::kQuadEdges;
        bool single = clip.ux - clip.lx < 256.f;  Index *index;
        uint32_t range = single ? powf(2.f, ceilf(log2f(clip.ux - clip.lx + 1.f))) : 256;
        Row<Index> *indices = & ctx.indices[0];  Row<int16_t> *uxcovers = & ctx.uxcovers[0];
        for (iy = ily; iy < iuy; iy++, indices->idx = indices->end, uxcovers->idx = uxcovers->end, indices++, uxcovers++) {
            if ((size = indices->end - indices->idx)) {
                if (size > 32 && size < 65536)
                    radixSort((uint32_t *)indices->base + indices->idx, int(size), single ? clip.lx : 0, range, single, counts);
                else
                    std::sort(indices->base + indices->idx, indices->base + indices->end);
                ly = iy * kfh, ly = ly < clip.ly ? clip.ly : ly > clip.uy ? clip.uy : ly;
                uy = (iy + 1) * kfh, uy = uy < clip.ly ? clip.ly : uy > clip.uy ? clip.uy : uy;
                for (h = uy - ly, wscale = 0.00003051850948f * kfh / h, cover = winding = 0.f, index = indices->base + indices->idx, lx = ux = index->x, i = begin = indices->idx; i < indices->end; i++, index++) {
                    if (index->x >= ux && fabsf((winding - floorf(winding)) - 0.5f) > 0.499f) {
                        if (lx != ux) {
                            Blend *inst = new (ctx.blends.alloc(1)) Blend(edgeIz);
                            ctx.allocator.alloc(ux - lx, h, ctx.blends.end - 1, & inst->quad.cell);
                            inst->quad.cell.lx = lx, inst->quad.cell.ly = ly, inst->quad.cell.ux = ux, inst->quad.cell.uy = uy, inst->quad.cover = short(cover), inst->quad.base = int(ctx.segments.idx), inst->data.count = int(i - begin), inst->data.iy = int(iy - ily), inst->data.begin = int(begin), inst->data.idx = int(indices->idx);
                            Allocator::Pass& pass = ctx.allocator.passes.back();  pass.size++, pass.counts[type] += (i - begin + 1) / 2;
                        }
                        winding = cover = truncf(winding + copysign(0.5f, winding));
                        if ((even && (int(winding) & 1)) || (!even && winding)) {
                            if (opaque) {
                                Cell *cell = & (new (ctx.opaques.alloc(1)) Instance(iz))->quad.cell;
                                cell->lx = ux, cell->ly = ly, cell->ux = index->x, cell->uy = uy;
                            } else {
                                Cell *cell = & (new (ctx.blends.alloc(1)) Blend(iz))->quad.cell;
                                cell->lx = ux, cell->ly = ly, cell->ux = index->x, cell->uy = uy, cell->ox = kNullIndex;
                                ctx.allocator.passes.back().size++;
                            }
                        }
                        begin = i, lx = ux = index->x;
                    }
                    int16_t *uxcover = uxcovers->base + uxcovers->idx + index->i * kUXCoverSize, iux = (uint16_t)uxcover[0] & CurveIndexer::Flags::kMask;
                    ux = iux > ux ? iux : ux, winding += uxcover[1] * wscale;
                }
                if (lx != ux) {
                    Blend *inst = new (ctx.blends.alloc(1)) Blend(edgeIz);
                    ctx.allocator.alloc(ux - lx, h, ctx.blends.end - 1, & inst->quad.cell);
                    inst->quad.cell.lx = lx, inst->quad.cell.ly = ly, inst->quad.cell.ux = ux, inst->quad.cell.uy = uy, inst->quad.cover = short(cover), inst->quad.base = int(ctx.segments.idx), inst->data.count = int(i - begin), inst->data.iy = int(iy - ily), inst->data.begin = int(begin), inst->data.idx = int(indices->idx);
                    Allocator::Pass& pass = ctx.allocator.passes.back();  pass.size++, pass.counts[type] += (i - begin + 1) / 2;
                }
            }
        }
    }
    struct Outliner {
        static void CountSegment(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
            size_t *count = (size_t *)info;  (*count)++;
        }
        static void WriteInstance(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
            Outliner *out = (Outliner *)info;
            if (x0 != FLT_MAX) {
                Outline& o = out->dst->outline;
                out->dst->iz = out->iz | out->flags[curve], o.s.x0 = x0, o.s.y0 = y0, o.s.x1 = x1, o.s.y1 = y1, o.prev = -1, o.next = 1, out->dst++;
            } else if (out->dst - out->dst0 > 0) {
                Instance *first = out->dst0, *last = out->dst - 1;  out->dst0 = out->dst;
                first->outline.prev = int(curve & 0x1) * int(last - first), last->outline.next = -first->outline.prev;
            }
        }
        uint32_t iz;  Instance *dst0, *dst;  uint32_t flags[3] = { 0, Instance::kNCurve, Instance::kPCurve };
    };
    static size_t writeContextsToBuffer(SceneList& list, Context *contexts, size_t count, size_t *begins, Buffer& buffer) {
        size_t size = buffer.headerSize, begin = buffer.headerSize, end = begin, sz, i, j, instances, p16outlines;
        for (i = 0; i < count; i++)
            size += contexts[i].opaques.end * sizeof(Instance);
        Context *ctx = contexts;   Allocator::Pass *pass;
        for (ctx = contexts, i = 0; i < count; i++, ctx++) {
            for (instances = p16outlines = 0, pass = ctx->allocator.passes.base, j = 0; j < ctx->allocator.passes.end; j++, pass++)
                instances += pass->count(), p16outlines += pass->counts[Allocator::kP16Outlines];
            begins[i] = size, size += instances * sizeof(Edge) + (ctx->outlineInstances - ctx->outlinePaths + ctx->blends.end) * (sizeof(Instance) + sizeof(Segment)) + ctx->segments.end * sizeof(Segment) + (ctx->p16total + kFastSegments * p16outlines) * sizeof(Geometry::Point16);
        }
        buffer.resize(size, buffer.headerSize);
        for (i = 0; i < count; i++)
            if ((sz = contexts[i].opaques.end * sizeof(Instance)))
                memcpy(buffer.base + end, contexts[i].opaques.base, sz), end += sz;
        if (begin != end)
            new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::kOpaques, begin, end);
        return size;
    }
    static void writeContextToBuffer(SceneList& list, Context *ctx, uint32_t *idxs, size_t begin, std::vector<Buffer::Entry>& entries, Buffer& buffer) {
        size_t i, j, size, iz, ip, is, lz, ic, end, pbase = 0;
        if (ctx->segments.end || ctx->p16total) {
            entries.emplace_back(Buffer::kSegmentsBase, begin, 0), end = begin + ctx->segments.end * sizeof(Segment);
            memcpy(buffer.base + begin, ctx->segments.base, end - begin), begin = end, entries.emplace_back(Buffer::kPointsBase, begin, 0);
            Row<Scene::Cache::Entry> *entries;
            for (pbase = 0, i = lz = 0; i < list.scenes.size(); lz += list.scenes[i].count, i++)
                for (entries = & list.scenes[i].cache->entries, ip = 0; ip < entries->end; ip++)
                    if (ctx->fasts.base[lz + ip]) {
                        end = begin + entries->base[ip].size * sizeof(Geometry::Point16);
                        memcpy(buffer.base + begin, entries->base[ip].p16s, end - begin);
                        begin = end, ctx->fasts.base[lz + ip] = uint32_t(pbase), pbase += entries->base[ip].size;
                    }
        }
        Transform *ctms = (Transform *)(buffer.base + buffer.ctms);  float *widths = (float *)(buffer.base + buffer.widths);
        Edge *quadEdge = nullptr, *fastEdge = nullptr, *fastOutline = nullptr, *fastOutline0 = nullptr, *quadOutline = nullptr, *quadOutline0 = nullptr, *fastMolecule = nullptr, *fastMolecule0 = nullptr, *quadMolecule = nullptr, *quadMolecule0 = nullptr, *outline = nullptr, *outline0 = nullptr;
        for (Allocator::Pass *pass = ctx->allocator.passes.base, *endpass = pass + ctx->allocator.passes.end; pass < endpass; pass++) {
            if (pass->count()) {
                entries.emplace_back(Buffer::kInstancesBase, begin + pass->count() * sizeof(Edge) + pass->counts[Allocator::kP16Outlines] * kFastSegments * sizeof(Geometry::Point16), 0);
                quadEdge = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kQuadEdges] * sizeof(Edge);
                entries.emplace_back(Buffer::kQuadEdges, begin, end), begin = end;
                fastEdge = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kFastEdges] * sizeof(Edge);
                entries.emplace_back(Buffer::kFastEdges, begin, end), begin = end;
                fastOutline0 = fastOutline = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kFastOutlines] * sizeof(Edge);
                entries.emplace_back(Buffer::kFastOutlines, begin, end), begin = end;
                quadOutline0 = quadOutline = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kQuadOutlines] * sizeof(Edge);
                entries.emplace_back(Buffer::kQuadOutlines, begin, end), begin = end;
                fastMolecule0 = fastMolecule = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kFastMolecules] * sizeof(Edge);
                entries.emplace_back(Buffer::kFastMolecules, begin, end), begin = end;
                quadMolecule0 = quadMolecule = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kQuadMolecules] * sizeof(Edge);
                entries.emplace_back(Buffer::kQuadMolecules, begin, end), begin = end;
                if (pass->counts[Allocator::kP16Outlines]) {
                    end = begin + pass->counts[Allocator::kP16Outlines] * kFastSegments * sizeof(Geometry::Point16);
                    entries.emplace_back(Buffer::kP16Miters, begin, end), begin = end;
                    outline0 = outline = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kP16Outlines] * sizeof(Edge);
                    entries.emplace_back(Buffer::kP16Outlines, begin, end), begin = end;
                }
            }
            Instance *dst0 = (Instance *)(buffer.base + begin), *dst = dst0;
            for (Blend *inst = ctx->blends.base + pass->idx, *endinst = inst + pass->size; inst < endinst; inst++) {
                iz = inst->iz & kPathIndexMask, is = idxs[iz] & 0xFFFFF, i = idxs[iz] >> 20;
                if (inst->iz & Instance::kOutlines) {
                    if (buffer.p16Outlines) {
                        ic = dst - dst0, dst->iz = inst->iz, dst->quad.base = int(ctx->fasts.base[inst->data.idx]), dst->quad.biid = int(outline - outline0), dst++;
                        Scene::Cache& cache = *list.scenes[i].cache.ref;  Scene::Cache::Entry& e = cache.entries.base[cache.ips.base[is]];
                        uint8_t *p16cnt = e.p16cnts;  short *p16off = e.p16offs;;
                        for (j = 0, size = e.size / kFastSegments; j < size; j++, outline++, p16cnt++, p16off += 2)
                            outline->ic = uint32_t(ic | (uint32_t(*p16cnt & 0xF) << 22)), outline->prev = p16off[0], outline->next = p16off[1];
                    } else {
                        Outliner out;  out.iz = inst->iz, out.dst = out.dst0 = dst;
                        divideGeometry(list.scenes[i].paths->base[is].ref, ctms[iz], inst->clip, inst->clip.lx == -FLT_MAX, false, true, & out, Outliner::WriteInstance);
                        dst = out.dst;
                    }
                } else {
                    ic = dst - dst0, dst->iz = inst->iz, dst->quad = inst->quad, dst++;
                    if (inst->iz & Instance::kMolecule) {
                        dst[-1].quad.base = int(ctx->fasts.base[inst->data.idx]);
                        Scene::Cache& cache = *list.scenes[i].cache.ref;
                        Scene::Cache::Entry *entry = & cache.entries.base[cache.ips.base[is]];
                        if (widths[iz]) {
                            Edge *outline = inst->iz & Instance::kFastEdges ? fastOutline : quadOutline;  uint8_t *p16end = entry->p16cnts;
                            dst[-1].quad.biid = int(outline - (inst->iz & Instance::kFastEdges ? fastOutline0 : quadOutline0));
                            for (j = 0, size = entry->size / kFastSegments; j < size; j++, outline++)
                                outline->ic = uint32_t(ic | (uint32_t(*p16end++ & 0xF) << 22));
                            *(inst->iz & Instance::kFastEdges ? & fastOutline : & quadOutline) = outline;
                        } else {
                            uint16_t ux = inst->quad.cell.ux;  Transform& ctm = ctms[iz];
                            float *molx = entry->mols + (ctm.a > 0.f ? 2 : 0), *moly = entry->mols + (ctm.c > 0.f ? 3 : 1);
                            bool update = entry->hasMolecules;  uint8_t *p16end = entry->p16cnts;
                            Edge *molecule = inst->iz & Instance::kFastEdges ? fastMolecule : quadMolecule;
                            dst[-1].quad.biid = int(molecule - (inst->iz & Instance::kFastEdges ? fastMolecule0 : quadMolecule0));
                            for (j = 0, size = entry->size / kFastSegments; j < size; j++, update = entry->hasMolecules && (*p16end & 0x80), p16end++) {
                                if (update)
                                    ux = ceilf(*molx * ctm.a + *moly * ctm.c + ctm.tx), molx += 4, moly += 4;
                                molecule->ic = uint32_t(ic | (uint32_t(*p16end & 0xF) << 22)), molecule->ux = ux, molecule++;
                            }
                            *(inst->iz & Instance::kFastEdges ? & fastMolecule : & quadMolecule) = molecule;
                        }
                    } else if (inst->iz & Instance::kEdge) {
                        Index *is = ctx->indices[inst->data.iy].base + inst->data.begin, *eis = is + inst->data.count;
                        int16_t *uxcovers = ctx->uxcovers[inst->data.iy].base + kUXCoverSize * inst->data.idx, *uxc;
                        Edge *edge = inst->iz & Instance::kFastEdges ? fastEdge : quadEdge;
                        for (; is < eis; is++, edge++) {
                            uxc = uxcovers + is->i * kUXCoverSize, edge->ic = uint32_t(ic) | ((uxc[3] << 26) & Edge::ue0) | Edge::a0 * bool(uxc[0] & CurveIndexer::Flags::a), edge->i0 = uint16_t(uxc[2]);
                            if (++is < eis)
                                uxc = uxcovers + is->i * kUXCoverSize, edge->ic |= ((uxc[3] << 22) & Edge::ue1) | Edge::a1 * bool(uxc[0] & CurveIndexer::Flags::a), edge->ux = uint16_t(uxc[2]);
                            else
                                edge->ux = kNullIndex, edge->ic |= Edge::ue1;
                        }
                        *(inst->iz & Instance::kFastEdges ? & fastEdge : & quadEdge) = edge;
                    }
                }
            }
            if (!buffer.p16Outlines && dst > dst0) {
                end = begin + (dst - dst0) * sizeof(Instance), entries.emplace_back(Buffer::kTransformBase, end, 0);
                entries.emplace_back(Buffer::kInstanceTransforms, begin, end), entries.emplace_back(Buffer::kInstances, begin, end);
                begin = end = end + (dst - dst0) * sizeof(Segment);
            }
        }
    }
};
typedef Rasterizer Ra;
