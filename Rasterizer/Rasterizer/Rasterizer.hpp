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
        inline Transform concat(Transform t) const {
            return {
                t.a * a + t.b * c, t.a * b + t.b * d,
                t.c * a + t.d * c, t.c * b + t.d * d,
                t.tx * a + t.ty * c + tx, t.tx * b + t.ty * d + ty
            };
        }
        inline Transform preconcat(Transform t, float cx, float cy) const {
            return Transform(t.a, t.b, t.c, t.d, t.tx + cx, t.ty + cy).concat(Transform(a, b, c, d, tx - cx, ty - cy));
        }
        inline Transform invert() const {
            float det = a * d - b * c, recip = 1.f / det;
            return det == 0.f ? *this : Transform(
                d * recip,                      -b * recip,
                -c * recip,                     a * recip,
                (c * ty - d * tx) * recip,      -(a * ty - b * tx) * recip
            );
        }
        inline float scale() const { return sqrtf(fabsf(a * d - b * c)); }
        float a, b, c, d, tx, ty;
    };
    struct Bounds {
        static inline Bounds huge() { return Bounds(-5e11f, -5e11f, 5e11f, 5e11f); }
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
            lx = x < lx ? x : lx, ly = y < ly ? y : ly, ux = x > ux ? x : ux, uy = y > uy ? y : uy;
        }
        inline void extend(Bounds b) {
            lx = b.lx < lx ? b.lx : lx, ly = b.ly < ly ? b.ly : ly, ux = b.ux > ux ? b.ux : ux, uy = b.uy > uy ? b.uy : uy;
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
        inline bool isHuge() { return lx == -5e11f; }
        inline Transform unit(Transform t) const {
            return { t.a * (ux - lx), t.b * (ux - lx), t.c * (uy - ly), t.d * (uy - ly), lx * t.a + ly * t.c + t.tx, lx * t.b + ly * t.d + t.ty };
        }
        size_t hash()  { return XXH64(this, sizeof(*this), 0); }
        float lx, ly, ux, uy;
    };
    struct Colorant {
        Colorant(uint8_t b, uint8_t g, uint8_t r, uint8_t a) : b(b), g(g), r(r), a(a) {}
        uint8_t b, g, r, a;
    };
    struct Gradient {
        Colorant stops[2];
        float x0, y0, yx, y1;
    };
    template<typename T>
    struct Ref {
        Ref()                               { ptr = new T(), ptr->refCount = 1; }
        ~Ref()                              { if (--(ptr->refCount) == 0) delete ptr; }
        Ref(const Ref& other)               { *this = other; }
        Ref& operator= (const Ref& other)   {
            if (this != & other) {
                if (ptr)
                    this->~Ref();
                ptr = other.ptr, ptr->refCount++;
            }
            return *this;
        }
        T* operator->() { return ptr; }
        T *ptr = nullptr;
    };
    template<typename T>
    struct Memory {
        ~Memory() { if (addr) free(addr); }
        void resize(size_t n) { size = n, addr = (T *)realloc(addr, size * sizeof(T)); }
        size_t refCount = 0, size = 0;  T *addr = nullptr;
    };
    template<typename T>
    struct Row {
        inline T *alloc(size_t n) {
            end += n;
            if (memory->size < end)
                memory->resize(end * 1.5), base = memory->addr;
            return base + end - n;
        }
        inline void prealloc(size_t n) {  alloc(n), end -= n;  }
        inline T *zalloc(size_t n) {
            return (T*)memset(alloc(n), 0, n * sizeof(T));
        }
        inline T& back() { return base[end - 1]; }
        Row<T>& empty() { end = idx = 0; return *this; }
        void reset() { end = idx = 0, base = nullptr, memory = Ref<Memory<T>>(); }
        Row<T>& operator+(const T *src) { if (src) { do *(alloc(1)) = *src; while (*src++);  --end; } return *this; }
        Row<T>& operator+(const int n) { char buf[32]; bzero(buf, sizeof(buf)), snprintf(buf, 32, "%d", n); operator+((T *)buf); return *this; }
        T *base = nullptr;  Ref<Memory<T>> memory;  size_t end = 0, idx = 0;
    };
    struct Range {
        Range(size_t begin, size_t end) : begin(int(begin)), end(int(end)) {}
        int begin, end;
    };
    struct Index {
        uint16_t x, i;
        inline bool operator< (const Index& other) const { return x < other.x; }
    };
    struct Writer {
        virtual void writeSegment(float x0, float y0, float x1, float y1) = 0;
        virtual void Quadratic(float x0, float y0, float x1, float y1, float x2, float y2) = 0;
        virtual void Cubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
            float cx, bx, ax, cy, by, ay, adot, bdot, count, dt, dt2, f3x, f2x, f1x, f3y, f2y, f1y, x, y;
            cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1), ax = x3 - x0 - bx, bx -= cx;
            cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1), ay = y3 - y0 - by, by -= cy;
            adot = ax * ax + ay * ay, bdot = bx * bx + by * by;
            if (cubicScale > 0.f && adot + bdot < 1.f)
                writeSegment(x0, y0, x3, y3);
            else {
                count = ceilf(cbrtf(sqrtf(adot + 1e-12f) / (fabsf(cubicScale) * kCubicMultiplier))), dt = 0.5f / count, dt2 = dt * dt;
                x = x0, bx *= dt2, ax *= dt2 * dt, f3x = 6.f * ax, f2x = f3x + 2.f * bx, f1x = ax + bx + cx * dt;
                y = y0, by *= dt2, ay *= dt2 * dt, f3y = 6.f * ay, f2y = f3y + 2.f * by, f1y = ay + by + cy * dt;
                while (--count) {
                    x += f1x, f1x += f2x, f2x += f3x, y += f1y, f1y += f2y, f2y += f3y;
                    x1 = x, y1 = y;
                    x += f1x, f1x += f2x, f2x += f3x, y += f1y, f1y += f2y, f2y += f3y;
                    Quadratic(x0, y0, 2.f * x1 - 0.5f * (x0 + x), 2.f * y1 - 0.5f * (y0 + y), x, y);
                    x0 = x, y0 = y;
                }
                x += f1x, f1x += f2x, f2x += f3x, y += f1y, f1y += f2y, f2y += f3y;
                Quadratic(x0, y0, 2.f * x - 0.5f * (x0 + x3), 2.f * y - 0.5f * (y0 + y3), x3, y3);
            }
        }
        virtual void EndSubpath(float x0, float y0, float x1, float y1, uint32_t curve) {}
        
        float quadraticScale = 1.f, cubicScale = kCubicPrecision;
    };
    struct Point16 {
        enum Flags { isCurve = 0x8000, kMask = ~isCurve };
        uint16_t x, y;
    };
    
    struct Atom {
        enum Flags { isCurve = 1 << 31, isEnd = 1 << 30, isClose = 1 << 29, kMask = ~(isCurve | isEnd | isClose) };
        uint32_t i;
    };
    
    struct Geometry {
        enum Type { kMove, kLine, kQuadratic, kCubic, kClose, kCountSize };

        void update(Type type, size_t size, float *p) {
            counts[type]++;  memset(types.alloc(size), type, size);
            for (int i = 0; i < size; i++)
                molecules.back().extend(p[i * 2], p[i * 2 + 1]);
            bounds.extend(molecules.back());
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
        void moveTo(float x, float y) {
            validate(), new (molecules.alloc(1)) Bounds(), points.idx = points.end;
            float *pts = points.alloc(2);  x0 = pts[0] = x, y0 = pts[1] = y, update(kMove, 1, pts);
        }
        void lineTo(float x1, float y1) {
            float ax = x1 - x0, ay = y1 - y0, dot = ax * ax + ay * ay;
            if (dot == 0.f)
                return;
            float *pts = points.alloc(2);  x0 = pts[0] = x1, y0 = pts[1] = y1, update(kLine, 1, pts);
        }
        void quadTo(float x1, float y1, float x2, float y2) {
            float ax, bx, ay, by, dot, t, err = 1e-2f;
            ax = x1 - x0, bx = x2 - x0, ay = y1 - y0, by = y2 - y0, dot = bx * bx + by * by;
            if (dot == 0.f)
                return;
            t = fabsf(ax * -by + ay * bx) / dot;
            if (t < err) {
                lineTo(x2, y2);
            } else {
                float *pts = points.alloc(4);  pts[0] = x1, pts[1] = y1, x0 = pts[2] = x2, y0 = pts[3] = y2, update(kQuadratic, 2, pts);
                ax = x2 + x0 - x1 - x1, ay = y2 + y0 - y1 - y1, maxCurve = fmaxf(maxCurve, ax * ax + ay * ay);
            }
        }
        void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
            float cx, bx, ax, cy, by, ay, dot, t0, t1, err = 1e-2f, s;
            ax = x1 - x0, bx = x2 - x0, cx = x3 - x0, ay = y1 - y0, by = y2 - y0, cy = y3 - y0, dot = cx * cx + cy * cy;
            if (dot == 0.f)
                return;
            t0 = fabsf(ax * -cy + ay * cx) / dot, t1 = fabsf(bx * -cy + by * cx) / dot;
            if (t0 < err && t1 < err) {
                lineTo(x3, y3);
            } else {
                bx = 3.f * (x2 - x1), ax = x3 - x0 - bx, by = 3.f * (y2 - y1), ay = y3 - y0 - by, dot = ax * ax + ay * ay;
                if (dot < 1e-4f)
                    quadTo((3.f * (x1 + x2) - x0 - x3) * 0.25f, (3.f * (y1 + y2) - y0 - y3) * 0.25f, x3, y3);
                else {
                    float *pts = points.alloc(6);  pts[0] = x1, pts[1] = y1, pts[2] = x2, pts[3] = y2, pts[4] = x3, pts[5] = y3, update(kCubic, 3, pts);
                    s = ceilf(cbrtf(sqrtf(dot + 1e-12f) / (kCubicPrecision * kCubicMultiplier)));
                    maxCurve = fmaxf(maxCurve, dot / s);
                    bx -= 3.f * (x1 - x0), by -= 3.f * (y1 - y0), dot += bx * bx + by * by, x0 = x3, y0 = y3;
                    cubicSums += ceilf(sqrtf(sqrtf(dot)));
                }
            }
        }
        void close() {
            float *pts = points.alloc(2);  pts[0] = x0, pts[1] = y0, update(kClose, 1, pts);
        }
        bool isValid() {
            validate();
            return types.end > 1 && *types.base == Geometry::kMove && (bounds.lx != bounds.ux || bounds.ly != bounds.uy);
        }
        size_t hash() {
            xxhash = xxhash ?: XXH64(points.base, points.end * sizeof(float), XXH64(types.base, types.end * sizeof(uint8_t), 0));
            return xxhash;
        }
        size_t refCount = 0, xxhash = 0, minUpper = 0, cubicSums = 0, counts[kCountSize] = { 0, 0, 0, 0, 0 };
        float x0 = 0.f, y0 = 0.f, maxCurve = 0.f;  Row<uint8_t> types;  Row<float> points;
        Bounds bounds;  Row<Bounds> molecules;
        Row<Point16> p16s;  Row<uint8_t> p16cnts;  Row<Atom> atoms;
    };
    typedef Ref<Geometry> Path;
    
    struct P16Writer: Writer {
        void writeGeometry(Geometry *g) {
            float dim = fmaxf(g->bounds.ux - g->bounds.lx, g->bounds.uy - g->bounds.ly);
            float s = kMoleculesRange / dim, det = s * s;
            size_t upper = 4 * g->molecules.end + g->upperBound(fmaxf(kMinUpperDet, det));
            Transform m = Transform(s, 0.f, 0.f, s, s * -g->bounds.lx, s * -g->bounds.ly);
            g->p16cnts.prealloc(upper / kFastSegments);
            p0 = pidx = p = g->p16s.alloc(upper);
            a0 = aidx = a = g->atoms.alloc(upper);
            p16s = & g->p16s, p16cnts = & g->p16cnts, atoms = & g->atoms;
            cubicScale = -kCubicPrecision * (kMoleculesRange / kMoleculesHeight);
            divideGeometry(g, m, Bounds(), true, true, *this);
            assert(g->p16s.end <= upper);
            assert(g->atoms.end <= upper);
        }
        void writeSegment(float x0, float y0, float x1, float y1) {
            p->x = fmaxf(0.f, fminf(kMoleculesRange, x0));
            p->y = fmaxf(0.f, fminf(kMoleculesRange, y0));
            a->i = uint32_t(p - p0), a++, p++;
        }
        void Quadratic(float x0, float y0, float x1, float y1, float x2, float y2) {
            p[0].x = uint16_t(fmaxf(0.f, fminf(kMoleculesRange, x0))) | Point16::isCurve;
            p[0].y = fmaxf(0.f, fminf(kMoleculesRange, y0));
            p[1].x = fmaxf(0.f, fminf(kMoleculesRange, 0.5f * x1 + 0.25f * (x0 + x2)));
            p[1].y = fmaxf(0.f, fminf(kMoleculesRange, 0.5f * y1 + 0.25f * (y0 + y2)));
            a->i = uint32_t(p - p0) | Atom::isCurve, a++, p += 2;
        }
        void EndSubpath(float x0, float y0, float x1, float y1, uint32_t curve) {
            p->x = fmaxf(0.f, fminf(kMoleculesRange, x1));
            p->y = fmaxf(0.f, fminf(kMoleculesRange, y1));
            p++;
            
            bool isClose = bool(curve & 2) && !bool(curve & 1);
            
            if (aidx < a)
                a[-1].i |= Atom::isEnd | isClose * Atom::isClose;
            aidx = a;
            atoms->end = a - a0;
           
            size_t segcnt = p - pidx - 1, icount = (segcnt + kFastSegments) / kFastSegments, rem, sz;
            uint8_t *cnt = p16cnts->alloc(icount), flags = 0x80 | (isClose * 0x8);
            for (int i = 0; i < icount; i++) {
                rem = segcnt - i * kFastSegments, sz = rem > kFastSegments ? kFastSegments : rem;
                cnt[i] = sz | ((sz && sz == rem) * flags);
            }
            p = pidx = p0 + p16cnts->end * kFastSegments;
            p16s->end = p - p0;
        }
        Point16 *p0, *pidx, *p;  Row<Point16> *p16s;   Row<uint8_t> *p16cnts;
        Atom *a0, *aidx, *a;  Row<Atom> *atoms;
    };
    
    struct Scene {
        Scene() {  bzero(clipCache->entries.alloc(1), sizeof(*clipCache->entries.base));  }
        
        template<typename T>
        struct Cache {
            T *entryAt(size_t i) {  return entries.base + ips.base[i];  }
            T *addEntry(size_t hash) {
                T *e = nullptr;  auto ip = ips.alloc(1);  *ip = 0;
                if (hash != 0) {
                    auto it = map.find(hash);
                    if (it != map.end())
                        *ip = it->second;
                    else
                        *ip = uint32_t(entries.end), map.emplace(hash, *ip), e = entries.alloc(1), bzero(e, sizeof(T));
                }
                return e;
            }
            size_t refCount = 0;  Row<uint32_t> ips;  Row<T> entries;  std::unordered_map<size_t, uint32_t> map;
        };
        template<typename T>
        struct Vector {
            uint64_t refCount;  T *base;  std::vector<T> src, dst;
            void add(T obj) {  src.emplace_back(obj), dst.emplace_back(obj), base = & dst[0]; }
        };
        
        enum Flags { kInvisible = 1 << 0, kFillEvenOdd = 1 << 1, kRoundCap = 1 << 2, kSquareCap = 1 << 3 };
        void addPath(Path path, Transform ctm, Colorant color, float width, uint8_t flag, Bounds *clipBounds = nullptr, void *image = nullptr) {
            if (path->isValid()) {
                Geometry *g = path.ptr;
                count++, weight += g->types.end;
                Bounds *be;
                if (kMoleculesHeight && g->p16s.end == 0) {
                    P16Writer writer;
                    writer.writeGeometry(g);
                }
                if ((be = clipCache->addEntry(clipBounds ? clipBounds->hash() : 0)))
                    *be = *clipBounds;
                g->minUpper = g->minUpper ?: g->upperBound(kMinUpperDet), xxhash = XXH64(& g->xxhash, sizeof(g->xxhash), xxhash);
                paths->add(path), bnds->add(g->bounds), ctms->add(ctm), colors->add(image ? Colorant(0, 0, 0, 64) : color), widths->add(width), flags->add(flag);
            }
        }
        Bounds bounds() {
            Bounds b;
            for (int i = 0; i < count; i++)
                if ((flags->base[i] & kInvisible) == 0)
                    b.extend(Bounds(bnds->base[i].inset(-0.5f * widths->base[i], -0.5f * widths->base[i]).unit(ctms->base[i])).intersect(clipCache->ips.base[i] ? *clipCache->entryAt(i) : Bounds::huge()));
            return b;
        }
        size_t count = 0, xxhash = 0, weight = 0;  uint64_t tag = 1;
        Ref<Cache<Bounds>> clipCache;
        Ref<Vector<Path>> paths;  Ref<Vector<Bounds>> bnds;
        Ref<Vector<Transform>> ctms;  Ref<Vector<Colorant>> colors;  Ref<Vector<float>> widths;  Ref<Vector<uint8_t>> flags;
    };
    struct SceneList {
        Bounds bounds() {
            Bounds b;
            for (int i = 0; i < scenes.size(); i++)
                b.extend(Bounds(scenes[i].bounds().unit(ctms[i])));
            return b;
        }
        SceneList& empty() {
            pathsCount = 0, scenes.resize(0), ctms.resize(0);
            return *this;
        }
        SceneList& addList(SceneList& list) {
            for (int i = 0; i < list.scenes.size(); i++)
                addScene(list.scenes[i], list.ctms[i]);
            return *this;
        }
        SceneList& addScene(Scene scene, Transform ctm = Transform(), Transform clip = Transform(1e12f, 0.f, 0.f, 1e12f, -5e11f, -5e11f)) {
            if (scene.weight)
                pathsCount += scene.count, scenes.emplace_back(scene), ctms.emplace_back(ctm);
            return *this;
        }
        size_t pathsCount = 0;  std::vector<Scene> scenes;  std::vector<Transform> ctms;
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
        Segment s;  short prev, next;  float cx, cy;
    };
    struct Instance {
        enum Type { kPCurve = 1 << 24, kEvenOdd = 1 << 24, kRoundCap = 1 << 25, kEdge = 1 << 26, kNCurve = 1 << 27, kSquareCap = 1 << 28, kOutlines = 1 << 29, kFastEdges = 1 << 30, kMolecule = 1 << 31 };
        Instance(size_t iz) : iz(uint32_t(iz)) {}
        uint32_t iz;  union { Quad quad;  Outline outline; };
    };
    struct Blend : Instance {
        Blend(size_t iz) : Instance(iz) {}
        union { struct { int count, idx; } data;  Bounds clip; };
    };
    struct Edge {
        uint32_t ic;  enum Flags { isClose = 1 << 31, a1 = 1 << 30, ue0 = 0xF << 26, ue1 = 0xF << 22, kMask = ~(isClose | a1 | ue0 | ue1) };
        uint16_t i0, ux;
    };
    struct Sample {
        Sample(float lx, float ux, float cover, size_t is): lx(lx), ux(ceilf(ux)), cover(int16_t(cover)), is(uint32_t(is)) {}
        int16_t lx, ux, cover;  uint32_t is;
    };
    struct Buffer {
        enum Type { kQuadEdges, kFastEdges, kFastOutlines, kQuadOutlines, kFastMolecules, kQuadMolecules, kOpaques, kInstances, kSegmentsBase, kPointsBase, kInstancesBase };
        struct Entry {
            Entry(Type type, size_t begin, size_t end) : type(type), begin(begin), end(end) {}
            Type type;  size_t begin, end;
        };
        ~Buffer() { if (base) free(base); }
        
        void prepare(SceneList& list) {
            pathsCount = list.pathsCount;
            size_t i, sizes[] = { sizeof(Colorant), sizeof(Transform), sizeof(Transform), sizeof(float), sizeof(Bounds), sizeof(uint32_t) };
            size_t count = sizeof(sizes) / sizeof(*sizes), base = 0, bases[count];
            for (i = 0; i < count; i++)
                bases[i] = base, base += pathsCount * sizes[i];
            colors = bases[0], ctms = bases[1], clips = bases[2], widths = bases[3], bounds = bases[4], idxs = bases[5];
            headerSize = (base + 15) & ~15, resize(headerSize), entries.empty();
        }
        void resize(size_t n, size_t copySize = 0) {
            if (copySize == 0 && allocation && size > 1000000 && size / allocation > 5)
                allocation = size = 0, free(base), base = nullptr;
            allocation = (n + kPageSize - 1) / kPageSize * kPageSize;
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
            _ctms = (Transform *)(base + ctms), _colors = (Colorant *)(base + colors), _clips = (Transform *)(base + clips), _idxs = (uint32_t *)(base + idxs), _widths = (float *)(base + widths), _bounds = (Bounds *)(base + bounds);
        }
        uint8_t *base = nullptr;  Row<Entry> entries;
        bool useCurves = false, fastOutlines = false;  Colorant clearColor = Colorant(255, 255, 255, 255);
        size_t colors, ctms, clips, widths, bounds, idxs, pathsCount, headerSize, size = 0, allocation = 0;
        Transform *_ctms, *_clips;  Colorant *_colors;  uint32_t *_idxs;  float *_widths;  Bounds *_bounds;
    };
    struct Allocator {
        struct Pass {
            Pass(size_t idx) : idx(idx) {}
            size_t idx, counts[6] = { 0, 0, 0, 0, 0, 0 };
            size_t count() { return counts[0] + counts[1] + counts[2] + counts[3] + counts[4] + counts[5]; }
        };
        void empty(Bounds device) {
            full = device, sheet = molecules = Bounds(0.f, 0.f, 0.f, 0.f), bzero(strips, sizeof(strips)), passes.empty(), new (passes.alloc(1)) Pass(0);
        }
        void refill(size_t idx) {
            sheet = full, molecules = Bounds(0.f, 0.f, 0.f, 0.f), bzero(strips, sizeof(strips)), new (passes.alloc(1)) Pass(idx);
        }
        inline void alloc(float lx, float ly, float ux, float uy, size_t idx, Cell *cell, int type, size_t count) {
            float w = ux - lx, h = uy - ly;  Bounds *b;  float hght;
            if (h <= kFastHeight) {
                hght = ceilf(h / 4) * 4;
                b = strips + size_t(hght / 4 - 1);
            } else
                b = & molecules, hght = kMoleculesHeight;
            if (b->ux - b->lx < w) {
                if (sheet.uy - sheet.ly < hght)
                    refill(idx);
                b->lx = sheet.lx, b->ly = sheet.ly, b->ux = sheet.ux, b->uy = sheet.ly + hght, sheet.ly = b->uy;
            }
            cell->ox = b->lx, cell->oy = b->ly, cell->lx = lx, cell->ly = ly, cell->ux = ux, cell->uy = uy, b->lx += w;
            passes.back().counts[type] += count;
        }
        Row<Pass> passes;  enum CountType { kFastEdges, kQuadEdges, kFastOutlines, kQuadOutlines, kFastMolecules, kQuadMolecules };
        Bounds full, sheet, molecules, strips[kFastHeight / 4];
    };
    
    struct Context {
        void drawList(SceneList& list, Bounds device, Transform view, size_t slz, size_t suz, Buffer *buffer) {
            empty(), allocator.empty(device);
            size_t fatlines = 1.f + ceilf((device.uy - device.ly) * krfh);
            if (samples.size() != fatlines)
                samples.resize(fatlines);
            fasts.zalloc(list.pathsCount);
            
            size_t lz, uz, i, clz, cuz, iz, is, ip, lastip, size, cnt;  Scene *scn = & list.scenes[0];  uint8_t flags;
            float err, e0, e1, det, width, uw;
            for (lz = uz = i = 0; i < list.scenes.size(); i++, scn++, lz = uz) {
                uz = lz + scn->count, clz = lz < slz ? slz : lz > suz ? suz : lz, cuz = uz < slz ? slz : uz > suz ? suz : uz;
                Transform ctm = view.concat(list.ctms[i]), clipctm, inv, m, unit;
                Bounds dev, clip, *bnds, clipBounds;
                lastip = ~0, e0 = 0.f, e1 = 1.f;
                memcpy(buffer->_colors + clz, & scn->colors->base[clz - lz].b, (cuz - clz) * sizeof(Colorant));
                for (is = clz - lz, iz = clz; iz < cuz; iz++, is++) {
                    if ((flags = scn->flags->base[is]) & Scene::Flags::kInvisible)
                        continue;
                    m = ctm.concat(scn->ctms->base[is]), det = fabsf(m.a * m.d - m.b * m.c), uw = scn->widths->base[is], bnds = & scn->bnds->base[is];
                    width = uw * (uw > 0.f ? sqrtf(det) : -1.f);
                    ip = scn->clipCache->ips.base[is];
                    if (ip != lastip) {
                        lastip = ip;
                        clipctm = ip ? scn->clipCache->entryAt(is)->unit(ctm) : Transform(1e12f, 0.f, 0.f, 1e12f, -5e11f, -5e11f);
                        inv = clipctm.invert(), err = fminf(1e-2f, 1e-2f / clipctm.scale()), e0 = -err, e1 = 1.f + err;
                        clipBounds = Bounds(clipctm).integral().intersect(device);
                    }
                    unit = bnds->unit(m), dev = Bounds(unit).inset(-width, -width), clip = dev.integral().intersect(clipBounds);
                    if (clip.lx < clip.ux && clip.ly < clip.uy) {
                        buffer->_ctms[iz] = m, buffer->_widths[iz] = width, buffer->_clips[iz] = clipctm, buffer->_idxs[iz] = uint32_t((i << 20) | is);
                        Geometry *g = scn->paths->base[is].ptr;
                        bool useMolecules = clip.uy - clip.ly <= kMoleculesHeight && clip.ux - clip.lx <= kMoleculesHeight;
                        if (width && !(buffer->fastOutlines && useMolecules && width <= 2.f)) {
                           Blend *inst = new (blends.alloc(1)) Blend(iz | Instance::kOutlines | bool(flags & Scene::kRoundCap) * Instance::kRoundCap | bool(flags & Scene::kSquareCap) * Instance::kSquareCap);
                           inst->clip = clip.contains(dev) ? Bounds::huge() : clip.inset(-width, -width);
                           if (det > 1e2f) {
                               SegmentCounter counter;
                               divideGeometry(g, m, inst->clip, false, false, counter);
                               outlineInstances += counter.count;
                           } else
                               outlineInstances += (det < kMinUpperDet ? g->minUpper : g->upperBound(det));
                       } else if (useMolecules) {
                           buffer->_bounds[iz] = *bnds, fasts.base[iz]++;
                           size = g->p16s.end, p16total += size;
                           bool fast = !buffer->useCurves || g->maxCurve * det < 16.f;
                           Blend *inst = new (blends.alloc(1)) Blend(iz | Instance::kMolecule | bool(flags & Scene::kFillEvenOdd) * Instance::kEvenOdd | fast * Instance::kFastEdges);
                           inst->quad.cover = 0;
                           int type = width ? (fast ? Allocator::kFastOutlines : Allocator::kQuadOutlines) : (fast ? Allocator::kFastMolecules : Allocator::kQuadMolecules);
                           cnt = fast ? size / kFastSegments : g->atoms.end;
                           allocator.alloc(clip.lx, clip.ly, clip.ux, clip.uy, blends.end - 1, & inst->quad.cell, type, cnt);
                        } else {
                            bool fast = !buffer->useCurves || g->maxCurve * det < 4.f;
                            CurveIndexer idxr; idxr.clip = clip, idxr.samples = & samples[0] - int(clip.ly * krfh), idxr.fast = fast, idxr.dst = idxr.dst0 = segments.alloc(2 * (det < kMinUpperDet ? g->minUpper : g->upperBound(det)));
                            divideGeometry(g, m, clip, clip.contains(dev), true, idxr);
                            Bounds clu = Bounds(inv.concat(unit));
                            bool opaque = buffer->_colors[iz].a == 255 && !(clu.lx < e0 || clu.ux > e1 || clu.ly < e0 || clu.uy > e1);
                            writeSegmentInstances(clip, flags & Scene::kFillEvenOdd, iz, opaque, fast, *this);
                            segments.idx = segments.end = idxr.dst - segments.base;
                        }
                    }
                }
            }
        }
        void empty() {
            outlinePaths = outlineInstances = p16total = 0, blends.empty(), fasts.empty(), opaques.empty(), segments.empty(), segmentsIndices.empty(), indices.empty();
            for (int i = 0; i < samples.size(); i++)
                samples[i].empty();
            entries = std::vector<Buffer::Entry>();
        }
        void reset() { outlinePaths = outlineInstances = p16total = 0, blends.reset(), fasts.reset(), opaques.reset(), segments.reset(), segmentsIndices.reset(), indices.reset(), samples.resize(0), entries = std::vector<Buffer::Entry>(); }
        size_t outlinePaths = 0, outlineInstances = 0, p16total;
        Allocator allocator;  std::vector<Buffer::Entry> entries;
        Row<uint32_t> fasts;  Row<Blend> blends;  Row<Instance> opaques;  Row<Segment> segments;
        Row<Index> indices;  std::vector<Row<Sample>> samples;  Row<uint32_t> segmentsIndices;
    };
    static void divideGeometry(Geometry *g, Transform m, Bounds clip, bool unclipped, bool polygon, Writer& writer) {
        bool closed, closeSubpath = false;  float *p = g->points.base, sx = FLT_MAX, sy = FLT_MAX, x0 = FLT_MAX, y0 = FLT_MAX, x1, y1, x2, y2, x3, y3, ly, uy, lx, ux;
        for (uint8_t *type = g->types.base, *end = type + g->types.end; type < end; )
            switch (*type) {
                case Geometry::kMove:
                    if ((closed = (polygon || closeSubpath) && (sx != x0 || sy != y0)))
                        line(x0, y0, sx, sy, clip, unclipped, polygon, writer);
                    if (sx != FLT_MAX)
                        writer.EndSubpath(x0, y0, sx, sy, (uint32_t(closeSubpath) << 0) | (uint32_t(closed) << 1));
                    sx = x0 = p[0] * m.a + p[1] * m.c + m.tx, sy = y0 = p[0] * m.b + p[1] * m.d + m.ty, p += 2, type++, closeSubpath = false;
                    break;
                case Geometry::kLine:
                    x1 = p[0] * m.a + p[1] * m.c + m.tx, y1 = p[0] * m.b + p[1] * m.d + m.ty;
                    line(x0, y0, x1, y1, clip, unclipped, polygon, writer);
                    x0 = x1, y0 = y1, p += 2, type++;
                    break;
                case Geometry::kQuadratic:
                    x1 = p[0] * m.a + p[1] * m.c + m.tx, y1 = p[0] * m.b + p[1] * m.d + m.ty;
                    x2 = p[2] * m.a + p[3] * m.c + m.tx, y2 = p[2] * m.b + p[3] * m.d + m.ty;
                    if (unclipped)
                        writer.Quadratic(x0, y0, x1, y1, x2, y2);
                    else {
                        ly = fminf(y0, fminf(y1, y2)), uy = fmaxf(y0, fmaxf(y1, y2));
                        if (ly < clip.uy && uy > clip.ly) {
                            lx = fminf(x0, fminf(x1, x2)), ux = fmaxf(x0, fmaxf(x1, x2));
                            if (polygon || !(ux < clip.lx || lx > clip.ux)) {
                                if (ly < clip.ly || uy > clip.uy || lx < clip.lx || ux > clip.ux)
                                    clipQuadratic(x0, y0, x1, y1, x2, y2, clip, lx, ly, ux, uy, polygon, writer);
                                else
                                    writer.Quadratic(x0, y0, x1, y1, x2, y2);
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
                        writer.Cubic(x0, y0, x1, y1, x2, y2, x3, y3);
                    else {
                        ly = fminf(fminf(y0, y1), fminf(y2, y3)), uy = fmaxf(fmaxf(y0, y1), fmaxf(y2, y3));
                        if (ly < clip.uy && uy > clip.ly) {
                            lx = fminf(fminf(x0, x1), fminf(x2, x3)), ux = fmaxf(fmaxf(x0, x1), fmaxf(x2, x3));
                            if (polygon || !(ux < clip.lx || lx > clip.ux)) {
                                if (ly < clip.ly || uy > clip.uy || lx < clip.lx || ux > clip.ux)
                                    clipCubic(x0, y0, x1, y1, x2, y2, x3, y3, clip, lx, ly, ux, uy, polygon, writer);
                                else
                                    writer.Cubic(x0, y0, x1, y1, x2, y2, x3, y3);
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
            line(x0, y0, sx, sy, clip, unclipped, polygon, writer);
        writer.EndSubpath(x0, y0, sx, sy, (uint32_t(closeSubpath) << 0) | (uint32_t(closed) << 1));
    }
    static inline void line(float x0, float y0, float x1, float y1, Bounds clip, bool unclipped, bool polygon, Writer& writer) {
        if (unclipped)
            writer.writeSegment(x0, y0, x1, y1);
        else {
            float ly = fminf(y0, y1), uy = fmaxf(y0, y1);
            if (ly < clip.uy && uy > clip.ly) {
                if (ly < clip.ly || uy > clip.uy || fminf(x0, x1) < clip.lx || fmaxf(x0, x1) > clip.ux)
                    clipLine(x0, y0, x1, y1, clip, polygon, writer);
                else
                    writer.writeSegment(x0, y0, x1, y1);
            }
        }
    }
    static void clipLine(float x0, float y0, float x1, float y1, Bounds clip, bool polygon, Writer& writer) {
        float lx, ux, ly, uy, roots[6], *root = roots, *r, s, t, sx0, sy0, sx1, sy1, mx, my, vx;
        lx = fminf(x0, x1), ux = fmaxf(x0, x1), ly = fminf(y0, y1), uy = fmaxf(y0, y1);
        *root++ = 0.f;
        if (clip.ly > ly && clip.ly < uy)
            *root++ = (clip.ly - y0) / (y1 - y0);
        if (clip.uy > ly && clip.uy < uy)
            *root++ = (clip.uy - y0) / (y1 - y0);
        if (clip.lx > lx && clip.lx < ux)
            *root++ = (clip.lx - x0) / (x1 - x0);
        if (clip.ux > lx && clip.ux < ux)
            *root++ = (clip.ux - x0) / (x1 - x0);
        std::sort(roots + 1, root), *root = 1.f;
        for (sx0 = x0, sy0 = y0, r = roots; r < root; r++, sx0 = sx1, sy0 = sy1) {
            t = r[1], s = 1.f - t;
            sx1 = s * x0 + t * x1, mx = 0.5f * (sx0 + sx1);
            sy1 = s * y0 + t * y1, my = 0.5f * (sy0 + sy1);
            if (my >= clip.ly && my < clip.uy) {
                if (mx >= clip.lx && mx < clip.ux)
                    writer.writeSegment(sx0, sy0, sx1, sy1);
                else if (polygon)
                    vx = mx <= clip.lx ? clip.lx : clip.ux, writer.writeSegment(vx, sy0, vx, sy1);
            }
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
    static void clipQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, Bounds clip, float lx, float ly, float ux, float uy, bool polygon, Writer& writer) {
        float ax, bx, ay, by, roots[10], *root = roots, *r, s, w0, w1, w2, mt, mx, my, vx, sx0, sy0, sx2, sy2;
        ax = x2 - x1, bx = x1 - x0, ax -= bx, bx *= 2.f, ay = y2 - y1, by = y1 - y0, ay -= by, by *= 2.f;
        *root++ = 0.f;
        if (clip.ly > ly && clip.ly < uy)
            root = solveQuadratic(ay, by, y0 - clip.ly, root);
        if (clip.uy > ly && clip.uy < uy)
            root = solveQuadratic(ay, by, y0 - clip.uy, root);
        if (clip.lx > lx && clip.lx < ux)
            root = solveQuadratic(ax, bx, x0 - clip.lx, root);
        if (clip.ux > lx && clip.ux < ux)
            root = solveQuadratic(ax, bx, x0 - clip.ux, root);
        if (root - roots == 1) {
            if (fmaxf(y0, y2) > clip.ly && fminf(y0, y2) < clip.uy) {
                if (fmaxf(x0, x2) > clip.lx && fminf(x0, x2) < clip.ux)
                    writer.Quadratic(x0, y0, x1, y1, x2, y2);
                else if (polygon)
                    vx = lx <= clip.lx ? clip.lx : clip.ux, writer.writeSegment(vx, y0, vx, y2);
            }
        } else {
            std::sort(roots + 1, root), *root = 1.f;
            for (sx0 = x0, sy0 = y0, r = roots; r < root; r++, sx0 = sx2, sy0 = sy2) {
                s = 1.f - r[1], w0 = s * s, w1 = 2.f * s * r[1], w2 = r[1] * r[1];
                sx2 = w0 * x0 + w1 * x1 + w2 * x2;
                sy2 = w0 * y0 + w1 * y1 + w2 * y2;
                mt = 0.5f * (r[0] + r[1]), mx = (ax * mt + bx) * mt + x0, my = (ay * mt + by) * mt + y0;
                if (my >= clip.ly && my < clip.uy) {
                    if (mx >= clip.lx && mx < clip.ux)
                        writer.Quadratic(sx0, sy0, 2.f * mx - 0.5f * (sx0 + sx2), 2.f * my - 0.5f * (sy0 + sy2), sx2, sy2);
                    else if (polygon)
                        vx = mx <= clip.lx ? clip.lx : clip.ux, writer.writeSegment(vx, sy0, vx, sy2);
                }
            }
        }
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
    static void clipCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Bounds clip, float lx, float ly, float ux, float uy, bool polygon, Writer& writer) {
        float cx, bx, ax, cy, by, ay, roots[14], *root = roots, *r, t, s, w0, w1, w2, w3, mt, mx, my, vx, x0t, y0t, x1t, y1t, x2t, y2t, x3t, y3t, fx, gx, fy, gy;
        cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1), ax = x3 - x0 - bx, bx -= cx;
        cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1), ay = y3 - y0 - by, by -= cy;
        *root++ = 0.f;
        if (clip.ly > ly && clip.ly < uy)
            root = solveCubic(by, cy, y0 - clip.ly, ay, root);
        if (clip.uy > ly && clip.uy < uy)
            root = solveCubic(by, cy, y0 - clip.uy, ay, root);
        if (clip.lx > lx && clip.lx < ux)
            root = solveCubic(bx, cx, x0 - clip.lx, ax, root);
        if (clip.ux > lx && clip.ux < ux)
            root = solveCubic(bx, cx, x0 - clip.ux, ax, root);
        if (root - roots == 1) {
            if (fmaxf(y0, y3) > clip.ly && fminf(y0, y3) < clip.uy) {
                if (fmaxf(x0, x3) > clip.lx && fminf(x0, x3) < clip.ux)
                    writer.Cubic(x0, y0, x1, y1, x2, y2, x3, y3);
                else if (polygon)
                    vx = lx <= clip.lx ? clip.lx : clip.ux, writer.writeSegment(vx, y0, vx, y3);
            }
        } else {
            std::sort(roots + 1, root), *root = 1.f;
            for (x0t = x0, y0t = y0, r = roots; r < root; r++, x0t = x3t, y0t = y3t) {
                t = r[1], s = 1.f - t;
                w0 = s * s, w1 = 3.f * w0 * t, w3 = t * t, w2 = 3.f * s * w3, w0 *= s, w3 *= t;
                x3t = w0 * x0 + w1 * x1 + w2 * x2 + w3 * x3;
                y3t = w0 * y0 + w1 * y1 + w2 * y2 + w3 * y3;
                mt = 0.5f * (r[0] + r[1]), mx = ((ax * mt + bx) * mt + cx) * mt + x0, my = ((ay * mt + by) * mt + cy) * mt + y0;
                if (my >= clip.ly && my < clip.uy) {
                    if (mx >= clip.lx && mx < clip.ux) {
                        const float u = 1.f / 3.f, v = 2.f / 3.f, u3 = 1.f / 27.f, v3 = 8.f / 27.f;
                        mt = v * r[0] + u * r[1], x1t = ((ax * mt + bx) * mt + cx) * mt + x0, y1t = ((ay * mt + by) * mt + cy) * mt + y0;
                        mt = u * r[0] + v * r[1], x2t = ((ax * mt + bx) * mt + cx) * mt + x0, y2t = ((ay * mt + by) * mt + cy) * mt + y0;
                        fx = x1t - v3 * x0t - u3 * x3t, gx = x2t - u3 * x0t - v3 * x3t;
                        fy = y1t - v3 * y0t - u3 * y3t, gy = y2t - u3 * y0t - v3 * y3t;
                        writer.Cubic(
                            x0t, y0t,
                            3.f * fx - 1.5f * gx, 3.f * fy - 1.5f * gy,
                            3.f * gx - 1.5f * fx, 3.f * gy - 1.5f * fy,
                            x3t, y3t
                        );
                    } else if (polygon)
                        vx = mx <= clip.lx ? clip.lx : clip.ux, writer.writeSegment(vx, y0t, vx, y3t);
                }
            }
        }
    }
    struct CurveIndexer: Writer {
        Segment *dst, *dst0;  bool fast;  Bounds clip;  Row<Sample> *samples;
        
        void writeSegment(float x0, float y0, float x1, float y1) {
            if (y0 != y1)
                writeLine(x0, y0, x1, y1);
        }
        void Quadratic(float x0, float y0, float x1, float y1, float x2, float y2) {
            float ax, ay, bx, by, itx, ity, s, t, sx0, sy0, sx1, sy1, sx2, sy2;
            ax = x2 - x1, bx = x1 - x0;
            ay = y2 - y1, by = y1 - y0;
            if (fast) {
                writeLine(x0, y0, x2, y2);
            } else if (ax * bx >= 0.f && ay * by >= 0.f)
                writeQuadratic(x0, y0, x1, y1, x2, y2);
            else {
                itx = fmaxf(0.f, fminf(1.f, bx / (bx - ax))), ity = fmaxf(0.f, fminf(1.f, by / (by - ay)));
                float roots[4] = { 0.f, fminf(itx, ity), fmaxf(itx, ity), 1.f }, *r = roots;
                sx0 = x0, sy0 = y0;
                for (int i = 0; i < 3; i++, r++) {
                    if (r[0] != r[1]) {
                        t = r[1], s = 1.f - t;
                        sx1 = (s * x0 + t * x1), sx2 = s * sx1 + t * (s * x1 + t * x2);
                        sy1 = (s * y0 + t * y1), sy2 = s * sy1 + t * (s * y1 + t * y2);
                        t = r[0] / r[1], s = 1.f - t;
                        sx1 = s * sx1 + t * sx2, sy1 = s * sy1 + t * sy2;
                        writeQuadratic(sx0, sy0, sx1, sy1, sx2, sy2);
                        sx0 = sx2, sy0 = sy2;
                    }
                }
            }
        }
        __attribute__((always_inline)) void writeLine(float x0, float y0, float x1, float y1) {
            y0 = fmaxf(clip.ly, fminf(clip.uy, y0));
            y1 = fmaxf(clip.ly, fminf(clip.uy, y1));
            if ((uint32_t(y0) & kFatMask) == (uint32_t(y1) & kFatMask))
                new (samples[int(y0 * krfh)].alloc(1)) Sample(fminf(x0, x1), fmaxf(x0, x1), (y1 - y0) * kCoverScale, dst - dst0);
            else {
                float ly, uy, ily, iuy, iy, ny, t, lx, ux, scale = copysignf(kCoverScale, y1 - y0);
                ly = fminf(y0, y1), ily = floorf(ly * krfh);
                uy = fmaxf(y0, y1), iuy = ceilf(uy * krfh);
                for (lx = y0 < y1 ? x0 : x1, iy = ily; iy < iuy; iy++, lx = ux, ly = ny) {
                    ny = fminf(uy, (iy + 1.f) * kfh), t = (ny - y0) / (y1 - y0), ux = (1.f - t) * x0 + t * x1;
                    new (samples[int(iy)].alloc(1)) Sample(fminf(lx, ux), fmaxf(lx, ux), (ny - ly) * scale, dst - dst0);
                }
            }
            new (dst++) Segment(x0, y0, x1, y1, 0);
        }
        __attribute__((always_inline)) void writeQuadratic(float x0, float y0, float x1, float y1, float x2, float y2) {
            y0 = fmaxf(clip.ly, fminf(clip.uy, y0));
            y1 = fmaxf(clip.ly, fminf(clip.uy, y1));
            y2 = fmaxf(clip.ly, fminf(clip.uy, y2));
            if ((uint32_t(y0) & kFatMask) == (uint32_t(y2) & kFatMask))
                new (samples[int(y0 * krfh)].alloc(1)) Sample(fminf(x0, x2), fmaxf(x0, x2), (y2 - y0) * kCoverScale, dst - dst0);
            else {
                float ay, by, ax, bx, ly, uy, lx, ux, d2a, ity, iy, t, ny, sign = copysignf(1.f, y2 - y0);
                ax = x2 - x1, bx = x1 - x0, ax -= bx, bx *= 2.f;
                ay = y2 - y1, by = y1 - y0, ay -= by, by *= 2.f;
                d2a = 0.5f / ay, ity = -by * d2a, d2a *= sign, sign *= kCoverScale;
                lx = y0 < y2 ? x0 : x2, ly = fminf(y0, y2), uy = fmaxf(y0, y2);
                for (iy = floorf(ly * krfh); ly < uy; ly = ny, iy++, lx = ux) {
                    ny = fminf(uy, (iy + 1.f) * kfh);
                    t = ay == 0 ? -(y0 - ny) / by : ity + sqrtf(fmaxf(0.f, by * by - 4.f * ay * (y0 - ny))) * d2a;
                    t = fmaxf(0.f, fminf(1.f, t)), ux = (ax * t + bx) * t + x0;
                    new (samples[int(iy)].alloc(1)) Sample(fminf(lx, ux), fmaxf(lx, ux), (ny - ly) * sign, dst - dst0);
                }
            }
            new (dst++) Segment(x0, y0, x1, y1, 1), new (dst++) Segment(x1, y1, x2, y2, 2);
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
        Row<Index> *indices = & ctx.indices;  Index *idx;
        Row<Sample> *samples = & ctx.samples[0];  Sample *sample;
        
        for (iy = ily; iy < iuy; iy++, samples->empty(), samples++, indices->empty()) {
            if ((size = samples->end)) {
                for (sample = samples->base, idx = indices->alloc(size), i = 0; i < size; i++, sample++) {
                    if (sample->cover)
                        idx->x = sample->lx, idx->i = i, idx++;
                }
                size = idx - indices->base;
                if (size > 32 && size < 65536)
                    radixSort((uint32_t *)indices->base, int(size), single ? clip.lx : 0, range, single, counts);
                else
                    std::sort(indices->base, indices->base + size);
                
                size_t siBase = ctx.segmentsIndices.end;
                uint32_t *si = ctx.segmentsIndices.alloc(size);
                
                ly = iy * kfh, ly = ly < clip.ly ? clip.ly : ly > clip.uy ? clip.uy : ly;
                uy = (iy + 1) * kfh, uy = uy < clip.ly ? clip.ly : uy > clip.uy ? clip.uy : uy;
                for (h = uy - ly, wscale = 0.00003051850948f * kfh / h, cover = winding = 0.f, index = indices->base, lx = ux = index->x, i = begin = 0; i < size; i++, index++) {
                    if (index->x >= ux && fabsf((winding - floorf(winding)) - 0.5f) > 0.499f) {
                        if (lx != ux) {
                            Blend *inst = new (ctx.blends.alloc(1)) Blend(edgeIz);
                            ctx.allocator.alloc(lx, ly, ux, uy, ctx.blends.end - 1, & inst->quad.cell, type, (i - begin + 1) / 2);
                            inst->quad.cover = short(cover), inst->quad.base = int(ctx.segments.idx), inst->data.count = int(i - begin), inst->data.idx = int(siBase + begin);
                        }
                        winding = cover = truncf(winding + copysign(0.5f, winding));
                        if ((even && (int(winding) & 1)) || (!even && winding)) {
                            if (opaque) {
                                Cell *cell = & (new (ctx.opaques.alloc(1)) Instance(iz))->quad.cell;
                                cell->lx = ux, cell->ly = ly, cell->ux = index->x, cell->uy = uy;
                            } else {
                                Cell *cell = & (new (ctx.blends.alloc(1)) Blend(iz))->quad.cell;
                                cell->lx = ux, cell->ly = ly, cell->ux = index->x, cell->uy = uy, cell->ox = kNullIndex;
                            }
                        }
                        begin = i, lx = ux = index->x;
                    }
                    sample = samples->base + index->i;
                    ux = sample->ux > ux ? sample->ux : ux, winding += sample->cover * wscale;
                    si[i] = sample->is;
                }
                if (lx != ux) {
                    Blend *inst = new (ctx.blends.alloc(1)) Blend(edgeIz);
                    ctx.allocator.alloc(lx, ly, ux, uy, ctx.blends.end - 1, & inst->quad.cell, type, (i - begin + 1) / 2);
                    inst->quad.cover = short(cover), inst->quad.base = int(ctx.segments.idx), inst->data.count = int(i - begin),
                    inst->data.idx = int(siBase + begin);
                }
            }
        }
    }
    struct SegmentCounter: Writer {
        void writeSegment(float x0, float y0, float x1, float y1) { count += 1; }
        void Quadratic(float x0, float y0, float x1, float y1, float x2, float y2) { count += 2; }
        size_t count = 0;
    };
    
    struct Outliner: Writer {
        void writeSegment(float x0, float y0, float x1, float y1) {
            writeQuadratic(x0, y0, FLT_MAX, FLT_MAX, x1, y1);
        }
        void Quadratic(float x0, float y0, float x1, float y1, float x2, float y2) {
            float ax, bx, ay, by, adot, bdot, cosine, ratio, a, b, t, s, tx0, tx1, x, ty0, ty1, y;
            ax = x2 - x1, bx = x1 - x0, ay = y2 - y1, by = y1 - y0;
            adot = ax * ax + ay * ay, bdot = bx * bx + by * by, cosine = (ax * bx + ay * by) / sqrt(adot * bdot + 1e-12f), ratio = adot / bdot;
            if (cosine > 0.7071f)
                writeQuadratic(x0, y0, ratio < 1e-2f || ratio > 1e2f ? FLT_MAX : x1, y1, x2, y2);
            else {
                a = sqrtf(adot), b = sqrtf(bdot), t = b / (a + b), s = 1.0f - t;
                tx0 = s * x0 + t * x1, tx1 = s * x1 + t * x2, x = s * tx0 + t * tx1;
                ty0 = s * y0 + t * y1, ty1 = s * y1 + t * y2, y = s * ty0 + t * ty1;
                writeQuadratic(x0, y0, tx0, ty0, x, y);
                writeQuadratic(x, y, tx1, ty1, x2, y2);
            }
        }
        void EndSubpath(float x0, float y0, float x1, float y1, uint32_t curve) {
            if (dst - dst0 > 0) {
                Instance *first = dst0, *last = dst - 1;  dst0 = dst;
                first->outline.prev = int(bool(curve & 3)) * int(last - first), last->outline.next = -first->outline.prev;
            }
        }
        inline void writeQuadratic(float x0, float y0, float x1, float y1, float x2, float y2) {
            Outline& o = dst->outline;
            dst->iz = iz, o.s.x0 = x0, o.s.y0 = y0, o.s.x1 = x2, o.s.y1 = y2, o.cx = x1, o.cy = y1, o.prev = -1, o.next = 1, dst++;
        }
        uint32_t iz;  Instance *dst0, *dst; float px0, py0;
    };
    static size_t writeContextsToBuffer(SceneList& list, Context *contexts, size_t count, size_t *begins, Buffer& buffer) {
        size_t size = buffer.headerSize, begin = buffer.headerSize, end = begin, sz, i, j, instances;
        for (i = 0; i < count; i++)
            size += contexts[i].opaques.end * sizeof(Instance);
        Context *ctx = contexts;   Allocator::Pass *pass;
        for (ctx = contexts, i = 0; i < count; i++, ctx++) {
            for (instances = 0, pass = ctx->allocator.passes.base, j = 0; j < ctx->allocator.passes.end; j++, pass++)
                instances += pass->count();
            begins[i] = size, size += instances * sizeof(Edge) + (ctx->outlineInstances - ctx->outlinePaths + ctx->blends.end) * sizeof(Instance) + ctx->segments.end * sizeof(Segment) + ctx->p16total * sizeof(Point16);
        }
        buffer.resize(size, buffer.headerSize);
        for (i = 0; i < count; i++)
            if ((sz = contexts[i].opaques.end * sizeof(Instance)))
                memcpy(buffer.base + end, contexts[i].opaques.base, sz), end += sz;
        if (begin != end)
            new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::kOpaques, begin, end);
        return size;
    }
    static void writeContextToBuffer(SceneList& list, Context *ctx, size_t begin, Buffer& buffer) {
        size_t i, j, count, size, ip, iz, is, lz, ic, end, pbase = 0, instbegin, passsize;
        if (ctx->segments.end || ctx->p16total) {
            size = ctx->segments.end * sizeof(Segment), end = begin + size;
            ctx->entries.emplace_back(Buffer::kSegmentsBase, begin, end);
            memcpy(buffer.base + begin, ctx->segments.base, size), begin = end;
            
            for (pbase = 0, i = lz = 0; i < list.scenes.size(); lz += list.scenes[i].count, i++)
                for (count = list.scenes[i].count, is = 0; is < count; is++)
                    if (ctx->fasts.base[lz + is]) {
                        Geometry *g = list.scenes[i].paths->base[is].ptr;
                        size = g->p16s.end * sizeof(Point16);
                        memcpy(buffer.base + end, g->p16s.base, size);
                        end += size, ctx->fasts.base[lz + is] = uint32_t(pbase), pbase += g->p16s.end;
                    }
            ctx->entries.emplace_back(Buffer::kPointsBase, begin, end), begin = end;
        }
        Transform *ctms = (Transform *)(buffer.base + buffer.ctms);
        float *widths = (float *)(buffer.base + buffer.widths);
        uint32_t *idxs = (uint32_t *)(buffer.base + buffer.idxs);
        Edge *quadEdge = nullptr, *fastEdge = nullptr, *fastOutline = nullptr, *fastOutline0 = nullptr, *quadOutline = nullptr, *quadOutline0 = nullptr, *fastMolecule = nullptr, *fastMolecule0 = nullptr, *quadMolecule = nullptr, *quadMolecule0 = nullptr;
        for (count = ctx->allocator.passes.end, ip = 0; ip < count; ip++) {
            Allocator::Pass *pass = ctx->allocator.passes.base + ip;
            passsize = (ip + 1 < count ? (pass + 1)->idx : ctx->blends.end) - pass->idx;
            instbegin = begin + pass->count() * sizeof(Edge);
            if (pass->count()) {
                ctx->entries.emplace_back(Buffer::kInstancesBase, instbegin, 0);
                
                quadEdge = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kQuadEdges] * sizeof(Edge);
                ctx->entries.emplace_back(Buffer::kQuadEdges, begin, end), begin = end;
                
                fastEdge = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kFastEdges] * sizeof(Edge);
                ctx->entries.emplace_back(Buffer::kFastEdges, begin, end), begin = end;
                
                fastOutline0 = fastOutline = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kFastOutlines] * sizeof(Edge);
                ctx->entries.emplace_back(Buffer::kFastOutlines, begin, end), begin = end;
                
                quadOutline0 = quadOutline = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kQuadOutlines] * sizeof(Edge);
                ctx->entries.emplace_back(Buffer::kQuadOutlines, begin, end), begin = end;
                
                fastMolecule0 = fastMolecule = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kFastMolecules] * sizeof(Edge);
                ctx->entries.emplace_back(Buffer::kFastMolecules, begin, end), begin = end;
                
                quadMolecule0 = quadMolecule = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kQuadMolecules] * sizeof(Edge);
                ctx->entries.emplace_back(Buffer::kQuadMolecules, begin, end), begin = end;
                assert(begin == instbegin);
            }
            
            Instance *dst0 = (Instance *)(buffer.base + begin), *dst = dst0;  Outliner outliner;
            for (Blend *inst = ctx->blends.base + pass->idx, *endinst = inst + passsize; inst < endinst; inst++) {
                iz = inst->iz & kPathIndexMask, is = idxs[iz] & 0xFFFFF, i = idxs[iz] >> 20;
                Geometry *g = list.scenes[i].paths->base[is].ptr;
                if (inst->iz & Instance::kOutlines) {
                    outliner.iz = inst->iz, outliner.dst = outliner.dst0 = dst;
                    divideGeometry(g, ctms[iz], inst->clip, inst->clip.isHuge(), false, outliner);
                    dst = outliner.dst;
                } else {
                    ic = dst - dst0, dst->iz = inst->iz, dst->quad = inst->quad, dst++;
                    if (inst->iz & Instance::kMolecule) {
                        Atom *atom = g->atoms.base;  uint32_t ia;
                        bool hasMolecules = g->molecules.end > 1;
                        dst[-1].quad.base = int(ctx->fasts.base[iz]);
                        if (widths[iz]) {
                            bool fast = inst->iz & Instance::kFastEdges;
                            Edge *outline = fast ? fastOutline : quadOutline;  uint8_t *p16cnt = g->p16cnts.base;
                            dst[-1].quad.biid = int(outline - (fast ? fastOutline0 : quadOutline0));
                            if (fast) {
                                for (j = 0, size = g->p16s.end / kFastSegments; j < size; j++, outline++)
                                    outline->ic = uint32_t(ic | (uint32_t(*p16cnt++ & 0xF) << 22));
                            } else {
                                for (j = 0, size = g->atoms.end; j < size; j++, atom++) {
                                    ia = atom->i & Atom::kMask, outline->ic = uint32_t(ic) | bool(atom->i & Atom::isClose) * Edge::isClose | ((ia & 0x000F0000) << 10), outline->i0 = ia & 0xFFFF, outline++;
                                }
                            }
                            *(fast ? & fastOutline : & quadOutline) = outline;
                        } else {
                            uint16_t ux = inst->quad.cell.ux;  Transform& ctm = ctms[iz];
                            float *molx = (float *)g->molecules.base + (ctm.a > 0.f ? 2 : 0), *moly = (float *)g->molecules.base + (ctm.c > 0.f ? 3 : 1);
                            bool update = hasMolecules, fast = inst->iz & Instance::kFastEdges;  uint8_t *p16cnt = g->p16cnts.base;
                            Edge *molecule = fast ? fastMolecule : quadMolecule;
                            dst[-1].quad.biid = int(molecule - (fast ? fastMolecule0 : quadMolecule0));

                            if (fast) {
                                for (j = 0, size = g->p16s.end / kFastSegments; j < size; j++, update = hasMolecules && (*p16cnt & 0x80), p16cnt++) {
                                    if (update)
                                        ux = ceilf(*molx * ctm.a + *moly * ctm.c + ctm.tx), molx += 4, moly += 4;
                                    molecule->ic = uint32_t(ic | (uint32_t(*p16cnt & 0xF) << 22)), molecule->ux = ux, molecule++;
                                }
                            } else {
                                for (j = 0, size = g->atoms.end; j < size; j++, update = hasMolecules && (atom->i & Atom::isEnd), atom++) {
                                    if (update)
                                        ux = ceilf(*molx * ctm.a + *moly * ctm.c + ctm.tx), molx += 4, moly += 4;
                                    ia = atom->i & Atom::kMask, molecule->ic = uint32_t(ic) | ((ia & 0x000F0000) << 10), molecule->i0 = ia & 0xFFFF, molecule->ux = ux, molecule++;
                                }
                            }
                            *(fast ? & fastMolecule : & quadMolecule) = molecule;
                        }
                    } else if (inst->iz & Instance::kEdge) {
                        Edge *edge = inst->iz & Instance::kFastEdges ? fastEdge : quadEdge;
                        uint32_t is0, is1, *si = ctx->segmentsIndices.base + inst->data.idx;
                        for (j = 0; j < inst->data.count; j++, edge++) {
                            is0 = si[j];
                            is1 = ++j < inst->data.count ? si[j] : ~0;
                            edge->ic = uint32_t(ic) | ((is0 << 10) & Edge::ue0) | ((is1 << 6) & Edge::ue1);
                            edge->i0 = is0 & 0xFFFF, edge->ux = is1 & 0xFFFF;
                        }
                        *(inst->iz & Instance::kFastEdges ? & fastEdge : & quadEdge) = edge;
                    }
                }
            }
            if ((size = dst - dst0))
                end = begin + size * sizeof(Instance), ctx->entries.emplace_back(Buffer::kInstances, begin, end), begin = end;
        }
    }
};
typedef Rasterizer Ra;
