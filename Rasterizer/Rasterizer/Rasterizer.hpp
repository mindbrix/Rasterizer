//
//  Rasterizer.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 17/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "Rasterizer.h"
#import "crc64.h"
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
        inline Transform concat(Transform t, float ax, float ay) const {
            Transform inv = invert();  float ix = ax * inv.a + ay * inv.c + inv.tx, iy = ax * inv.b + ay * inv.d + inv.ty;
            return Transform(a, b, c, d, ax, ay).concat(t).translate(-ix, -iy);
        }
        inline Transform translate(float x, float y) const { return { a, b, c, d, x * a + y * c + tx, x * b + y * d + ty }; }
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
    public:
        Row<T>& operator+(const T *src) { if (src) { do *(alloc(1)) = *src; while (*src++);  --end; } return *this; }
        Row<T>& operator+(const int n) { char buf[32]; bzero(buf, sizeof(buf)), sprintf(buf, "%d", n); operator+((T *)buf); return *this; }
        Row<T>& empty() { end = idx = 0; return *this; }
        void reset() { end = idx = 0, base = nullptr, memory = Ref<Memory<T>>(); }
        inline T *alloc(size_t n) {
            end += n;
            if (memory->size < end)
                memory->resize(end * 1.5), base = memory->addr;
            return base + end - n;
        }
        inline T& back() { return base[end - 1]; }
        Ref<Memory<T>> memory;
        size_t end = 0, idx = 0;
        T *base = nullptr;
    };
    struct Geometry {
        static float normalizeRadians(float a) { return fmodf(a >= 0.f ? a : (kTau - (fmodf(-a, kTau))), kTau); }
        struct Point16 {
            Point16(uint16_t x, uint16_t y) : x(x), y(y) {}
            uint16_t x, y;
        };
        enum Type { kMove, kLine, kQuadratic, kCubic, kClose, kCountSize };
        
        void prepare(size_t moveCount, size_t lineCount, size_t quadCount, size_t cubicCount, size_t closeCount) {
            size_t size = moveCount + lineCount + 2 * quadCount + 3 * cubicCount + closeCount;
            types.alloc(size), types.empty(), points.alloc(size * 2), points.empty(), molecules.alloc(moveCount), molecules.empty();
            size_t p16sSize = 5 * moveCount + lineCount + 2 * quadCount + 3 * cubicCount;
            p16s.alloc(p16sSize), p16s.empty(), p16ends.alloc(p16sSize / kFastSegments), p16ends.empty();
        }
        void update(Type type, size_t size, float *p) {
            counts[type]++;
            uint8_t *tp = types.alloc(size);
            for (int i = 0; i < size; i++, p += 2)
                tp[i] = type, bounds.extend(p[0], p[1]), molecules.back().extend(p[0], p[1]);
        }
        size_t upperBound(float det) {
            float s = sqrtf(sqrtf(det < 1e-2f ? 1e-2f : det));
            size_t cubics = cubicSums == 0 ? 0 : (det < 1.f ? ceilf(s * (cubicSums + 2.f)) : ceilf(s) * cubicSums);
            return cubics + 2 * (molecules.end + counts[kLine] + counts[kQuadratic] + counts[kCubic]);
        }
        void addBounds(Bounds b) { moveTo(b.lx, b.ly), lineTo(b.ux, b.ly), lineTo(b.ux, b.uy), lineTo(b.lx, b.uy), lineTo(b.lx, b.ly); }
        void addEllipse(Bounds b) {
            const float t0 = 0.5f - 2.f / 3.f * (M_SQRT2 - 1.f), t1 = 1.f - t0, mx = 0.5f * (b.lx + b.ux), my = 0.5f * (b.ly + b.uy);
            moveTo(b.ux, my);
            cubicTo(b.ux, t0 * b.ly + t1 * b.uy, t0 * b.lx + t1 * b.ux, b.uy, mx, b.uy);
            cubicTo(t1 * b.lx + t0 * b.ux, b.uy, b.lx, t0 * b.ly + t1 * b.uy, b.lx, my);
            cubicTo(b.lx, t1 * b.ly + t0 * b.uy, t1 * b.lx + t0 * b.ux, b.ly, mx, b.ly);
            cubicTo(t0 * b.lx + t1 * b.ux, b.ly, b.ux, t1 * b.ly + t0 * b.uy, b.ux, my);
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
            *(molecules.alloc(1)) = Bounds();
            float *pts = points.alloc(2);
            x0 = pts[0] = x, y0 = pts[1] = y;
            update(kMove, 1, pts);
        }
        void lineTo(float x1, float y1) {
            if (x0 != x1 || y0 != y1) {
                float *pts = points.alloc(2);
                x0 = pts[0] = x1, y0 = pts[1] = y1;
                update(kLine, 1, pts);
            }
        }
        void quadTo(float x1, float y1, float x2, float y2) {
            float ax = x2 - x1, ay = y2 - y1, bx = x1 - x0, by = y1 - y0, det = bx * ay - by * ax, dot = ax * bx + ay * by;
            if (fabsf(det) < 1e-2f) {
                if (dot < 0.f && det)
                    lineTo((x0 + x2) * 0.25f + x1 * 0.5f, (y0 + y2) * 0.25f + y1 * 0.5f);
                lineTo(x2, y2);
            } else {
                float *pts = points.alloc(4);
                pts[0] = x1, pts[1] = y1, x0 = pts[2] = x2, y0 = pts[3] = y2;
                update(kQuadratic, 2, pts);
                ax -= bx, ay -= by, dot = ax * ax + ay * ay, maxDot = maxDot > dot ? maxDot : dot;
            }
        }
        void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
            float bx, ax, by, ay, cx, cy, dot;
            bx = 3.f * (x2 - x1), ax = -bx - x0 + x3, by = 3.f * (y2 - y1), ay = -by - y0 + y3;
            if (ax * ax + ay * ay < 1e-2f)
                quadTo((3.f * (x1 + x2) - x0 - x3) * 0.25f, (3.f * (y1 + y2) - y0 - y3) * 0.25f, x3, y3);
            else {
                float *pts = points.alloc(6);
                cx = 3.f * (x1 - x0), bx -= cx, ax = x3 - x0 - cx - bx;
                cy = 3.f * (y1 - y0), by -= cy, ay = y3 - y0 - cy - by;
                dot = ax * ax + ay * ay + bx * bx + by * by, cubicSums += ceilf(sqrtf(sqrtf(dot))), maxDot = maxDot > dot ? maxDot : dot;
                pts[0] = x1, pts[1] = y1, pts[2] = x2, pts[3] = y2, x0 = pts[4] = x3, y0 = pts[5] = y3;
                update(kCubic, 3, pts);
            }
        }
        void close() {
            float *pts = points.alloc(2);
            pts[0] = x0, pts[1] = y0;
            update(kClose, 1, pts);
        }
        size_t hash() {
            crc = crc ?: crc64(crc64(crc, types.base, types.end * sizeof(uint8_t)), points.base, points.end * sizeof(float));
            return crc;
        }
        inline void writePoint16(float x, float y, Bounds& b, uint32_t curve) {
            new (p16s.alloc(1)) Point16(
                uint16_t((x - b.lx) / (b.ux - b.lx) * 32767.f) | ((curve & 2) << 14),
                uint16_t((y - b.ly) / (b.uy - b.ly) * 32767.f) | ((curve & 1) << 15));
        }
        static void WriteSegment16(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
            Geometry *g = (Geometry *)info;
            if (x0 != FLT_MAX)
                g->writePoint16(x0, y0, g->bounds, curve);
            else if (g->p16s.end > g->p16s.idx) {
                g->writePoint16(x1, y1, g->bounds, 0);
                size_t count = kFastSegments - (g->p16s.end % kFastSegments);
                for (Point16 *ep16 = g->p16s.alloc(count); count; count--)
                    new (ep16++) Point16(0xFFFF, 0xFFFF);
                Point16 *p16 = & g->p16s.base[g->p16s.idx + kFastSegments - 1];
                uint8_t *p16end = g->p16ends.alloc((g->p16s.end - g->p16s.idx) / kFastSegments);
                for (; g->p16s.idx < g->p16s.end; g->p16s.idx += kFastSegments, p16 += kFastSegments, p16end++)
                    *p16end = p16->x == 0xFFFF && p16->y == 0xFFFF;
            }
        }
        size_t refCount = 0, crc = 0, minUpper = 0, cubicSums = 0, counts[kCountSize] = { 0, 0, 0, 0, 0 };
        Row<uint8_t> types;  Row<float> points;
        Row<Point16> p16s;  Row<uint8_t> p16ends;  Row<Bounds> molecules;
        float x0 = 0.f, y0 = 0.f, maxDot = 0.f;
        Bounds bounds;
    };
    typedef Ref<Geometry> Path;
    
    typedef void (*SegmentFunction)(float x0, float y0, float x1, float y1, uint32_t curve, void *info);
    typedef void (*QuadFunction)(float x0, float y0, float x1, float y1, float x2, float y2, SegmentFunction function, void *info, float s);
    typedef void (*CubicFunction)(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, SegmentFunction function, void *info, float s);
    
    struct Segment {
        Segment(float x0, float y0, float x1, float y1) : x0(x0), y0(y0), x1(x1), y1(y1) {}
        Segment(float _x0, float _y0, float _x1, float _y1, uint32_t curve) {
            *((uint32_t *)& x0) = (*((uint32_t *)& _x0) & ~3) | curve, y0 = _y0, x1 = _x1, y1 = _y1;
        }
        float x0, y0, x1, y1;
    };
    struct Scene {
        struct Cache {
            struct Entry {
                Entry(size_t size, bool hasMolecules, float maxDot, float *mols, uint16_t *p16s, uint8_t *p16end) : size(size), hasMolecules(hasMolecules), maxDot(maxDot), mols(mols), p16s(p16s), p16end(p16end) {}
                size_t size;  bool hasMolecules;  float maxDot;  float *mols;  uint16_t *p16s;  uint8_t *p16end;
            };
            size_t refCount = 0;
            Row<uint32_t> ips;  Row<Entry> entries;
            std::unordered_map<size_t, size_t> map;
        };
        template<typename T>
        struct Vector {
            uint64_t refCount;
            std::vector<T> src, dst;
            void add(T obj) {  src.emplace_back(obj), dst.emplace_back(obj); }
        };
        enum Flags { kInvisible = 1 << 0, kFillEvenOdd = 1 << 1, kOutlineRounded = 1 << 2, kOutlineEndCap = 1 << 3 };
        void addPath(Path path, Transform ctm, Colorant color, float width, uint8_t flag) {
            if (path->types.end > 1 && *path->types.base == Geometry::kMove && (path->bounds.lx != path->bounds.ux || path->bounds.ly != path->bounds.uy)) {
                count++, weight += path->types.end;
                auto it = cache->map.find(path->hash());
                if (it != cache->map.end())
                    *(cache->ips.alloc(1)) = uint32_t(it->second);
                else {
                    if (path->p16s.end == 0) {
                        float w = path->bounds.ux - path->bounds.lx, h = path->bounds.uy - path->bounds.ly, dim = w > h ? w : h;
                        divideGeometry(path.ref, Transform(), Bounds(), true, true, true, path.ref, Geometry::WriteSegment16, bisectQuadratic, 0.f, divideCubic, kCubicScale * powf(dim > kMoleculesHeight ? 1.f : kMoleculesHeight / dim, 2.f));
                    }
                    new (cache->entries.alloc(1)) Cache::Entry(path->p16s.end, path->molecules.end > 1, path->maxDot, (float *)path->molecules.base, (uint16_t *)path->p16s.base, path->p16ends.base);
                    *(cache->ips.alloc(1)) = uint32_t(cache->map.size());
                    cache->map.emplace(path->hash(), cache->map.size());
                }
                path->minUpper = path->minUpper ?: path->upperBound(kMinUpperDet);
                _paths->dst.emplace_back(path), _bounds->add(path->bounds), _ctms->add(ctm), _colors->add(color), _widths->add(width), _flags->add(flag);
                paths = & _paths->dst[0], b = & _bounds->dst[0], ctms = & _ctms->dst[0], colors = & _colors->dst[0], widths = & _widths->dst[0], flags = & _flags->dst[0];
            }
        }
        Bounds bounds() {
            Bounds bnds;
            for (int i = 0; i < count; i++)
                if ((flags[i] & kInvisible) == 0)
                    bnds.extend(Bounds(b[i].inset(-0.5f * widths[i], -0.5f * widths[i]).unit(ctms[i])));
            return bnds;
        }
        size_t count = 0, weight = 0;
        Ref<Cache> cache;
        Path *paths;  Transform *ctms;  Colorant *colors;  float *widths;  uint8_t *flags;  Bounds *b;
        Ref<Vector<Path>> _paths;  Ref<Vector<Bounds>> _bounds;  Ref<Vector<Transform>> _ctms;  Ref<Vector<Colorant>> _colors;  Ref<Vector<float>> _widths;  Ref<Vector<uint8_t>> _flags;
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
        size_t index(size_t si, size_t pi) {
            for (int i = 0; i < si; i++)
                pi += scenes[i].count;
            return pi;
        }
        size_t pathsCount = 0;  std::vector<Scene> scenes;  std::vector<Transform> ctms, clips;
    };
    struct Range {
        Range(size_t begin, size_t end) : begin(int(begin)), end(int(end)) {}
        int begin, end;
    };
    static void CountSegment(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
        size_t *count = (size_t *)info;  (*count)++;
    }
    struct Index {
        Index(uint16_t x, uint16_t i) : x(x), i(i) {}
        uint16_t x, i;
        inline bool operator< (const Index& other) const { return x < other.x; }
    };
    struct Cell {
        Cell(float lx, float ly, float ux, float uy, float ox, float oy) : lx(lx), ly(ly), ux(ux), uy(uy), ox(ox), oy(oy) {}
        uint16_t lx, ly, ux, uy, ox, oy;
    };
    struct Quad {
        Cell cell;
        short cover;
        uint16_t count;
        int iy, begin, base, idx;
    };
    struct Outline {
        union { Segment s;  Bounds clip; };
        short prev, next;
    };
    struct Instance {
        enum Type { kEvenOdd = 1 << 24, kRounded = 1 << 25, kEdge = 1 << 26, kSolidCell = 1 << 27, kEndCap = 1 << 28, kOutlines = 1 << 29, kFastEdges = 1 << 30, kMolecule = 1 << 31 };
        Instance(size_t iz, int type) : iz((uint32_t)iz | type) {}
        union { Quad quad;  Outline outline; };
        uint32_t iz;
    };
    struct Edge {
        uint32_t ic;  enum Flags { a0 = 1 << 31, a1 = 1 << 30, kMask = ~(a0 | a1) };
        uint16_t i0, ux;
    };
    struct Buffer {
        static constexpr size_t kPageSize = 4096;
        enum Type { kQuadEdges, kFastEdges, kFastMolecules, kQuadMolecules, kOpaques, kInstances };
        struct Entry {
            Entry(Type type, size_t begin, size_t end, size_t segments = 0, size_t points = 0, size_t instbase = 0) : type(type), begin(begin), end(end), segments(segments), points(points), instbase(instbase) {}
            Type type;  size_t begin, end, segments, points, instbase;
        };
        ~Buffer() { if (base) free(base); }
        void prepare(size_t pathsCount) {
            size_t szcolors = pathsCount * sizeof(Colorant), szctms = pathsCount * sizeof(Transform), szwidths = pathsCount * sizeof(float), szbounds = pathsCount * sizeof(Bounds);
            headerSize = szcolors + 2 * szctms + szwidths + szbounds;
            this->pathsCount = pathsCount, colors = 0, ctms = colors + szcolors, clips = ctms + szctms, widths = clips + szctms, bounds = widths + szwidths;
            resize(headerSize);
        }
        void resize(size_t n, size_t copySize = 0) {
            size_t allocation = (n + kPageSize - 1) / kPageSize * kPageSize;
            if (size < allocation) {
                size = allocation;
                void *copy = nullptr;
                if (base) {
                    if (copySize)
                        copy = malloc(copySize), memcpy(copy, base, copySize);
                    free(base);
                }
                posix_memalign((void **)& base, kPageSize, allocation);
                if (copy)
                    memcpy(base, copy, copySize), free(copy);
            }
        }
        uint8_t *base = nullptr;
        Row<Entry> entries;
        bool useCurves = false;
        Colorant clearColor = Colorant(255, 255, 255, 255);
        size_t colors, ctms, clips, widths, bounds, sceneCount, tick, pathsCount, headerSize, size = 0;
    };
    struct Allocator {
        struct Pass {
            Pass(size_t idx) : li(idx), ui(idx) {}
            size_t fastEdges = 0, quadEdges = 0, fastMolecules = 0, quadMolecules = 0, li, ui;
        };
        void empty(Bounds device) {
            full = device, sheet = strip = fast = molecules = Bounds(0.f, 0.f, 0.f, 0.f), passes.empty(), new (passes.alloc(1)) Pass(0);
        }
        void allocAndCount(float lx, float ly, float ux, float uy, size_t idx, size_t fastEdges, size_t quadEdges, size_t fastMolecules, size_t quadMolecules, Cell *cell) {
            float w = ux - lx, h = uy - ly, hght;  Bounds *b;
            if (h <= kfh)
                b = & strip, hght = kfh;
            else if (h <- kFastHeight)
                b = & fast, hght = kFastHeight;
            else
                b = & molecules, hght = kMoleculesHeight;
            if (b->ux - b->lx < w) {
                if (sheet.uy - sheet.ly < hght)
                    sheet = full, strip = fast = molecules = Bounds(0.f, 0.f, 0.f, 0.f), new (passes.alloc(1)) Pass(idx);
                b->lx = sheet.lx, b->ly = sheet.ly, b->ux = sheet.ux, b->uy = sheet.ly + hght, sheet.ly = b->uy;
            }
            new (cell) Cell(lx, ly, ux, uy, b->lx, b->ly), b->lx += w;
            Pass& pass = passes.back();
            pass.ui++, pass.fastEdges += fastEdges, pass.quadEdges += quadEdges, pass.fastMolecules += fastMolecules, pass.quadMolecules += quadMolecules;
        }
        Row<Pass> passes;
        Bounds full, sheet, strip, fast, molecules;
    };
    struct Context {
        void prepare(size_t w, size_t h, size_t pathsCount, size_t slz, size_t suz) {
            device = Bounds(0.f, 0.f, w, h), this->slz = slz, this->suz = suz;
            size_t fatlines = 1.f + ceilf(float(h) * krfh);
            if (indices.size() != fatlines)
                indices.resize(fatlines), uxcovers.resize(fatlines);
            empty(), allocator.empty(device);
            bzero(fasts.alloc(pathsCount), pathsCount * sizeof(*fasts.base));
        }
        void drawList(SceneList& list, Transform view, uint32_t *idxs, Transform *ctms, Colorant *colors, Transform *clipctms, float *widths, Bounds *bounds, Buffer *buffer) {
            size_t lz, uz, i, clz, cuz, iz, is, ip, size;  Scene *scene = & list.scenes[0];
            for (lz = i = 0, uz = scene->count; i < list.scenes.size(); i++, scene++, lz = uz, uz = lz + scene->count) {
                Transform ctm = view.concat(list.ctms[i]), clipctm = view.concat(list.clips[i]), inv = clipctm.invert();
                Bounds clipbounds = Bounds(clipctm).integral().intersect(device);
                float err = fminf(1e-2f, 1e-2f / sqrtf(fabsf(clipctm.det()))), e0 = -err, e1 = 1.f + err;
                clz = lz < slz ? slz : lz > suz ? suz : lz, cuz = uz < slz ? slz : uz > suz ? suz : uz;
                for (is = clz - lz, iz = clz; iz < cuz; iz++, is++) {
                    if (scene->flags[is] & Scene::Flags::kInvisible)
                        continue;
                    Transform m = ctm.concat(scene->ctms[is]);
                    float det = fabsf(m.det()), ws = sqrtf(det), w = scene->widths[is], width = w < 0.f ? -w : w * ws, uw = w < 0.f ? -w / ws : w;
                    Transform unit = scene->b[is].inset(-uw, -uw).unit(m);
                    Bounds dev = Bounds(unit), clip = dev.integral().intersect(clipbounds);
                    if (clip.lx != clip.ux && clip.ly != clip.uy) {
                        ctms[iz] = m, widths[iz] = width, clipctms[iz] = clipctm, idxs[iz] = uint32_t((i << 20) | is);
                        Geometry *g = scene->paths[is].ref;
                        if (width) {
                           Instance *inst = new (blends.alloc(1)) Instance(iz, Instance::kOutlines
                               | (scene->flags[is] & Scene::kOutlineRounded ? Instance::kRounded : 0)
                               | (scene->flags[is] & Scene::kOutlineEndCap ? Instance::kEndCap : 0));
                           inst->outline.clip = clip.contains(dev) ? Bounds(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX) : clip.inset(-width, -width);
                           if (det > 1e2f) {
                               size_t count = 0;
                               divideGeometry(g, m, inst->outline.clip, false, false, true, & count, CountSegment);
                               outlineInstances += count;
                           } else
                               outlineInstances += det < kMinUpperDet ? g->minUpper : g->upperBound(det);
                           outlinePaths++, allocator.passes.back().ui++;
                       } else if (clip.uy - clip.ly <= kMoleculesHeight && clip.ux - clip.lx <= kMoleculesHeight) {
                            ip = scene->cache->ips.base[is], size = scene->cache->entries.base[ip].size;
                            if (fasts.base[lz + ip] == 0)
                                p16total += size;
                            fasts.base[lz + ip] = 1, bounds[iz] = scene->b[is];
                            bool fast = det * scene->cache->entries.base[ip].maxDot < 16.f;
                            Instance *inst = new (blends.alloc(1)) Instance(iz, Instance::kMolecule | (scene->flags[is] & Scene::kFillEvenOdd ? Instance::kEvenOdd : 0) | (fast ? Instance::kFastEdges : 0));
                            allocator.allocAndCount(clip.lx, clip.ly, clip.ux, clip.uy, blends.end - 1, 0, 0, fast ? size / kFastSegments : 0, !fast ? size / kFastSegments : 0, & inst->quad.cell), inst->quad.cover = 0, inst->quad.iy = int(lz);
                        } else {
                            CurveIndexer idxr;  idxr.clip = clip, idxr.indices = & indices[0] - int(clip.ly * krfh), idxr.uxcovers = & uxcovers[0] - int(clip.ly * krfh), idxr.useCurves = buffer->useCurves, idxr.dst = segments.alloc(det < kMinUpperDet ? g->minUpper : g->upperBound(det));
                            divideGeometry(g, m, clip, clip.contains(dev), true, false, & idxr, CurveIndexer::WriteSegment);
                            Bounds clu = Bounds(inv.concat(unit));
                            bool opaque = colors[iz].a == 255 && !(clu.lx < e0 || clu.ux > e1 || clu.ly < e0 || clu.uy > e1);
                            bool fast = !buffer->useCurves || (g->counts[Geometry::kQuadratic] == 0 && g->counts[Geometry::kCubic] == 0);
                            writeSegmentInstances(clip, scene->flags[is] & Scene::kFillEvenOdd, iz, opaque, fast, *this);
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
        Row<uint32_t> fasts;  Row<Instance> blends, opaques;  Row<Segment> segments;
        std::vector<Row<Index>> indices;  std::vector<Row<int16_t>> uxcovers;
    };
    static void divideGeometry(Geometry *g, Transform m, Bounds clip, bool unclipped, bool polygon, bool mark, void *info, SegmentFunction function, QuadFunction quadFunction = bisectQuadratic, float quadScale = 0.f, CubicFunction cubicFunction = divideCubic, float cubicScale = kCubicScale) {
        float *p = g->points.base, sx = FLT_MAX, sy = FLT_MAX, x0 = FLT_MAX, y0 = FLT_MAX, x1, y1, x2, y2, x3, y3, ly, uy, lx, ux;
        for (uint8_t *type = g->types.base, *end = type + g->types.end; type < end; )
            switch (*type) {
                case Geometry::kMove:
                    if (polygon && sx != FLT_MAX && (sx != x0 || sy != y0)) {
                        if (unclipped)
                            (*function)(x0, y0, sx, sy, 0, info);
                        else {
                            ly = y0 < sy ? y0 : sy, uy = y0 > sy ? y0 : sy;
                            if (!unclipped && ly < clip.uy && uy > clip.ly) {
                                if (ly < clip.ly || uy > clip.uy || (x0 < sx ? x0 : sx) < clip.lx || (x0 > sx ? x0 : sx) > clip.ux)
                                    clipLine(x0, y0, sx, sy, clip, polygon, function, info);
                                else
                                    (*function)(x0, y0, sx, sy, 0, info);
                            }
                        }
                    }
                    if (mark && sx != FLT_MAX)
                        (*function)(FLT_MAX, FLT_MAX, sx, sy, 0, info);
                    sx = x0 = p[0] * m.a + p[1] * m.c + m.tx, sy = y0 = p[0] * m.b + p[1] * m.d + m.ty, p += 2, type++;
                    break;
                case Geometry::kLine:
                    x1 = p[0] * m.a + p[1] * m.c + m.tx, y1 = p[0] * m.b + p[1] * m.d + m.ty;
                    if (unclipped)
                        (*function)(x0, y0, x1, y1, 0, info);
                    else {
                        ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
                        if (ly < clip.uy && uy > clip.ly) {
                            if (ly < clip.ly || uy > clip.uy || (x0 < x1 ? x0 : x1) < clip.lx || (x0 > x1 ? x0 : x1) > clip.ux)
                                clipLine(x0, y0, x1, y1, clip, polygon, function, info);
                            else
                                (*function)(x0, y0, x1, y1, 0, info);
                        }
                    }
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
                    p += 2, type++;
                    break;
            }
        if (polygon && sx != FLT_MAX && (sx != x0 || sy != y0)) {
            if (unclipped)
                (*function)(x0, y0, sx, sy, 0, info);
            else {
                ly = y0 < sy ? y0 : sy, uy = y0 > sy ? y0 : sy;
                if (ly < clip.uy && uy > clip.ly) {
                    if (ly < clip.ly || uy > clip.uy || (x0 < sx ? x0 : sx) < clip.lx || (x0 > sx ? x0 : sx) > clip.ux)
                        clipLine(x0, y0, sx, sy, clip, polygon, function, info);
                    else
                        (*function)(x0, y0, sx, sy, 0, info);
                }
            }
        }
        if (mark && sx != FLT_MAX)
            (*function)(FLT_MAX, FLT_MAX, sx, sy, 0, info);
    }
    static void clipLine(float x0, float y0, float x1, float y1, Bounds clip, bool polygon, SegmentFunction function, void *info) {
        float dx = x1 - x0, dy = y1 - y0, t0 = (clip.lx - x0) / dx, t1 = (clip.ux - x0) / dx, sy0, sy1, sx0, sx1, mx, vx, ts[4], *t;
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
                sy0 = y0 + t[0] * dy, sy0 = sy0 < clip.ly ? clip.ly : sy0 > clip.uy ? clip.uy : sy0;
                sy1 = y0 + t[1] * dy, sy1 = sy1 < clip.ly ? clip.ly : sy1 > clip.uy ? clip.uy : sy1;
                mx = x0 + (t[0] + t[1]) * 0.5f * dx;
                if (mx >= clip.lx && mx < clip.ux) {
                    sx0 = x0 + t[0] * dx, sx0 = sx0 < clip.lx ? clip.lx : sx0 > clip.ux ? clip.ux : sx0;
                    sx1 = x0 + t[1] * dx, sx1 = sx1 < clip.lx ? clip.lx : sx1 > clip.ux ? clip.ux : sx1;
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
    static void clipQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, Bounds clip, float lx, float ly, float ux, float uy, bool polygon, SegmentFunction function, QuadFunction quadFunction, void *info, float s) {
        float ax, bx, ay, by, roots[10], *root = roots, *t, mt, mx, my, vx, tx0, ty0, tx2, ty2;
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
        std::sort(roots + 1, root), *root++ = 1.f;
        for (tx0 = x0, ty0 = y0, t = roots; t < root - 1; t++, tx0 = tx2, ty0 = ty2) {
            tx2 = (ax * t[1] + bx) * t[1] + x0, ty2 = (ay * t[1] + by) * t[1] + y0;
            mt = (t[0] + t[1]) * 0.5f, mx = (ax * mt + bx) * mt + x0, my = (ay * mt + by) * mt + y0;
            if (my >= clip.ly && my < clip.uy) {
                if (mx >= clip.lx && mx < clip.ux) {
                    (*quadFunction)(
                        tx0 < clip.lx ? clip.lx : tx0 > clip.ux ? clip.ux : tx0,
                        ty0 < clip.ly ? clip.ly : ty0 > clip.uy ? clip.uy : ty0,
                        2.f * mx - 0.5f * (tx0 + tx2), 2.f * my - 0.5f * (ty0 + ty2),
                        tx2 < clip.lx ? clip.lx : tx2 > clip.ux ? clip.ux : tx2,
                        ty2 < clip.ly ? clip.ly : ty2 > clip.uy ? clip.uy : ty2,
                        function, info, s);
                } else if (polygon) {
                    vx = mx <= clip.lx ? clip.lx : clip.ux;
                    (*function)(vx, ty0 < clip.ly ? clip.ly : ty0 > clip.uy ? clip.uy : ty0, vx, ty2 < clip.ly ? clip.ly : ty2 > clip.uy ? clip.uy : ty2, 0, info);
                }
            }
        }
    }
    static void bisectQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, SegmentFunction function, void *info, float s) {
        float x = 0.25f * (x0 + x2) + 0.5f * x1, y = 0.25f * (y0 + y2) + 0.5f * y1;
        (*function)(x0, y0, x, y, 1, info), (*function)(x, y, x2, y2, 2, info);
    }
    static void divideQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, SegmentFunction function, void *info, float s) {
        float ax, ay, a, count, dt, f2x, f1x, f2y, f1y;
        ax = x0 + x2 - x1 - x1, ay = y0 + y2 - y1 - y1, a = s * (ax * ax + ay * ay);
        count = a < s ? 1.f : a < 8.f ? 2.f : 2.f + floorf(sqrtf(sqrtf(a))), dt = 1.f / count;
        ax *= dt * dt, f2x = 2.f * ax, f1x = ax + 2.f * (x1 - x0) * dt, x1 = x0;
        ay *= dt * dt, f2y = 2.f * ay, f1y = ay + 2.f * (y1 - y0) * dt, y1 = y0;
        while (--count) {
            x1 += f1x, f1x += f2x, y1 += f1y, f1y += f2y;
            (*function)(x0, y0, x1, y1, 1, info);
            x0 = x1, y0 = y1;
        }
        (*function)(x0, y0, x2, y2, dt == 1.f ? 0 : 2, info);
    }
    static float *solveCubic(double A, double B, double C, double D, float *roots) {
        if (fabs(D) < 1e-3)
            return solveQuadratic(A, B, C, roots);
        else {
            const double wq0 = 2.0 / 27.0, third = 1.0 / 3.0;
            double  p, q, q2, u1, v1, a3, discriminant, sd, t;
            A /= D, B /= D, C /= D, p = B - A * A * third, q = A * (wq0 * A * A - third * B) + C;
            q2 = q * 0.5, a3 = A * third, discriminant = q2 * q2 + p * p * p / 27.0;
            if (discriminant < 0) {
                double mp3 = -p / 3, mp33 = mp3 * mp3 * mp3, r = sqrt(mp33), tcos = -q / (2 * r), crtr = 2 * copysign(cbrt(fabs(r)), r), sine, cosine;
                __sincos(acos(tcos < -1 ? -1 : tcos > 1 ? 1 : tcos) / 3, & sine, & cosine);
                t = crtr * cosine - a3; if (t > 0.f && t < 1.f)  *roots++ = t;
                t = crtr * (-0.5 * cosine - 0.866025403784439 * sine) - a3; if (t > 0.f && t < 1.f)  *roots++ = t;
                t = crtr * (-0.5 * cosine + 0.866025403784439 * sine) - a3; if (t > 0.f && t < 1.f)  *roots++ = t;
            } else if (discriminant == 0) {
                u1 = copysign(cbrt(fabs(q2)), q2);
                t = 2 * u1 - a3; if (t > 0.f && t < 1.f)  *roots++ = t;
                t = -u1 - a3; if (t > 0.f && t < 1.f)  *roots++ = t;
            } else {
                sd = sqrt(discriminant), u1 = copysign(cbrt(fabs(sd - q2)), sd - q2), v1 = copysign(cbrt(fabs(sd + q2)), sd + q2);
                t = u1 - v1 - a3; if (t > 0.f && t < 1.f)  *roots++ = t;
            }
        }
        return roots;
    }
    static void clipCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Bounds clip, float lx, float ly, float ux, float uy, bool polygon, SegmentFunction function, CubicFunction cubicFunction, void *info, float s) {
        float cy, by, ay, cx, bx, ax, roots[14], *root = roots, *t, mt, mx, my, vx, tx0, ty0, tx1, ty1, tx2, ty2, tx3, ty3, fx, gx, fy, gy;
        cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
        cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
        *root++ = 0.f;
        if (clip.ly >= ly && clip.ly < uy)
            root = solveCubic(by, cy, y0 - clip.ly, ay, root);
        if (clip.uy >= ly && clip.uy < uy)
            root = solveCubic(by, cy, y0 - clip.uy, ay, root);
        if (clip.lx >= lx && clip.lx < ux)
            root = solveCubic(bx, cx, x0 - clip.lx, ax, root);
        if (clip.ux >= lx && clip.ux < ux)
            root = solveCubic(bx, cx, x0 - clip.ux, ax, root);
        std::sort(roots + 1, root), *root++ = 1.f;
        for (tx0 = x0, ty0 = y0, t = roots; t < root - 1; t++, tx0 = tx3, ty0 = ty3) {
            tx3 = ((ax * t[1] + bx) * t[1] + cx) * t[1] + x0, ty3 = ((ay * t[1] + by) * t[1] + cy) * t[1] + y0;
            mt = (t[0] + t[1]) * 0.5f, mx = ((ax * mt + bx) * mt + cx) * mt + x0, my = ((ay * mt + by) * mt + cy) * mt + y0;
            if (my >= clip.ly && my < clip.uy) {
                if (mx >= clip.lx && mx < clip.ux) {
                    const float u = 1.f / 3.f, v = 2.f / 3.f, u3 = 1.f / 27.f, v3 = 8.f / 27.f, m0 = 3.f, m1 = 1.5f;
                    mt = v * t[0] + u * t[1], tx1 = ((ax * mt + bx) * mt + cx) * mt + x0, ty1 = ((ay * mt + by) * mt + cy) * mt + y0;
                    mt = u * t[0] + v * t[1], tx2 = ((ax * mt + bx) * mt + cx) * mt + x0, ty2 = ((ay * mt + by) * mt + cy) * mt + y0;
                    fx = tx1 - v3 * tx0 - u3 * tx3, fy = ty1 - v3 * ty0 - u3 * ty3;
                    gx = tx2 - u3 * tx0 - v3 * tx3, gy = ty2 - u3 * ty0 - v3 * ty3;
                    (*cubicFunction)(
                        tx0 < clip.lx ? clip.lx : tx0 > clip.ux ? clip.ux : tx0,
                        ty0 < clip.ly ? clip.ly : ty0 > clip.uy ? clip.uy : ty0,
                        fx * m0 + gx * -m1, fy * m0 + gy * -m1,
                        fx * -m1 + gx * m0, fy * -m1 + gy * m0,
                        tx3 < clip.lx ? clip.lx : tx3 > clip.ux ? clip.ux : tx3,
                        ty3 < clip.ly ? clip.ly : ty3 > clip.uy ? clip.uy : ty3,
                        function, info, s
                    );
                } else if (polygon) {
                    vx = mx <= clip.lx ? clip.lx : clip.ux;
                    (*function)(vx, ty0 < clip.ly ? clip.ly : ty0 > clip.uy ? clip.uy : ty0, vx, ty3 < clip.ly ? clip.ly : ty3 > clip.uy ? clip.uy : ty3, 0, info);
                }
            }
        }
    }
    static void divideCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, SegmentFunction function, void *info, float s) {
        float cx, bx, ax, cy, by, ay, a, count, dt, dt2, f3x, f2x, f1x, f3y, f2y, f1y;
        cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
        cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
        a = s * (ax * ax + ay * ay + bx * bx + by * by);
        count = a < 0.1f ? 1.f : 2.f + floorf(sqrtf(sqrtf(a)));
        dt = 1.f / count, dt2 = dt * dt;
        bx *= dt2, ax *= dt2 * dt, f3x = 6.f * ax, f2x = f3x + 2.f * bx, f1x = ax + bx + cx * dt;
        by *= dt2, ay *= dt2 * dt, f3y = 6.f * ay, f2y = f3y + 2.f * by, f1y = ay + by + cy * dt;
        x1 = x0, y1 = y0;
        while (--count) {
            x1 += f1x, f1x += f2x, f2x += f3x, y1 += f1y, f1y += f2y, f2y += f3y;
            (*function)(x0, y0, x1, y1, 1, info);
            x0 = x1, y0 = y1;
        }
        (*function)(x0, y0, x3, y3, dt == 1.f ? 0 : 2, info);
    }
    struct CurveIndexer {
        enum Flags { a = 1 << 15, c = 1 << 14, kMask = ~(a | c) };
        bool useCurves = false;  Bounds clip;  float px = FLT_MAX, py = FLT_MAX;  int is = 0;
        Segment *dst;  Row<Index> *indices;  Row<int16_t> *uxcovers;
        
        static void WriteSegment(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
            if (y0 != y1 || curve) {
                CurveIndexer *idxr = (CurveIndexer *)info;
                new (idxr->dst++) Segment(x0, y0, x1, y1, curve);
                if (curve == 0 || !idxr->useCurves)
                    idxr->indexLine(x0, y0, x1, y1);
                else if (curve == 1) {
                    if (idxr->px != FLT_MAX)
                        idxr->indexQuadratic(idxr->px, idxr->py, 0.25f * (idxr->px - x1) + x0, 0.25f * (idxr->py - y1) + y0, x0, y0);
                    idxr->px = x0, idxr->py = y0;
                } else {
                    idxr->indexQuadratic(idxr->px, idxr->py, 0.25f * (idxr->px - x1) + x0, 0.25f * (idxr->py - y1) + y0, x0, y0);
                    idxr->indexQuadratic(x0, y0, 0.25f * (x1 - idxr->px) + x0, 0.25f * (y1 - idxr->py) + y0, x1, y1);
                    idxr->px = FLT_MAX;
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
            is++;
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
            is++;
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
            int16_t *dst = uxcovers[ir].alloc(3);  dst[0] = int16_t(ceilf(ux)) | (a * Flags::a), dst[1] = cover, dst[2] = is;
        }
    };
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
    static void writeSegmentInstances(Bounds clip, bool even, size_t iz, bool opaque, bool fast, Context& ctx) {
        size_t ily = floorf(clip.ly * krfh), iuy = ceilf(clip.uy * krfh), iy, fastCount = 0, quadCount = 0, *count = fast ? & fastCount : & quadCount, i, begin;
        uint16_t counts[256];  float ly, uy, cover, winding, lx, ux;
        int edgeType = Instance::kEdge | (even ? Instance::kEvenOdd : 0) | (fast ? Instance::kFastEdges : 0);
        bool single = clip.ux - clip.lx < 256.f;
        uint32_t range = single ? powf(2.f, ceilf(log2f(clip.ux - clip.lx + 1.f))) : 256;
        Row<Index> *indices = & ctx.indices[0];  Row<int16_t> *uxcovers = & ctx.uxcovers[0];
        for (iy = ily; iy < iuy; iy++, indices->idx = indices->end, uxcovers->idx = uxcovers->end, indices++, uxcovers++) {
            if (indices->end != indices->idx) {
                if (indices->end - indices->idx > 32)
                    radixSort((uint32_t *)indices->base + indices->idx, int(indices->end - indices->idx), single ? clip.lx : 0, range, single, counts);
                else
                    std::sort(indices->base + indices->idx, indices->base + indices->end);
                Index *index;
                ly = iy * kfh, ly = ly < clip.ly ? clip.ly : ly > clip.uy ? clip.uy : ly;
                uy = (iy + 1) * kfh, uy = uy < clip.ly ? clip.ly : uy > clip.uy ? clip.uy : uy;
                for (cover = winding = 0.f, index = indices->base + indices->idx, lx = ux = index->x, i = begin = indices->idx; i < indices->end; i++, index++) {
                    if (index->x >= ux && fabsf((winding - floorf(winding)) - 0.5f) > 0.499f) {
                        if (lx != ux) {
                            Instance *inst = new (ctx.blends.alloc(1)) Instance(iz, edgeType);
                            *count = (i - begin + 1) / 2, ctx.allocator.allocAndCount(lx, ly, ux, uy, ctx.blends.end - 1, fastCount, quadCount, 0, 0, & inst->quad.cell);
                            inst->quad.cover = short(cover), inst->quad.count = uint16_t(i - begin), inst->quad.iy = int(iy - ily), inst->quad.begin = int(begin), inst->quad.base = int(ctx.segments.idx), inst->quad.idx = int(indices->idx);
                        }
                        winding = cover = truncf(winding + copysign(0.5f, winding));
                        if ((even && (int(winding) & 1)) || (!even && winding)) {
                            if (opaque) {
                                Instance *inst = new (ctx.opaques.alloc(1)) Instance(iz, 0);
                                new (& inst->quad.cell) Cell(ux, ly, index->x, uy, 0.f, 0.f);
                            } else {
                                Instance *inst = new (ctx.blends.alloc(1)) Instance(iz, Instance::kSolidCell);
                                new (& inst->quad.cell) Cell(ux, ly, index->x, uy, 0.f, 0.f);
                                ctx.allocator.passes.back().ui++;
                            }
                        }
                        begin = i, lx = ux = index->x;
                    }
                    int16_t *uxcover = uxcovers->base + uxcovers->idx + index->i * 3, _ux = (uint16_t)uxcover[0] & CurveIndexer::Flags::kMask;
                    ux = _ux > ux ? _ux : ux, winding += uxcover[1] * 0.00003051850948f;
                }
                if (lx != ux) {
                    Instance *inst = new (ctx.blends.alloc(1)) Instance(iz, edgeType);
                    *count = (i - begin + 1) / 2, ctx.allocator.allocAndCount(lx, ly, ux, uy, ctx.blends.end - 1, fastCount, quadCount, 0, 0, & inst->quad.cell);
                    inst->quad.cover = short(cover), inst->quad.count = uint16_t(i - begin), inst->quad.iy = int(iy - ily), inst->quad.begin = int(begin), inst->quad.base = int(ctx.segments.idx), inst->quad.idx = int(indices->idx);
                }
            }
        }
    }
    struct Outliner {
        uint32_t type;  Instance *dst0, *dst;  size_t iz;
        static void WriteInstance(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
            Outliner *out = (Outliner *)info;
            if (x0 != FLT_MAX) {
                Outline& outline = (new (out->dst++) Instance(out->iz, Instance::Type(out->type)))->outline;
                new (& outline.s) Segment(x0, y0, x1, y1, curve), outline.prev = -1, outline.next = 1;
            } else if (out->dst - out->dst0 > 0) {
                Outline& first = out->dst0->outline, & last = (out->dst - 1)->outline;
                float dx = first.s.x0 - last.s.x1, dy = first.s.y0 - last.s.y1;
                first.prev = dx * dx + dy * dy > 1e-6f ? 0 : int(out->dst - out->dst0 - 1), last.next = -first.prev;
                out->dst0 = out->dst;
            }
        }
    };
    static size_t writeContextsToBuffer(SceneList& list, Context *contexts, size_t count, size_t *begins, Buffer& buffer) {
        size_t size = buffer.headerSize, begin = buffer.headerSize, end = begin, sz, i, j, instances;
        for (i = 0; i < count; i++)
            size += contexts[i].opaques.end * sizeof(Instance);
        Context *ctx = contexts;   Allocator::Pass *pass;
        for (ctx = contexts, i = 0; i < count; i++, ctx++) {
            for (instances = 0, pass = ctx->allocator.passes.base, j = 0; j < ctx->allocator.passes.end; j++, pass++)
                instances += pass->quadEdges + pass->fastEdges + pass->fastMolecules + pass->quadMolecules;
            begins[i] = size, size += instances * sizeof(Edge) + (ctx->outlineInstances - ctx->outlinePaths + ctx->blends.end) * sizeof(Instance) + ctx->segments.end * sizeof(Segment) + ctx->p16total * sizeof(Geometry::Point16);
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
        Transform *ctms = (Transform *)(buffer.base + buffer.ctms);
        size_t i, j, iz, ip, is, lz, ic, end, segbase = 0, pbase = 0, pointsbase = 0, instcount = 0, instbase = 0;
        if (ctx->slz != ctx->suz) {
            if (ctx->segments.end || ctx->p16total) {
                segbase = begin, begin += ctx->segments.end * sizeof(Segment);
                memcpy(buffer.base + segbase, ctx->segments.base, begin - segbase);
                Row<Scene::Cache::Entry> *entries;
                for (pointsbase = begin, pbase = 0, i = lz = 0; i < list.scenes.size(); lz += list.scenes[i].count, i++)
                    for (entries = & list.scenes[i].cache->entries, ip = 0; ip < entries->end; ip++)
                        if (ctx->fasts.base[lz + ip]) {
                            end = begin + entries->base[ip].size * sizeof(Geometry::Point16);
                            memcpy(buffer.base + begin, entries->base[ip].p16s, end - begin);
                            begin = end, ctx->fasts.base[lz + ip] = uint32_t(pbase), pbase += entries->base[ip].size;
                        }
            }
            for (Allocator::Pass *pass = ctx->allocator.passes.base, *upass = pass + ctx->allocator.passes.end; pass < upass; pass++) {
                instcount = pass->quadEdges + pass->fastEdges + pass->fastMolecules + pass->quadMolecules, instbase = begin + instcount * sizeof(Edge);
                Edge *quadEdge = (Edge *)(buffer.base + begin);
                if (instcount)
                    entries.emplace_back(Buffer::kQuadEdges, begin, begin + pass->quadEdges * sizeof(Edge), segbase, pointsbase, instbase), begin = entries.back().end;
                Edge *fastEdge = (Edge *)(buffer.base + begin);
                if (instcount)
                    entries.emplace_back(Buffer::kFastEdges, begin, begin + pass->fastEdges * sizeof(Edge), segbase, pointsbase, instbase), begin = entries.back().end;
                Edge *fastMolecule = (Edge *)(buffer.base + begin);
                if (instcount)
                    entries.emplace_back(Buffer::kFastMolecules, begin, begin + pass->fastMolecules * sizeof(Edge), segbase, pointsbase, instbase), begin = entries.back().end;
                Edge *quadMolecule = (Edge *)(buffer.base + begin);
                if (instcount)
                    entries.emplace_back(Buffer::kQuadMolecules, begin, begin + pass->quadMolecules * sizeof(Edge), segbase, pointsbase, instbase), begin = entries.back().end;
            
                Instance *linst = ctx->blends.base + pass->li, *uinst = ctx->blends.base + pass->ui, *inst, *dst, *dst0;
                dst0 = dst = (Instance *)(buffer.base + begin);
                for (inst = linst; inst < uinst; inst++) {
                    iz = inst->iz & kPathIndexMask, is = idxs[iz] & 0xFFFFF, i = idxs[iz] >> 20;
                    if (inst->iz & Instance::kOutlines) {
                        Outliner out; out.type = (inst->iz & ~kPathIndexMask), out.dst = out.dst0 = dst, out.iz = iz;
                        divideGeometry(list.scenes[i].paths[is].ref, ctms[iz], inst->outline.clip, inst->outline.clip.lx == -FLT_MAX, false, true, & out, Outliner::WriteInstance);
                        dst = out.dst;
                    } else {
                        ic = dst - dst0, *dst++ = *inst;
                        if (inst->iz & Instance::kMolecule) {
                            Scene::Cache *cache = list.scenes[i].cache.ref;
                            ip = cache->ips.base[is];
                            dst[-1].quad.base = uint32_t(ctx->fasts.base[inst->quad.iy + ip]);
                            Scene::Cache::Entry *entry = & cache->entries.base[ip];
                            uint16_t ux = inst->quad.cell.ux;  Transform& ctm = ctms[iz];
                            float *molx = entry->mols + (ctm.a > 0.f ? 2 : 0), *moly = entry->mols + (ctm.c > 0.f ? 3 : 1);
                            bool update = entry->hasMolecules;  uint8_t *p16end = entry->p16end;
                            Edge *molecule = inst->iz & Instance::kFastEdges ? fastMolecule : quadMolecule;
                            for (j = 0; j < entry->size; j += kFastSegments, update = entry->hasMolecules && *p16end++) {
                                if (update)
                                    ux = ceilf(*molx * ctm.a + *moly * ctm.c + ctm.tx), molx += 4, moly += 4;
                                molecule->ic = uint32_t(ic), molecule->i0 = j, molecule->ux = ux, molecule++;
                            }
                            *(inst->iz & Instance::kFastEdges ? & fastMolecule : & quadMolecule) = molecule;
                        } else if (inst->iz & Instance::kEdge) {
                            Index *is = ctx->indices[inst->quad.iy].base + inst->quad.begin, *eis = is + inst->quad.count;
                            int16_t *uxcovers = ctx->uxcovers[inst->quad.iy].base + 3 * inst->quad.idx, *uxc;
                            Edge *edge = inst->iz & Instance::kFastEdges ? fastEdge : quadEdge;
                            for (; is < eis; is++, edge++) {
                                uxc = uxcovers + is->i * 3, edge->ic = uint32_t(ic) | Edge::a0 * bool(uxc[0] & CurveIndexer::Flags::a), edge->i0 = uint16_t(uxc[2]);
                                if (++is < eis)
                                    uxc = uxcovers + is->i * 3, edge->ic |= Edge::a1 * bool(uxc[0] & CurveIndexer::Flags::a), edge->ux = uint16_t(uxc[2]);
                                else
                                    edge->ux = kNullIndex;
                            }
                            *(inst->iz & Instance::kFastEdges ? & fastEdge : & quadEdge) = edge;
                        }
                    }
                }
                if (dst > dst0)
                    entries.emplace_back(Buffer::kInstances, begin, begin + (dst - dst0) * sizeof(Instance)), begin = entries.back().end;
            }
        }
    }
};
typedef Rasterizer Ra;
