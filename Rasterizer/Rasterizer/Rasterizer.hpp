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
        static Transform nullclip() { return { 1e12f, 0.f, 0.f, 1e12f, -5e11f, -5e11f }; }
        inline Transform concat(Transform t, float ax, float ay) const {
            Transform inv = invert(), m = Transform(a, b, c, d, ax, ay).concat(t);
            float ix = ax * inv.a + ay * inv.c + inv.tx, iy = ax * inv.b + ay * inv.d + inv.ty;
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
        inline float det() { return a * d - b * c; }
        float a, b, c, d, tx, ty;
    };
    struct Bounds {
        Bounds() : lx(FLT_MAX), ly(FLT_MAX), ux(-FLT_MAX), uy(-FLT_MAX) {}
        Bounds(float lx, float ly, float ux, float uy) : lx(lx), ly(ly), ux(ux), uy(uy) {}
        Bounds(Transform t) {
            lx = t.tx + (t.a < 0.f ? t.a : 0.f) + (t.c < 0.f ? t.c : 0.f), ly = t.ty + (t.b < 0.f ? t.b : 0.f) + (t.d < 0.f ? t.d : 0.f);
            ux = t.tx + (t.a > 0.f ? t.a : 0.f) + (t.c > 0.f ? t.c : 0.f), uy = t.ty + (t.b > 0.f ? t.b : 0.f) + (t.d > 0.f ? t.d : 0.f);
        }
        inline float area() {
            return (ux - lx) * (uy - ly);
        }
        inline bool contains(Bounds b) const {
            return lx <= b.lx && ux >= b.ux && ly <= b.ly && uy >= b.uy;
        }
        inline void extend(float x, float y) {
            lx = lx < x ? lx : x, ux = ux > x ? ux : x, ly = ly < y ? ly : y, uy = uy > y ? uy : y;
        }
        inline void extend(Bounds b) {
            extend(b.lx, b.ly), extend(b.ux, b.uy);
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
    static bool isVisible(Bounds user, Transform ctm, Transform clip, Bounds device, float width) {
        float uw = width < 0.f ? -width / sqrtf(fabsf(ctm.det())) : width;
        Transform unit = user.inset(-uw, -uw).unit(ctm);
        Bounds dev = Bounds(unit).intersect(device.intersect(Bounds(clip)));
        Bounds clu = Bounds(clip.invert().concat(unit));
        return dev.lx != dev.ux && dev.ly != dev.uy && clu.ux >= 0.f && clu.lx < 1.f && clu.uy >= 0.f && clu.ly < 1.f;
    }
    struct Colorant {
        Colorant(uint8_t src0, uint8_t src1, uint8_t src2, uint8_t src3) : src0(src0), src1(src1), src2(src2), src3(src3) {}
        uint8_t src0, src1, src2, src3;
    };
    struct Geometry {
        enum Type { kNull = 0, kMove, kLine, kQuadratic, kCubic, kClose, kCountSize };
        Geometry() : quadraticSums(0), cubicSums(0), px(0), py(0), isGlyph(false), isDrawable(false), refCount(0), hash(0) { bzero(counts, sizeof(counts)); }
        
        float *alloc(Type type, size_t size) {
            for (int i = 0; i < size; i++)
                types.emplace_back(type);
            size_t idx = points.size();
            points.resize(idx + size * 2), pts = & points[0];
            return & points[idx];
        }
        void update(Type type, size_t size, float *p) {
            counts[type]++, hash = ::crc64(::crc64(hash, & type, sizeof(type)), p, size * 2 * sizeof(float));
            if (type == kMove)
                molecules.emplace_back(Bounds()), mols = & molecules[0];
            else if (type == kQuadratic) {
                float *q = p - 2, ax = q[0] + q[4] - q[2] - q[2], ay = q[1] + q[5] - q[3] - q[3];
                quadraticSums += ceilf(sqrtf(sqrtf(ax * ax + ay * ay)));
            } else if (type == kCubic) {
                float *c = p - 2, cx, bx, ax, cy, by, ay;
                cx = 3.f * (c[2] - c[0]), bx = 3.f * (c[4] - c[2]) - cx, ax = c[6] - c[0] - cx - bx;
                cy = 3.f * (c[3] - c[1]), by = 3.f * (c[5] - c[3]) - cy, ay = c[7] - c[1] - cy - by;
                cubicSums += ceilf(sqrtf(sqrtf(ax * ax + ay * ay + bx * bx + by * by)));
            }
            while (size--)
                bounds.extend(p[0], p[1]), molecules.back().extend(p[0], p[1]), p += 2;
            isDrawable = types.size() > 1 && (bounds.lx != bounds.ux || bounds.ly != bounds.uy);
        }
        inline uint64_t cacheHash(Transform ctm) {
            if (counts[kCubic] == 0)
                return hash;
            float det = bounds.area() * fabsf(ctm.det());
            return hash + (det > 256.f) * ((*((uint32_t *)& det) & 0x7FFFFFFF) >> 24);
        }
        float upperBound(Transform ctm) {
            float det = fabsf(ctm.det()), s = sqrtf(sqrtf(det < 1e-2f ? 1e-2f : det));
            size_t quads = quadraticSums == 0 ? 0 : (det < 1.f ? ceilf(s * (quadraticSums + 2.f)) : ceilf(s) * quadraticSums);
            size_t cubics = cubicSums == 0 ? 0 : (det < 1.f ? ceilf(s * (cubicSums + 2.f)) : ceilf(s) * cubicSums);
            return quads + cubics + 2 * (molecules.size() + counts[kLine] + counts[kQuadratic] + counts[kCubic]);
        }
        void addBounds(Bounds b) { moveTo(b.lx, b.ly), lineTo(b.ux, b.ly), lineTo(b.ux, b.uy), lineTo(b.lx, b.uy); }
        void addEllipse(Bounds b) {
            const float t0 = 0.5f - 2.f / 3.f * (sqrtf(2.f) - 1.f), t1 = 1.f - t0, mx = 0.5f * (b.lx + b.ux), my = 0.5f * (b.ly + b.uy);
            moveTo(b.ux, my);
            cubicTo(b.ux, t0 * b.ly + t1 * b.uy, t0 * b.lx + t1 * b.ux, b.uy, mx, b.uy);
            cubicTo(t1 * b.lx + t0 * b.ux, b.uy, b.lx, t0 * b.ly + t1 * b.uy, b.lx, my);
            cubicTo(b.lx, t1 * b.ly + t0 * b.uy, t1 * b.lx + t0 * b.ux, b.ly, mx, b.ly);
            cubicTo(t0 * b.lx + t1 * b.ux, b.ly, b.ux, t1 * b.ly + t0 * b.uy, b.ux, my);
        }
        void moveTo(float x, float y) {
            float *points = alloc(kMove, 1);
            px = points[0] = x, py = points[1] = y;
            update(kMove, 1, points);
        }
        void lineTo(float x, float y) {
            if (px != x || py != y) {
                float *points = alloc(kLine, 1);
                px = points[0] = x, py = points[1] = y;
                update(kLine, 1, points);
            }
        }
        void quadTo(float cx, float cy, float x, float y) {
            float ax = cx - px, ay = cy - py, bx = x - cx, by = y - cy, det = ax * by - ay * bx, dot = ax * bx + ay * by;
            if (fabsf(det) < 1e-2f) {
                if (dot < 0.f && det)
                    lineTo((px + x) * 0.25f + cx * 0.5f, (py + y) * 0.25f + cy * 0.5f);
                lineTo(x, y);
            } else {
                float *points = alloc(kQuadratic, 2);
                points[0] = cx, points[1] = cy, px = points[2] = x, py = points[3] = y;
                update(kQuadratic, 2, points);
            }
        }
        void cubicTo(float cx0, float cy0, float cx1, float cy1, float x, float y) {
            float dx = 3.f * (cx0 - cx1) - px + x, dy = 3.f * (cy0 - cy1) - py + y;
            if (dx * dx + dy * dy < 1e-2f)
                quadTo((3.f * (cx0 + cx1) - px - x) * 0.25f, (3.f * (cy0 + cy1) - py - y) * 0.25f, x, y);
            else {
                float *points = alloc(kCubic, 3);
                points[0] = cx0, points[1] = cy0, points[2] = cx1, points[3] = cy1, px = points[4] = x, py = points[5] = y;
                update(kCubic, 3, points);
            }
        }
        void close() {
            update(kClose, 0, alloc(kClose, 1));
        }
        size_t refCount, quadraticSums, cubicSums, hash, counts[kCountSize];
        std::vector<uint8_t> types;
        std::vector<float> points;
        std::vector<Bounds> molecules;
        float px, py, *pts;
        bool isGlyph, isDrawable;
        Bounds bounds, *mols;
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
    typedef Ref<Geometry> Path;
    
    typedef void (*Function)(float x0, float y0, float x1, float y1, uint32_t curve, void *info);
    typedef void (*QuadFunction)(float x0, float y0, float x1, float y1, float x2, float y2, Function function, void *info, float s);
    typedef void (*CubicFunction)(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Function function, void *info, float s);
    
    struct Segment {
        Segment(float x0, float y0, float x1, float y1) : x0(x0), y0(y0), x1(x1), y1(y1) {}
        Segment(float _x0, float _y0, float _x1, float _y1, uint32_t curve) {
            *((uint32_t *)& x0) = (*((uint32_t *)& _x0) & ~3) | curve, y0 = _y0, x1 = _x1, y1 = _y1;
        }
        float x0, y0, x1, y1;
    };
    template<typename T>
    struct Vector {
        uint64_t refCount, hash;
        std::vector<T> v;
    };
    struct SceneBuffer {
        uint32_t i0(size_t ip) { return ip == 0 ? 0 : ends[ip - 1]; }
        uint32_t i1(size_t ip) { return ends[ip]; }
        
        void addPath(Path path) {
            auto it = cache.find(path->hash);
            if (it != cache.end())
                ips.emplace_back(it->second);
            else {
                cache.emplace(path->hash, bounds.size());
                writePath(path.ref, Transform(), Bounds(), true, true, true, writeSegment, writeQuadratic, writeCubic, this);
                ends.emplace_back(segments.size()), ips.emplace_back(bounds.size()), bounds.emplace_back(path->bounds);
                for (Bounds& m : path->molecules)  molecules.emplace_back(m);
            }
        }
        static void writeSegment(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
            SceneBuffer *buffer = (SceneBuffer *)info;
            std::vector<Segment>& segments = buffer->segments;
            size_t i = segments.size() - buffer->dst0, offset;
            if (x0 != FLT_MAX) {
                segments.emplace_back(Segment(x0, y0, x1, y1, curve));
                buffer->prevs.emplace_back(-1), buffer->nexts.emplace_back(1);
                if (i % 4 == 0)
                    buffer->ims.emplace_back(buffer->im);
            } else {
                if (i > 0) {
                    Segment& fs = segments[buffer->dst0], & ls = segments[buffer->dst0 + i - 1];
                    offset = (fs.x0 - ls.x1) * (fs.x0 - ls.x1) + (fs.y0 - ls.y1) * (fs.y0 - ls.y1) > 1e-6f ? 0 : i - 1;
                    buffer->prevs[buffer->dst0] = offset, buffer->nexts[buffer->dst0 + i - 1] = -offset;
                    while (i++ % 4)
                        segments.emplace_back(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX), buffer->prevs.emplace_back(0), buffer->nexts.emplace_back(0);
                }
                buffer->im++, buffer->dst0 = segments.size();
            }
        }
        size_t refCount = 0, dst0 = 0, im = 0;
        std::vector<Segment> segments;
        std::vector<Bounds> bounds, molecules;
        std::vector<int16_t> prevs, nexts;
        std::vector<uint32_t> ims, ends, ips;
        std::unordered_map<size_t, size_t> cache;
    };
    struct Scene {
        enum Flags { kVisible = 1 << 0, kFillEvenOdd = 1 << 1, kOutlineRounded = 1 << 2, kOutlineEndCap = 1 << 3 };
        void addPath(Path path, Transform ctm, Colorant color, float width, uint8_t flag) {
            if (path->isDrawable) {
                count++, weight += path->types.size();
                _paths->v.emplace_back(path), _ctms->v.emplace_back(ctm), _colors->v.emplace_back(color), _widths->v.emplace_back(width), _flags->v.emplace_back(flag), bounds.extend(Bounds(path->bounds.unit(ctm)).inset(-width, -width));
                _colors->hash = ::crc64(_colors->hash, & color, sizeof(color));
                hash = ::crc64(hash, & path->hash, sizeof(path->hash));
                paths = & _paths->v[0], ctms = & _ctms->v[0], colors = & _colors->v[0], widths = & _widths->v[0], flags = & _flags->v[0];
                buffer->addPath(path);
            }
        }
        size_t colorHash() { return _colors->hash; }
        size_t count = 0, weight = 0, hash = 0;
        Path *paths;  Transform *ctms;  Colorant *colors;  float *widths;  uint8_t *flags;  Bounds bounds;
        Ref<SceneBuffer> buffer;
    private:
        Ref<Vector<Path>> _paths; Ref<Vector<Transform>> _ctms;  Ref<Vector<Colorant>> _colors;  Ref<Vector<float>> _widths;  Ref<Vector<uint8_t>> _flags;
    };
    struct SceneList {
        SceneList& empty() {
            pathsCount = 0, scenes.resize(0), ctms.resize(0), clips.resize(0), bounds = Bounds();
            return *this;
        }
        SceneList& addScene(Scene scene) {
            return addScene(scene, Transform(), Transform::nullclip());
        }
        SceneList& addScene(Scene scene, Transform ctm, Transform clip) {
            if (scene.weight)
                pathsCount += scene.count, scenes.emplace_back(scene), ctms.emplace_back(ctm), clips.emplace_back(clip), bounds.extend(Bounds(scene.bounds.unit(ctm)));
            return *this;
        }
        void writeVisibles(Transform view, Bounds device, SceneList& visibles) {
            for (int i = 0; i < scenes.size(); i++)
                if (isVisible(scenes[i].bounds, view.concat(ctms[i]), view.concat(clips[i]), device, 0.f))
                    visibles.addScene(scenes[i], ctms[i], clips[i]);
        }
        size_t pathsCount = 0;  std::vector<Scene> scenes;  std::vector<Transform> ctms, clips;  Bounds bounds;
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
        Ref<Memory<T>> memory;
        size_t end = 0, idx = 0;
        T *base = nullptr;
    };
    struct Range {
        Range(size_t begin, size_t end) : begin(int(begin)), end(int(end)) {}
        int begin, end;
    };
    struct SegmentCounter {
        size_t count = 0;
        static void increment(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
            ((SegmentCounter *)info)->count++;
        }
    };
    struct Index {
        Index(uint16_t x, uint16_t i) : x(x), i(i) {}
        uint16_t x, i;
        inline bool operator< (const Index& other) const { return x < other.x; }
    };
    struct GPU {
        struct Cell {
            Cell(float lx, float ly, float ux, float uy, float ox, float oy) : lx(lx), ly(ly), ux(ux), uy(uy), ox(ox), oy(oy) {}
            uint16_t lx, ly, ux, uy, ox, oy;
        };
        struct Allocator {
            struct Pass {
                Pass(size_t idx) : li(idx), ui(idx) {}
                size_t cells = 0, edgeInstances = 0, fastInstances = 0, li, ui;
            };
            void init(size_t w, size_t h) {
                width = w, height = h, sheet = strip = fast = molecules = Bounds(0.f, 0.f, 0.f, 0.f), passes.empty();
            }
            Cell allocAndCount(float lx, float ly, float ux, float uy, size_t idx, size_t cells, size_t edgeInstances, size_t fastInstances) {
                float w = ux - lx, h = uy - ly;
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
                Cell cell(lx, ly, lx + w, uy, b->lx, b->ly);
                b->lx += w, pass->cells += cells, pass->ui++, pass->edgeInstances += edgeInstances, pass->fastInstances += fastInstances;
                return cell;
            }
            inline void countInstance() {
                Pass *pass = passes.end ? & passes.base[passes.end - 1] : new (passes.alloc(1)) Pass(0);
                pass->ui++;
            }
            size_t width, height;
            Row<Pass> passes;
            Bounds sheet, strip, fast, molecules;
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
            enum Type { kEvenOdd = 1 << 24, kRounded = 1 << 25, kEdge = 1 << 26, kSolidCell = 1 << 27, kEndCap = 1 << 28, kOutlines = 1 << 29,    kMolecule = 1 << 31 };
            Instance(size_t iz, int type) : iz((uint32_t)iz | type) {}
            union { Quad quad;  Outline outline; };
            uint32_t iz;
        };
        struct EdgeCell {
            Cell cell;
            uint32_t im, base;
        };
        struct Edge {
            enum Flags { a0 = 1 << 31, a1 = 1 << 30, c0 = 1 << 29, c1 = 1 << 28, kMask = ~(a0 | a1 | c0 | c1) };
            uint32_t ic;
            uint16_t i0, i1;
        };
        void empty() { zero(), blends.empty(), fasts.empty(), opaques.empty(); }
        void reset() { zero(), blends.reset(), fasts.reset(), opaques.reset(); }
        void zero() { outlinePaths = outlineUpper = upper = 0, minerr = INT_MAX; }
        size_t outlinePaths = 0, outlineUpper = 0, upper = 0, minerr = INT_MAX, slz, suz, total;
        Allocator allocator;
        Row<uint32_t> fasts;
        Row<Instance> blends, opaques;
    };
    struct Buffer {
        static constexpr size_t kPageSize = 4096;
        enum Type { kEdges, kFastEdges, kOpaques, kInstances };
        struct Entry {
            Entry(Type type, size_t begin, size_t end) : type(type), begin(begin), end(end), segments(0), cells(0) {}
            Type type;
            size_t begin, end, segments, cells;
        };
        ~Buffer() { if (base) free(base); }
        uint8_t *resize(size_t n) {
            size_t allocation = (n + kPageSize - 1) / kPageSize * kPageSize;
            if (size < allocation) {
                if (base) free(base);
                posix_memalign((void **)& base, kPageSize, allocation);
                size = allocation;
            }
            return base;
        }
        uint8_t *base = nullptr;
        Row<Entry> entries;
        bool useCurves = false;
        Colorant clearColor = Colorant(255, 255, 255, 255);
        size_t colors, transforms, clips, widths, sceneCount, tick, pathsCount, size = 0;
    };
    struct Context {
        void setGPU(size_t width, size_t height, size_t pathsCount, size_t slz, size_t suz) {
            bounds = Bounds(0.f, 0.f, width, height);
            size_t size = ceilf(float(height) * krfh);
            if (indices.size() != size)
                indices.resize(size), uxcovers.resize(size);
            gpu.allocator.init(width, height);
            gpu.slz = slz, gpu.suz = suz;
            bzero(gpu.fasts.alloc(pathsCount), pathsCount * sizeof(*gpu.fasts.base));
        }
        void drawList(SceneList& list, Transform view, Geometry **paths, Transform *ctms, Colorant *colors, Transform *clipctms, float *widths, float outlineWidth, Buffer *buffer) {
            size_t lz, uz, i, clz, cuz, iz, is;  Scene *scene;
            for (scene = & list.scenes[0], lz = i = 0; i < list.scenes.size(); i++, scene++, lz = uz) {
                uz = lz + scene->count;
                if ((clz = lz < gpu.slz ? gpu.slz : lz > gpu.suz ? gpu.suz : lz) != (cuz = uz < gpu.slz ? gpu.slz : uz > gpu.suz ? gpu.suz : uz)) {
                    Transform ctm = view.concat(list.ctms[i]), clipctm = view.concat(list.clips[i]), inv = clipctm.invert();
                    Bounds device = Bounds(clipctm).integral().intersect(bounds), uc = bounds.inset(1.f, 1.f).intersect(device);
                    float ws = sqrtf(fabsf(ctm.det())), err = fminf(1e-2f, 1e-2f / sqrtf(fabsf(clipctm.det()))), e0 = -err, e1 = 1.f + err;
                    for (is = clz - lz, iz = clz; iz < cuz; iz++, is++) {
                        paths[iz] = scene->paths[is].ref;
                        float w = outlineWidth ?: scene->widths[is], width = w * (w < 0.f ? -1.f : ws);
                        Transform m = ctm.concat(scene->ctms[is]), unit = paths[iz]->bounds.unit(m);
                        Bounds dev = Bounds(unit), clip = dev.inset(-width, -width).integral().intersect(device);
                        if (clip.lx != clip.ux && clip.ly != clip.uy) {
                            Bounds clu = Bounds(inv.concat(unit));
                            bool soft = clu.lx < e0 || clu.ux > e1 || clu.ly < e0 || clu.uy > e1;
                            bool unclipped = uc.contains(dev), fast = clip.uy - clip.ly <= kMoleculesHeight && clip.ux - clip.lx <= kMoleculesHeight;
                            ctms[iz] = m, widths[iz] = width, clipctms[iz] = clipctm;
                            if (fast && width == 0.f) {
                                gpu.fasts.base[lz + scene->buffer->ips[is]] = 1;
                                size_t ip = scene->buffer->ips[is], i0 = scene->buffer->i0(ip), i1 = scene->buffer->i1(ip);
                                GPU::Instance *inst = new (gpu.blends.alloc(1)) GPU::Instance(iz, GPU::Instance::kMolecule | (scene->flags[is] & Scene::kFillEvenOdd ? GPU::Instance::kEvenOdd : 0));
                                inst->quad.cell = gpu.allocator.allocAndCount(clip.lx, clip.ly, clip.ux, clip.uy, gpu.blends.end - 1, scene->paths[is].ref->molecules.size(), 0, (i1 - i0) / kFastSegments), inst->quad.cover = 0, inst->quad.iy = int(lz), inst->quad.idx = int((i << 16) | is);
                            } else
                                writeGPUPath(ctms[iz], scene, is, clip, width, colors[iz].src3 == 255 && !soft, iz, fast, unclipped, buffer->useCurves);
                        }
                    }
                }
            }
        }
        void writeGPUPath(Transform& ctm, Scene *scene, size_t is, Bounds clip, float width, bool opaque, size_t iz, bool fast, bool unclipped, bool useCurves) {
            uint8_t flags = scene->flags[is];  Geometry *geometry = scene->paths[is].ref;
            if (width) {
                GPU::Instance *inst = new (gpu.blends.alloc(1)) GPU::Instance(iz, GPU::Instance::kOutlines
                    | (flags & Scene::kOutlineRounded ? GPU::Instance::kRounded : 0)
                    | (flags & Scene::kOutlineEndCap ? GPU::Instance::kEndCap : 0));
                inst->outline.clip = unclipped ? Bounds(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX) : clip.inset(-width, -width);
                if (fabsf(ctm.det()) > 1e2f) {
                    SegmentCounter counter;  writePath(geometry, ctm, inst->outline.clip, false, false, true, SegmentCounter::increment, writeQuadratic, writeCubic, & counter);  gpu.outlineUpper += counter.count;
                } else
                    gpu.outlineUpper += geometry->upperBound(ctm);
                 gpu.outlinePaths++, gpu.allocator.countInstance();
            } else {
                float sx = 1.f - 2.f * kClipMargin / (clip.ux - clip.lx), tx = clip.lx * (1.f - sx) + kClipMargin;
                float sy = 1.f - 2.f * kClipMargin / (clip.uy - clip.ly), ty = clip.ly * (1.f - sy) + kClipMargin;
                ctm = Transform(sx, 0.f, 0.f, sy, tx, ty).concat(ctm);
                CurveIndexer out;  out.clip = clip, out.indices = & indices[0] - int(clip.ly * krfh), out.uxcovers = & uxcovers[0] - int(clip.ly * krfh), out.useCurves = useCurves;
                out.dst = segments.alloc(geometry->upperBound(ctm));
                writePath(geometry, ctm, clip, unclipped, true, false, CurveIndexer::WriteSegment, writeQuadratic, writeCubic, & out);
                segments.end = out.dst - segments.base;
                writeSegmentInstances(& indices[0], & uxcovers[0], int(segments.idx), clip, flags & Scene::kFillEvenOdd, iz, opaque, gpu);
                segments.idx = segments.end;
            }
        }
        void empty() { gpu.empty(), segments.empty();  for (int i = 0; i < indices.size(); i++)  indices[i].empty(), uxcovers[i].empty();  }
        void reset() { gpu.reset(), segments.reset(), indices.resize(0), uxcovers.resize(0); }
        GPU gpu;
        Bounds bounds;
        std::vector<Row<Index>> indices;
        std::vector<Row<int16_t>> uxcovers;
        Row<Segment> segments;
    };
    static void writePath(Geometry *geometry, Transform ctm, Bounds clip, bool unclipped, bool polygon, bool mark, Function function, QuadFunction quadFunction, CubicFunction cubicFunction, void *info) {
        float *p = geometry->pts, sx = FLT_MAX, sy = FLT_MAX, x0 = FLT_MAX, y0 = FLT_MAX, x1, y1, x2, y2, x3, y3, ly, uy, lx, ux;
        for (size_t index = 0; index < geometry->types.size(); )
            switch (geometry->types[index]) {
                case Geometry::kMove:
                    if (polygon && sx != FLT_MAX && (sx != x0 || sy != y0)) {
                        if (unclipped)
                            (*function)(x0, y0, sx, sy, 0, info);
                        else {
                            ly = y0 < sy ? y0 : sy, uy = y0 > sy ? y0 : sy;
                            if (!unclipped && ly < clip.uy && uy > clip.ly) {
                                if (ly < clip.ly || uy > clip.uy || (x0 < sx ? x0 : sx) < clip.lx || (x0 > sx ? x0 : sx) > clip.ux)
                                    writeClippedLine(x0, y0, sx, sy, clip, polygon, function, info);
                                else
                                    (*function)(x0, y0, sx, sy, 0, info);
                            }
                        }
                    }
                    if (mark && sx != FLT_MAX)
                        (*function)(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, 0, info);
                    sx = x0 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, sy = y0 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty, p += 2, index++;
                    break;
                case Geometry::kLine:
                    x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                    if (unclipped)
                        (*function)(x0, y0, x1, y1, 0, info);
                    else {
                        ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1;
                        if (ly < clip.uy && uy > clip.ly) {
                            if (ly < clip.ly || uy > clip.uy || (x0 < x1 ? x0 : x1) < clip.lx || (x0 > x1 ? x0 : x1) > clip.ux)
                                writeClippedLine(x0, y0, x1, y1, clip, polygon, function, info);
                            else
                                (*function)(x0, y0, x1, y1, 0, info);
                        }
                    }
                    x0 = x1, y0 = y1, p += 2, index++;
                    break;
                case Geometry::kQuadratic:
                    x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                    x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                    if (unclipped)
                        (*quadFunction)(x0, y0, x1, y1, x2, y2, function, info, kQuadraticScale);
                    else {
                        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2;
                        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2;
                        if (ly < clip.uy && uy > clip.ly) {
                            lx = x0 < x1 ? x0 : x1, lx = lx < x2 ? lx : x2;
                            ux = x0 > x1 ? x0 : x1, ux = ux > x2 ? ux : x2;
                            if (polygon || !(ux < clip.lx || lx > clip.ux)) {
                                if (ly < clip.ly || uy > clip.uy || lx < clip.lx || ux > clip.ux)
                                    writeClippedQuadratic(x0, y0, x1, y1, x2, y2, clip, lx, ly, ux, uy, polygon, function, quadFunction, info, kQuadraticScale);
                                else
                                    (*quadFunction)(x0, y0, x1, y1, x2, y2, function, info, kQuadraticScale);
                            }
                        }
                    }
                    x0 = x2, y0 = y2, p += 4, index += 2;
                    break;
                case Geometry::kCubic:
                    x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                    x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                    x3 = p[4] * ctm.a + p[5] * ctm.c + ctm.tx, y3 = p[4] * ctm.b + p[5] * ctm.d + ctm.ty;
                    if (unclipped)
                        (*cubicFunction)(x0, y0, x1, y1, x2, y2, x3, y3, function, info, kCubicScale);
                    else {
                        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2, ly = ly < y3 ? ly : y3;
                        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2, uy = uy > y3 ? uy : y3;
                        if (ly < clip.uy && uy > clip.ly) {
                            lx = x0 < x1 ? x0 : x1, lx = lx < x2 ? lx : x2, lx = lx < x3 ? lx : x3;
                            ux = x0 > x1 ? x0 : x1, ux = ux > x2 ? ux : x2, ux = ux > x3 ? ux : x3;
                            if (polygon || !(ux < clip.lx || lx > clip.ux)) {
                                if (ly < clip.ly || uy > clip.uy || lx < clip.lx || ux > clip.ux)
                                    writeClippedCubic(x0, y0, x1, y1, x2, y2, x3, y3, clip, lx, ly, ux, uy, polygon, function, cubicFunction, info, kCubicScale);
                                else
                                    (*cubicFunction)(x0, y0, x1, y1, x2, y2, x3, y3, function, info, kCubicScale);
                            }
                        }
                    }
                    x0 = x3, y0 = y3, p += 6, index += 3;
                    break;
                case Geometry::kClose:
                    p += 2, index++;
                    break;
            }
        if (polygon && sx != FLT_MAX && (sx != x0 || sy != y0)) {
            if (unclipped)
                (*function)(x0, y0, sx, sy, 0, info);
            else {
                ly = y0 < sy ? y0 : sy, uy = y0 > sy ? y0 : sy;
                if (ly < clip.uy && uy > clip.ly) {
                    if (ly < clip.ly || uy > clip.uy || (x0 < sx ? x0 : sx) < clip.lx || (x0 > sx ? x0 : sx) > clip.ux)
                        writeClippedLine(x0, y0, sx, sy, clip, polygon, function, info);
                    else
                        (*function)(x0, y0, sx, sy, 0, info);
                }
            }
        }
        if (mark && sx != FLT_MAX)
            (*function)(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX, 0, info);
    }
    static void writeClippedLine(float x0, float y0, float x1, float y1, Bounds clip, bool polygon, Function function, void *info) {
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
    static float *solveQuadratic(double A, double B, double C, float *ts) {
        if (fabs(A) < 1e-3) {
            float t = -C / B;  if (t > 0.f && t < 1.f)  *ts++ = t;
        } else {
            double d = B * B - 4.0 * A * C, r = sqrt(d);
            if (d >= 0.0) {
                float t0 = (-B + r) * 0.5 / A;  if (t0 > 0.f && t0 < 1.f)  *ts++ = t0;
                float t1 = (-B - r) * 0.5 / A;  if (t1 > 0.f && t1 < 1.f)  *ts++ = t1;
            }
        }
        return ts;
    }
    static void writeClippedQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, Bounds clip, float lx, float ly, float ux, float uy, bool polygon, Function function, QuadFunction quadFunction, void *info, float s) {
        float ax, bx, ay, by, ts[10], *et = ts, *t, mt, mx, my, vx, tx0, ty0, tx2, ty2;
        ax = x0 + x2 - x1 - x1, bx = 2.f * (x1 - x0), ay = y0 + y2 - y1 - y1, by = 2.f * (y1 - y0);
        *et++ = 0.f;
        if (clip.ly >= ly && clip.ly < uy)
            et = solveQuadratic(ay, by, y0 - clip.ly, et);
        if (clip.uy >= ly && clip.uy < uy)
            et = solveQuadratic(ay, by, y0 - clip.uy, et);
        if (clip.lx >= lx && clip.lx < ux)
            et = solveQuadratic(ax, bx, x0 - clip.lx, et);
        if (clip.ux >= lx && clip.ux < ux)
            et = solveQuadratic(ax, bx, x0 - clip.ux, et);
        std::sort(ts + 1, et), *et++ = 1.f;
        for (tx0 = tx2 = x0, ty0 = ty2 = y0, t = ts; t < et - 1; t++, tx0 = tx2, ty0 = ty2) {
            tx2 = (ax * t[1] + bx) * t[1] + x0, ty2 = (ay * t[1] + by) * t[1] + y0;
            if (t[0] != t[1]) {
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
    }
    static void writeQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, Function function, void *info, float s) {
        if (s == 0.f) {
            float x = 0.25f * (x0 + x2) + 0.5f * x1, y = 0.25f * (y0 + y2) + 0.5f * y1;
            (*function)(x0, y0, x, y, 1, info), (*function)(x, y, x2, y2, 2, info);;
        } else {
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
    }
    static float *solveCubic(double A, double B, double C, double D, float *ts) {
        if (fabs(D) < 1e-3)
            return solveQuadratic(A, B, C, ts);
        else {
            const double wq0 = 2.0 / 27.0, third = 1.0 / 3.0;
            double  p, q, q2, u1, v1, a3, discriminant, sd, t;
            A /= D, B /= D, C /= D, p = B - A * A * third, q = A * (wq0 * A * A - third * B) + C;
            q2 = q * 0.5, a3 = A * third, discriminant = q2 * q2 + p * p * p / 27.0;
            if (discriminant < 0) {
                double mp3 = -p / 3, mp33 = mp3 * mp3 * mp3, r = sqrt(mp33), tcos = -q / (2 * r), crtr = 2 * copysign(cbrt(fabs(r)), r), sine, cosine;
                __sincos(acos(tcos < -1 ? -1 : tcos > 1 ? 1 : tcos) / 3, & sine, & cosine);
                t = crtr * cosine - a3; if (t > 0.f && t < 1.f)  *ts++ = t;
                t = crtr * (-0.5 * cosine - 0.866025403784439 * sine) - a3; if (t > 0.f && t < 1.f)  *ts++ = t;
                t = crtr * (-0.5 * cosine + 0.866025403784439 * sine) - a3; if (t > 0.f && t < 1.f)  *ts++ = t;
            } else if (discriminant == 0) {
                u1 = copysign(cbrt(fabs(q2)), q2);
                t = 2 * u1 - a3; if (t > 0.f && t < 1.f)  *ts++ = t;
                t = -u1 - a3; if (t > 0.f && t < 1.f)  *ts++ = t;
            } else {
                sd = sqrt(discriminant), u1 = copysign(cbrt(fabs(sd - q2)), sd - q2), v1 = copysign(cbrt(fabs(sd + q2)), sd + q2);
                t = u1 - v1 - a3; if (t > 0.f && t < 1.f)  *ts++ = t;
            }
        }
        return ts;
    }
    static void writeClippedCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Bounds clip, float lx, float ly, float ux, float uy, bool polygon, Function function, CubicFunction cubicFunction, void *info, float s) {
        float cy, by, ay, cx, bx, ax, ts[14], *et = ts, *t, mt, mx, my, vx, tx0, ty0, tx1, ty1, tx2, ty2, tx3, ty3, fx, gx, fy, gy;
        cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
        cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
        *et++ = 0.f;
        if (clip.ly >= ly && clip.ly < uy)
            et = solveCubic(by, cy, y0 - clip.ly, ay, et);
        if (clip.uy >= ly && clip.uy < uy)
            et = solveCubic(by, cy, y0 - clip.uy, ay, et);
        if (clip.lx >= lx && clip.lx < ux)
            et = solveCubic(bx, cx, x0 - clip.lx, ax, et);
        if (clip.ux >= lx && clip.ux < ux)
            et = solveCubic(bx, cx, x0 - clip.ux, ax, et);
        std::sort(ts + 1, et), *et++ = 1.f;
        for (tx0 = tx3 = x0, ty0 = ty3 = y0, t = ts; t < et - 1; t++, tx0 = tx3, ty0 = ty3) {
            tx3 = ((ax * t[1] + bx) * t[1] + cx) * t[1] + x0, ty3 = ((ay * t[1] + by) * t[1] + cy) * t[1] + y0;
            if (t[0] != t[1]) {
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
    }
    static void writeCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Function function, void *info, float s) {
        float cx, bx, ax, cy, by, ay, a, count, dt, dt2, f3x, f2x, f1x, f3y, f2y, f1y;
        cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
        cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
        a = s * (ax * ax + ay * ay + bx * bx + by * by);
        count = a < s ? 1.f : a < 16.f ? 3.f : 2.f + floorf(sqrtf(sqrtf(a)));
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
                    if (idxr->px != FLT_MAX) {
                        float ax = x0 - 0.25f * (idxr->px + x1), ay = y0 - 0.25f * (idxr->py + y1);
                        idxr->indexCurve(idxr->px, idxr->py, 0.5f * idxr->px + ax, 0.5f * idxr->py + ay, x0, y0);
                    }
                    idxr->px = x0, idxr->py = y0;
                } else {
                    float ax = x0 - 0.25f * (idxr->px + x1), ay = y0 - 0.25f * (idxr->py + y1);
                    idxr->indexCurve(idxr->px, idxr->py, 0.5f * idxr->px + ax, 0.5f * idxr->py + ay, x0, y0);
                    idxr->indexCurve(x0, y0, 0.5f * x1 + ax, 0.5f * y1 + ay, x1, y1);
                    idxr->px = FLT_MAX;
                }
            }
        }
        __attribute__((always_inline)) void indexLine(float x0, float y0, float x1, float y1) {
            if ((uint32_t(y0) & kFatMask) == (uint32_t(y1) & kFatMask))
                writeIndex(y0 * krfh, x0 < x1 ? x0 : x1, x0 > x1 ? x0 : x1, (y1 - y0) * kCoverScale, false, false);
            else {
                float lx, ux, ly, uy, m, c, y, ny, minx, maxx, scale;  int ir;
                lx = x0 < x1 ? x0 : x1, ux = x0 > x1 ? x0 : x1;
                ly = y0 < y1 ? y0 : y1, uy = y0 > y1 ? y0 : y1, scale = y0 < y1 ? kCoverScale : -kCoverScale;
                ir = ly * krfh, y = ir * kfh;
                m = (x1 - x0) / (y1 - y0), c = x0 - m * y0;
                minx = (y + (m < 0.f ? kfh : 0.f)) * m + c;
                maxx = (y + (m > 0.f ? kfh : 0.f)) * m + c;
                for (m *= kfh, y = ly; y < uy; y = ny, minx += m, maxx += m, ir++) {
                    ny = (floorf(y * krfh) + 1.f) * kfh, ny = uy < ny ? uy : ny;
                    writeIndex(ir, minx > lx ? minx : lx, maxx < ux ? maxx : ux, (ny - y) * scale, false, false);
                }
            }
            is++;
        }
        __attribute__((always_inline)) void indexCurve(float x0, float y0, float x1, float y1, float x2, float y2) {
            float ay, by, ax, bx, iy;
            ay = y2 - y1, by = y1 - y0;
            if (fabsf(ay) < kMonotoneFlatness || fabsf(by) < kMonotoneFlatness || (ay > 0.f) == (by > 0.f)) {
                if ((uint32_t(y0) & kFatMask) == (uint32_t(y2) & kFatMask))
                    writeIndex(y0 * krfh, x0 < x2 ? x0 : x2, x0 > x2 ? x0 : x2, (y2 - y0) * kCoverScale, true, true);
                else
                    ax = x2 - x1, bx = x1 - x0, writeCurve(y0, y2, ay - by, 2.f * by, y0, ax - bx, 2.f * bx, x0, is, true);
            } else {
                iy = y0 - by * by / (ay - by), iy = iy < clip.ly ? clip.ly : iy > clip.uy ? clip.uy : iy;
                ax = x2 - x1, bx = x1 - x0;
                if (y0 != iy)
                    writeCurve(y0, iy, ay - by, 2.f * by, y0, ax - bx, 2.f * bx, x0, is, true);
                if (iy != y2)
                    writeCurve(iy, y2, ay - by, 2.f * by, y0, ax - bx, 2.f * bx, x0, is, false);
            }
            is++;
        }
        __attribute__((always_inline)) void writeCurve(float w0, float w1, float ay, float by, float y0, float ax, float bx, float x0, int is, bool a) {
            float ly, uy, d2a, ity, d, t0, t1, itx, tx0, tx1, y, ny, sign = w1 < w0 ? -1.f : 1.f, lx, ux, ix;  int ir;
            ly = w0 < w1 ? w0 : w1, uy = w0 > w1 ? w0 : w1, d2a = 0.5f / ay, ity = -by * d2a, d2a *= sign, sign *= kCoverScale;
            itx = fabsf(ax) < kFlatness ? FLT_MAX : -bx / ax * 0.5f;
            if (fabsf(ay) < kFlatness)
                t0 = -(y0 - ly) / by;
            else
                d = by * by - 4.f * ay * (y0 - ly), t0 = ity + sqrtf(d < 0.f ? 0.f : d) * d2a;
            tx0 = (ax * t0 + bx) * t0 + x0;
            for (ir = ly * krfh, y = ly; y < uy; y = ny, ir++, t0 = t1, tx0 = tx1) {
                ny = (floorf(y * krfh) + 1.f) * kfh, ny = uy < ny ? uy : ny;
                if (fabsf(ay) < kFlatness)
                    t1 = -(y0 - ny) / by;
                else
                    d = by * by - 4.f * ay * (y0 - ny), t1 = ity + sqrtf(d < 0.f ? 0.f : d) * d2a;
                tx1 = (ax * t1 + bx) * t1 + x0;
                lx = tx0 < tx1 ? tx0 : tx1, ux = tx0 > tx1 ? tx0 : tx1;
                if ((t0 <= itx) == (itx <= t1))
                    ix = (ax * itx + bx) * itx + x0, lx = lx < ix ? lx : ix, ux = ux > ix ? ux : ix;
                writeIndex(ir, lx, ux, sign * (ny - y), a, true);
            }
        }
        __attribute__((always_inline)) void writeIndex(int ir, float lx, float ux, int16_t cover, bool a, bool c) {
            Row<Index>& row = indices[ir];  size_t i = row.end - row.idx;  new (row.alloc(1)) Index(lx, i);
            int16_t *dst = uxcovers[ir].alloc(3);  dst[0] = int16_t(ceilf(ux)) | (a * Flags::a) | (c * Flags::c), dst[1] = cover, dst[2] = is;
        }
    };
    static void writeSegmentInstances(Row<Index> *indices, Row<int16_t> *uxcovers, int base, Bounds clip, bool even, size_t iz, bool opaque, GPU& gpu) {
        size_t ily = floorf(clip.ly * krfh), iuy = ceilf(clip.uy * krfh), iy, count, i, begin;
        uint16_t counts[256];
        float ly, uy, cover, winding, lx, ux;
        bool single = clip.ux - clip.lx < 256.f;
        uint32_t range = single ? powf(2.f, ceilf(log2f(clip.ux - clip.lx + 1.f))) : 256;
        for (iy = ily; iy < iuy; iy++, indices->idx = indices->end, uxcovers->idx = uxcovers->end, indices++, uxcovers++) {
            if ((count = indices->end - indices->idx)) {
                if (indices->end - indices->idx > 32)
                    radixSort((uint32_t *)indices->base + indices->idx, int(indices->end - indices->idx), single ? clip.lx : 0, range, single, counts);
                else
                    std::sort(indices->base + indices->idx, indices->base + indices->end);
                Index *index;
                ly = iy * kfh, ly = ly < clip.ly ? clip.ly : ly > clip.uy ? clip.uy : ly;
                uy = (iy + 1) * kfh, uy = uy < clip.ly ? clip.ly : uy > clip.uy ? clip.uy : uy;
                for (cover = winding = 0.f, index = indices->base + indices->idx, lx = ux = index->x, i = begin = indices->idx; i < indices->end; i++, index++) {
                    if (index->x >= ux && fabsf(winding - roundf(winding)) < 1e-3f) {
                        if (lx != ux) {
                            GPU::Cell cell = gpu.allocator.allocAndCount(lx, ly, ux, uy, gpu.blends.end, 1, (i - begin + 1) / 2, 0);
                            GPU::Instance *inst = new (gpu.blends.alloc(1)) GPU::Instance(iz, GPU::Instance::kEdge | (even ? GPU::Instance::kEvenOdd : 0));
                            inst->quad.cell = cell, inst->quad.cover = short(roundf(cover)), inst->quad.count = uint16_t(i - begin), inst->quad.iy = int(iy - ily), inst->quad.begin = int(begin), inst->quad.base = base, inst->quad.idx = int(indices->idx);
                        }
                        if (alphaForCover(winding, even) > 0.998f) {
                            if (opaque) {
                                GPU::Instance *inst = new (gpu.opaques.alloc(1)) GPU::Instance(iz, 0);
                                new (& inst->quad.cell) GPU::Cell(ux, ly, index->x, uy, 0.f, 0.f);
                            } else {
                                GPU::Instance *inst = new (gpu.blends.alloc(1)) GPU::Instance(iz, GPU::Instance::kSolidCell);
                                new (& inst->quad.cell) GPU::Cell(ux, ly, index->x, uy, 0.f, 0.f);
                                gpu.allocator.countInstance();
                            }
                        }
                        begin = i, lx = ux = index->x, cover = winding = roundf(winding);
                    }
                    int16_t *uxcover = uxcovers->base + uxcovers->idx + index->i * 3, _ux = (uint16_t)uxcover[0] & CurveIndexer::Flags::kMask;
                    ux = _ux > ux ? _ux : ux, winding += uxcover[1] * 0.00003051850948f;
                }
                if (lx != ux) {
                    GPU::Cell cell = gpu.allocator.allocAndCount(lx, ly, ux, uy, gpu.blends.end, 1, (i - begin + 1) / 2, 0);
                    GPU::Instance *inst = new (gpu.blends.alloc(1)) GPU::Instance(iz, GPU::Instance::kEdge | (even ? GPU::Instance::kEvenOdd : 0));
                    inst->quad.cell = cell, inst->quad.cover = short(roundf(cover)), inst->quad.count = uint16_t(i - begin), inst->quad.iy = int(iy - ily), inst->quad.begin = int(begin), inst->quad.base = base, inst->quad.idx = int(indices->idx);
                }
            }
        }
    }
    struct OutlineInfo {
        uint32_t type;  GPU::Instance *dst0, *dst;  size_t iz;
        static void writeInstance(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
            OutlineInfo *in = (OutlineInfo *)info;
            if (x0 != FLT_MAX) {
                GPU::Outline& outline = (new (in->dst++) GPU::Instance(in->iz, GPU::Instance::Type(in->type)))->outline;
                new (& outline.s) Segment(x0, y0, x1, y1, curve), outline.prev = -1, outline.next = 1;
            } else if (in->dst - in->dst0 > 0) {
                GPU::Outline& first = in->dst0->outline, & last = (in->dst - 1)->outline;
                float dx = first.s.x0 - last.s.x1, dy = first.s.y0 - last.s.y1;
                first.prev = dx * dx + dy * dy > 1e-6f ? 0 : (int)(in->dst - in->dst0 - 1), last.next = -first.prev;
                in->dst0 = in->dst;
            }
        }
    };
    static size_t writeContextsToBuffer(SceneList& list,
                                        Context *contexts, size_t count,
                                        Colorant *colorants,
                                        Transform *ctms,
                                        Transform *clips,
                                        float *widths,
                                        size_t pathsCount,
                                        size_t *begins,
                                        Buffer& buffer) {
        size_t szcolors = pathsCount * sizeof(Colorant), sztransforms = pathsCount * sizeof(Transform), szwidths = pathsCount * sizeof(float);
        size_t size = szcolors + 2 * sztransforms + szwidths, sz, i, j, begin, end, cells, instances, lz, puz, ip;
        for (i = 0; i < count; i++)
            size += contexts[i].gpu.opaques.end * sizeof(GPU::Instance);
        for (i = 0; i < count; i++) {
            begins[i] = size;
            GPU& gpu = contexts[i].gpu;
            for (cells = 0, instances = 0, j = 0; j < gpu.allocator.passes.end; j++)
                cells += gpu.allocator.passes.base[j].cells, instances += gpu.allocator.passes.base[j].edgeInstances, instances += gpu.allocator.passes.base[j].fastInstances;
            size += instances * sizeof(GPU::Edge) + cells * sizeof(GPU::EdgeCell) + (gpu.outlineUpper - gpu.outlinePaths + gpu.blends.end) * sizeof(GPU::Instance);
            Scene *scene = & list.scenes[0], *uscene = scene + list.scenes.size();
            for (gpu.total = 0, lz = 0; scene < uscene; lz += scene->count, scene++)
                for (puz = scene->buffer->bounds.size(), ip = 0; ip < puz; ip++)
                    if (gpu.fasts.base[lz + ip])
                        gpu.total += scene->buffer->i1(ip) - scene->buffer->i0(ip);
            size += (contexts[i].segments.end + gpu.total) * sizeof(Segment);
        }
        buffer.resize(size);
        buffer.colors = 0, buffer.transforms = buffer.colors + szcolors, buffer.clips = buffer.transforms + sztransforms, buffer.widths = buffer.clips + sztransforms;
        buffer.pathsCount = pathsCount;
        memcpy(buffer.base + buffer.colors, colorants, szcolors);
        memcpy(buffer.base + buffer.transforms, ctms, sztransforms);
        memcpy(buffer.base + buffer.clips, clips, sztransforms);
        memcpy(buffer.base + buffer.widths, widths, szwidths);
        begin = end = buffer.widths + szwidths;
            
        for (i = 0; i < count; i++)
            if ((sz = contexts[i].gpu.opaques.end * sizeof(GPU::Instance)))
                memcpy(buffer.base + end, contexts[i].gpu.opaques.base, sz), end += sz;
        if (begin != end)
            new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::kOpaques, begin, end);
        return size;
    }
    static void writeContextToBuffer(SceneList& list,
                                     Context *ctx,
                                     Geometry **paths,
                                     size_t begin,
                                     std::vector<Buffer::Entry>& entries,
                                     Buffer& buffer) {
        Transform *ctms = (Transform *)(buffer.base + buffer.transforms);
        size_t j, iz, puz, ip, im, lz, size, i0, i1, count, segbase = 0, totalbase = 0, cellbase = 0;
        if (ctx->gpu.slz != ctx->gpu.suz) {
            size = (ctx->segments.end + ctx->gpu.total) * sizeof(Segment);
            if (size) {
                Segment *dst = (Segment *)(buffer.base + begin);
                memcpy(dst, ctx->segments.base, ctx->segments.end * sizeof(Segment));
                totalbase = ctx->segments.end;
                dst = (Segment *)(buffer.base + begin + totalbase * sizeof(Segment));
                Scene *scene = & list.scenes[0], *uscene = scene + list.scenes.size();
                for (lz = 0; scene < uscene; lz += scene->count, scene++)
                   for (puz = scene->buffer->bounds.size(), ip = 0; ip < puz; ip++)
                       if (ctx->gpu.fasts.base[lz + ip]) {
                           count = scene->buffer->i1(ip) - scene->buffer->i0(ip);
                           memcpy(dst, & scene->buffer->segments[scene->buffer->i0(ip)], count * sizeof(Segment));
                           ctx->gpu.fasts.base[lz + ip] = uint32_t(totalbase), totalbase += count, dst += count;
                       }
                segbase = begin, begin = begin + size;
            }
            for (GPU::Allocator::Pass *pass = ctx->gpu.allocator.passes.base, *upass = pass + ctx->gpu.allocator.passes.end; pass < upass; pass++, begin = entries.back().end) {
                GPU::EdgeCell *cell = (GPU::EdgeCell *)(buffer.base + begin), *c0 = cell;
                cellbase = begin, begin = begin + pass->cells * sizeof(GPU::EdgeCell);
                
                GPU::Edge *edge = (GPU::Edge *)(buffer.base + begin);
                if (pass->cells) {
                    entries.emplace_back(Buffer::kEdges, begin, begin + pass->edgeInstances * sizeof(GPU::Edge)), begin = entries.back().end;
                    entries.back().segments = segbase, entries.back().cells = cellbase;
                }
                GPU::Edge *fast = (GPU::Edge *)(buffer.base + begin);
                if (pass->cells) {
                    entries.emplace_back(Buffer::kFastEdges, begin, begin + pass->fastInstances * sizeof(GPU::Edge)), begin = entries.back().end;
                    entries.back().segments = segbase, entries.back().cells = cellbase;
                }
                GPU::Instance *linst = ctx->gpu.blends.base + pass->li, *uinst = ctx->gpu.blends.base + pass->ui, *inst, *dst, *dst0;
                dst0 = dst = (GPU::Instance *)(buffer.base + begin);
                for (inst = linst; inst < uinst; inst++) {
                    iz = inst->iz & kPathIndexMask;
                    if (inst->iz & GPU::Instance::kOutlines) {
                        OutlineInfo info; info.type = (inst->iz & ~kPathIndexMask), info.dst = info.dst0 = dst, info.iz = iz;
                        writePath(paths[iz], ctms[iz], inst->outline.clip, inst->outline.clip.lx == -FLT_MAX, false, true, OutlineInfo::writeInstance, writeQuadratic, writeCubic, & info);
                        if (dst == info.dst)
                            OutlineInfo::writeInstance(0.f, 0.f, 0.f, 0.f, 0, & info);
                        dst = info.dst, ctms[iz] = Transform();
                    } else {
                        *dst++ = *inst;
                        if (inst->iz & GPU::Instance::kMolecule) {
                            int is = inst->quad.idx & 0xFFFF, i = inst->quad.idx >> 16;
                            Bounds *b = list.scenes[i].paths[is]->mols;
                            float ta, tc, ux;
                            Transform& ctm = ctms[iz];
                            if (1) {
                                Scene *scene = & list.scenes[i];
                                ip = scene->buffer->ips[is], i0 = scene->buffer->i0(ip), i1 = scene->buffer->i1(ip), im = INT_MAX;
                                for (j = i0; j < i1; j += kFastSegments, fast++) {
                                    if (im != scene->buffer->ims[j >> 2]) {
                                        im = scene->buffer->ims[j >> 2], b = & scene->buffer->molecules[im];
                                        cell->cell = inst->quad.cell, cell->im = int(iz), cell->base = uint32_t(ctx->gpu.fasts.base[inst->quad.iy + ip]);
                                        ta = ctm.a * (b->ux - b->lx), tc = ctm.c * (b->uy - b->ly);
                                        ux = ceilf(b->lx * ctm.a + b->ly * ctm.c + ctm.tx + (ta > 0.f ? ta : 0.f) + (tc > 0.f ? tc : 0.f));
                                        cell->cell.ux = ux < cell->cell.lx ? cell->cell.lx : ux > cell->cell.ux ? cell->cell.ux : ux;
                                        cell++;
                                    }
                                    fast->ic = uint32_t(cell - c0 - 1), fast->i0 = j - i0, fast->i1 = 0xFFFF;
                                }
                            } else {
//                                Cache::Entry *e = ctx->gpu.cache.entries.base + inst->quad.iy;
//                                int bc = 0, *lc = ctx->gpu.cache.counts.base + e->cnt.begin, *uc = ctx->gpu.cache.counts.base + e->cnt.end, *cnt = lc;
//                                for (uint32_t ic = uint32_t(cell - c0); cnt < uc; ic++, cell++, bc = *cnt++ + 1, b++) {
//                                    cell->cell = inst->quad.cell, cell->im = int(iz), cell->base = uint32_t(e->seg.begin);
//                                    ta = ctm.a * (b->ux - b->lx), tc = ctm.c * (b->uy - b->ly);
//                                    ux = ceilf(b->lx * ctm.a + b->ly * ctm.c + ctm.tx + (ta > 0.f ? ta : 0.f) + (tc > 0.f ? tc : 0.f));
//                                    cell->cell.ux = ux < cell->cell.lx ? cell->cell.lx : ux > cell->cell.ux ? cell->cell.ux : ux;
//                                    for (j = bc; j < *cnt; fast++)
//                                        fast->ic = ic, fast->i0 = j, j += kFastSegments, fast->i1 = j;
//                                    (fast - 1)->i1 = *cnt;
//                                }
//                                ctm = ctm.concat(e->ctm);
                            }
                        } else if (inst->iz & GPU::Instance::kEdge) {
                            cell->cell = inst->quad.cell;
                            cell->im = kNullIndex, cell->base = uint32_t(inst->quad.base);
                            Index *is = ctx->indices[inst->quad.iy].base + inst->quad.begin;
                            int16_t *uxcovers = ctx->uxcovers[inst->quad.iy].base + 3 * inst->quad.idx, *uxc;
                            uint32_t ic = uint32_t(cell - c0);
                            for (j = 0; j < inst->quad.count; j++, edge++) {
                                uxc = uxcovers + is->i * 3, edge->ic = ic | (bool(uint16_t(uxc[0]) & CurveIndexer::Flags::a) * GPU::Edge::a0) | (bool(uint16_t(uxc[0]) & CurveIndexer::Flags::c) * GPU::Edge::c0), edge->i0 = uint16_t(uxc[2]), is++;
                                if (++j < inst->quad.count)
                                    uxc = uxcovers + is->i * 3, edge->ic |= (bool(uint16_t(uxc[0]) & CurveIndexer::Flags::a) * GPU::Edge::a1) | (bool(uint16_t(uxc[0]) & CurveIndexer::Flags::c) * GPU::Edge::c1), edge->i1 = uint16_t(uxc[2]), is++;
                                else
                                    edge->i1 = kNullIndex;
                            }
                            cell++;
                        }
                    }
                }
                entries.emplace_back(Buffer::kInstances, begin, begin + (dst - dst0) * sizeof(GPU::Instance));
            }
        }
        ctx->empty();
    }
};
typedef Rasterizer Ra;
