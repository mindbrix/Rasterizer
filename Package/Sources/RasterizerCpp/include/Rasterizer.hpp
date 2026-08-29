//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//
//  This software is provided 'as-is', without any express or implied
//  warranty. In no event will the authors be held liable for any damages
//  arising from the use of this software.
//
//  Permission is granted to anyone to use this software for personal use
//  (for a commercial licence please contact the author), and to alter it and
//  redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
//

#import "Rasterizer.h"
#import "xxhash.h"
#import <map>
#import <vector>
#pragma clang diagnostic ignored "-Wcomma"

struct Rasterizer {
    template<typename T>
    struct Ref {
        Ref() {
            ptr = new T(), ptr->refCount = 1;
        }
        ~Ref() {
            if (ptr && --(ptr->refCount) == 0)
                delete ptr;
        }
        Ref(T* src) {
            ptr = src;
            if (ptr)
                ptr->refCount = 1;
        }
        Ref(const Ref& other) {
            *this = other;
        }
        Ref& operator= (const Ref& other)   {
            if (this != & other) {
                if (ptr)
                    this->~Ref();
                ptr = other.ptr;
                if (ptr)
                    ptr->refCount++;
            }
            return *this;
        }
        inline T* operator->() const {
            return ptr;
        }
        T *ptr = nullptr;
    };
    template<typename T, bool isRef = false>
    struct Memory {
        ~Memory() {
            if (addr) {
                if (isRef)
                    for (size_t i = 0; i < end; i++)
                        addr[i].~T();
                free(addr);
            }
        }
        inline T *alloc(size_t n) {
            size_t oldEnd = end;
            end += n;
            if (size < end)
                resize(end * 1.5);
            return addr + oldEnd;
        }
        T *resize(size_t n) {
            size_t oldSize = size;
            if (isRef)
                for (size_t i = n; i < end; i++)
                    addr[i].~T();
            size = n, end = end < n ? end : n;
            addr = (T *)realloc(addr, n * sizeof(T));
            if (isRef && size > oldSize)
                bzero((void *)(addr + oldSize), (size - oldSize) * sizeof(T));
            return addr;
        }
        size_t refCount, size = 0, end = 0;  T *addr = nullptr;
    };
    template<typename T, bool isRef = false>
    struct Vector {
        Vector(size_t size = 0) {
            if (size)
                resize(size);
        }
        inline void add(T obj) {
            *memory->alloc(1) = obj;
        }
        inline void add(T *objs, size_t n) {
            T *dst = memory->alloc(n);
            if (!isRef)
                memcpy(dst, objs, n * sizeof(T));
            else {
                for (size_t i = 0; i < n; i++)
                    *dst++ = *objs++;
            }
        }
        inline T *resize(size_t n) {
            return memory->resize(n);
        }
        inline size_t end() const {
            return memory->end;
        }
        inline T& operator[](size_t i) const {
            return memory->addr[i];
        }
        inline bool operator== (const Vector& other) const {
            return memory.ptr == other.memory.ptr;
        }
        inline T& back() const {
            return memory->addr[memory->end - 1];
        }
        Ref<Memory<T, isRef>> memory;
    };
    template<typename T>
    struct RefVector: Vector<T, true> {};
    
    template<typename T>
    struct Row {
        inline T *alloc(size_t n) {
            size_t begin = end;
            end += n;
            if (memory->size < end)
                base = memory->resize(end * 1.5);
            return base + begin;
        }
        inline T *prealloc(size_t n) {
            size_t begin = end;
            alloc(n), end = begin;
            return base + begin;
        }
        inline T& back() const { return base[end - 1]; }
        Row<T>& empty() { end = idx = 0; return *this; }
        void reset() { end = idx = 0, base = nullptr, memory = Ref<Memory<T>>(); }
        
        T *base = nullptr;  Ref<Memory<T>> memory;  size_t end = 0, idx = 0;
    };
    
    struct Transform {
        Transform() : a(1.f), b(0.f), c(0.f), d(1.f), tx(0.f), ty(0.f) {}
        Transform(float a, float b, float c, float d, float tx, float ty) : a(a), b(b), c(c), d(d), tx(tx), ty(ty) {}
        inline Transform concat(const Transform t) const {
            return {
                a * t.a + b * t.c, a * t.b + b * t.d,
                c * t.a + d * t.c, c * t.b + d * t.d,
                tx * t.a + ty * t.c + t.tx, tx * t.b + ty * t.d + t.ty
            };
        }
        inline Transform concatAroundCenter(const Transform t, float cx, float cy) const {
            return Transform(a, b, c, d, tx - cx, ty - cy).concat(Transform(t.a, t.b, t.c, t.d, t.tx + cx, t.ty + cy));
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
        inline float width() const {
            return ux - lx;
        }
        inline float height() const {
            return uy - ly;
        }
        inline float cx() const {
            return 0.5f * (lx + ux);
        }
        inline float cy() const {
            return 0.5f * (ly + uy);
        }
        inline bool contains(const Bounds b) const {
            return lx <= b.lx && ux >= b.ux && ly <= b.ly && uy >= b.uy;
        }
        inline void extend(float x, float y) {
            lx = fminf(lx, x), ly = fminf(ly, y), ux = fmaxf(ux, x), uy = fmaxf(uy, y);
        }
        inline void extend(const Bounds b) {
            lx = fminf(lx, b.lx), ly = fminf(ly, b.ly), ux = fmaxf(ux, b.ux), uy = fmaxf(uy, b.uy);
        }
        inline Bounds inset(float dx, float dy) const {
            bool valid = dx * 2.f < ux - lx && dy * 2.f < uy - ly;
            return valid ? Bounds(lx + dx, ly + dy, ux - dx, uy - dy) : *this;
        }
        inline Bounds integral() const {
            return { floorf(lx), floorf(ly), ceilf(ux), ceilf(uy) };
        }
        inline Bounds intersect(const Bounds b) const {
            return {
                fmaxf(b.lx, fminf(b.ux, lx)), fmaxf(b.ly, fminf(b.uy, ly)),
                fmaxf(b.lx, fminf(b.ux, ux)), fmaxf(b.ly, fminf(b.uy, uy))
            };
        }
        inline bool isHuge() const {
            return lx == -5e11f;
        }
        inline bool isNull() const {
            return lx == FLT_MAX && ly == FLT_MAX;
        }
        inline bool isRect() const {
            return ux > lx && uy > ly;
        }
        inline bool isZero() const {
            return lx == ux && ly == uy;
        }
        inline Bounds(const Transform quad) :
            lx(quad.tx + fminf(0.f, quad.a) + fminf(0.f, quad.c)),
            ly(quad.ty + fminf(0.f, quad.b) + fminf(0.f, quad.d)),
            ux(quad.tx + fmaxf(0.f, quad.a) + fmaxf(0.f, quad.c)),
            uy(quad.ty + fmaxf(0.f, quad.b) + fmaxf(0.f, quad.d)) {
        }
        inline Transform quad(const Transform t) const {
            float w = width(), h = height();
            return {
                t.a * w, t.b * w,
                t.c * h, t.d * h,
                lx * t.a + ly * t.c + t.tx,
                lx * t.b + ly * t.d + t.ty
            };
        }
        inline Transform fitTransform(const Bounds b) const {
            if (isNull() || !isRect() || b.isNull() || !b.isRect())
                return Transform();
            float w = width(), h = height(), bw = b.width(), bh = b.height(), s = fminf(w / bw, h / bh);
            return {
                s, 0.f,
                0.f, s,
                lx + 0.5f * (w - s * bw) - s * b.lx,
                ly + 0.5f * (h - s * bh) - s * b.ly
            };
        }
        float lx, ly, ux, uy;
    };
    
    
    struct Atom {
        enum Flags { isMoveTo = 1 << 31, kMask = ~isMoveTo };
        uint32_t i;
    };
    
    struct Point16 {
        enum Flags { kIsCurve = 1 << 15, kMask = ~kIsCurve };
        inline Point16(float x0, float y0, bool isCurve = false)
                : x(uint16_t(fmaxf(0.f, fminf(kMoleculesRange, x0))) | (isCurve ? kIsCurve : 0)),
                  y(fmaxf(0.f, fminf(kMoleculesRange, y0))) {}
        uint16_t x, y;
    };
    
    struct Geometry {
        enum Type { kMove, kLine, kQuadratic, kCubic, kClose, kCountSize };
        const size_t TypeSizes[kCountSize] = { 1, 1, 2, 3, 1 };
        
        void prealloc(size_t count) {
            points.prealloc(2 * count), types.prealloc(count);
        }
        void addBounds(Bounds b) {
            if (b.ux > b.lx && b.uy > b.ly)
                moveTo(b.lx, b.ly), lineTo(b.ux, b.ly), lineTo(b.ux, b.uy), lineTo(b.lx, b.uy), lineTo(b.lx, b.ly), close();
        }
        void addEllipse(Bounds b) {
            if (b.ux > b.lx && b.uy > b.ly) {
                const float t = 0.5f - 2.f / 3.f * (M_SQRT2 - 1.f), s = 1.f - t, mx = 0.5f * (b.lx + b.ux), my = 0.5f * (b.ly + b.uy);
                moveTo(b.ux, my);
                cubicTo(b.ux, t * b.ly + s * b.uy, t * b.lx + s * b.ux, b.uy, mx, b.uy);
                cubicTo(s * b.lx + t * b.ux, b.uy, b.lx, t * b.ly + s * b.uy, b.lx, my);
                cubicTo(b.lx, s * b.ly + t * b.uy, s * b.lx + t * b.ux, b.ly, mx, b.ly);
                cubicTo(t * b.lx + s * b.ux, b.ly, b.ux, s * b.ly + t * b.uy, b.ux, my);
                close();
            }
        }
        void addRoundedRect(Bounds b, float width, float height) {
            if (b.ux > b.lx && b.uy > b.ly && width > 0.f && height > 0.f) {
                const float t = 1.f - 4.f / 3.f * (M_SQRT2 - 1.f);
                const float tx = fminf(0.5f, width / (b.ux - b.lx)), sx = 1.f - tx, tw = t * tx, sw = 1.f - tw;
                const float ty = fminf(0.5f, height / (b.uy - b.ly)), sy = 1.f - ty, th = t * ty, sh = 1.f - th;
                moveTo(b.lx, sy * b.ly + ty * b.uy);
                lineTo(b.lx, ty * b.ly + sy * b.uy);
                cubicTo(b.lx, th * b.ly + sh * b.uy, sw * b.lx + tw * b.ux, b.uy, sx * b.lx + tx * b.ux, b.uy);
                lineTo(tx * b.lx + sx * b.ux, b.uy);
                cubicTo(tw * b.lx + sw * b.ux, b.uy, b.ux, th * b.ly + sh * b.uy, b.ux, ty * b.ly + sy * b.uy);
                lineTo(b.ux, sy * b.ly + ty * b.uy);
                cubicTo(b.ux, sh * b.ly + th * b.uy, tw * b.lx + sw * b.ux, b.ly, tx * b.lx + sx * b.ux, b.ly);
                lineTo(sx * b.lx + tx * b.ux, b.ly);
                cubicTo(sw * b.lx + tw * b.ux, b.ly, b.lx, sh * b.ly + th * b.uy, b.lx, sy * b.ly + ty * b.uy);
                close();
            }
        }
        void moveTo(float x, float y) {
            validate();
            float *pts = points.alloc(2);  x0 = pts[0] = x, y0 = pts[1] = y, *types.alloc(1) = kMove;
        }
        void lineTo(float x1, float y1) {
            if (x1 == x0 && y1 == y0)
                return;
            float *pts = points.alloc(2);  x0 = pts[0] = x1, y0 = pts[1] = y1, *types.alloc(1) = kLine;
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
                float *pts = points.alloc(4);  pts[0] = x1, pts[1] = y1, x0 = pts[2] = x2, y0 = pts[3] = y2, memset(types.alloc(2), kQuadratic, 2);
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
                    float *pts = points.alloc(6);  pts[0] = x1, pts[1] = y1, pts[2] = x2, pts[3] = y2, pts[4] = x3, pts[5] = y3, memset(types.alloc(3), kCubic, 3);
                    bx -= 3.f * (x1 - x0), by -= 3.f * (y1 - y0), dot += bx * bx + by * by, x0 = x3, y0 = y3;
                    s = ceilf(sqrtf(sqrtf(dot)));
                    cubicSums += s, maxCurve = fmaxf(maxCurve, dot / s);
                }
            }
        }
        void close() {
            float *pts = points.alloc(2);  pts[0] = x0, pts[1] = y0, *types.alloc(1) = kClose;
        }
        
        void validate() {
            if (points.idx != points.end) {
                Bounds molecule;
                for (size_t i = points.idx; i < points.end; i += 2)
                    molecule.extend(points.base[i], points.base[i + 1]);
                if (!molecule.isNull() && !molecule.isZero()) {
                    bounds.extend(molecule);
                    *(molecules.alloc(1)) = molecule;
                    for (size_t i = types.idx; i < types.end;) {
                        uint8_t type = types.base[i];
                        counts[type]++;
                        i += TypeSizes[type];
                    }
                    points.idx = points.end;
                    types.idx = types.end;
                } else {
                    points.end = points.idx;
                    types.end = types.idx;
                }
            }
        }
        bool isValid() {
            validate();
            return types.end > 1 && *types.base == Geometry::kMove && (bounds.lx != bounds.ux || bounds.ly != bounds.uy);
        }
        bool isRect() {
            validate();
            const float *pts = points.base;
            size_t last = types.end - 1;
            if (!(last == 4 || last == 5) || counts[kLine] != 4 || pts[0] != pts[last * 2] || pts[1] != pts[last * 2 + 1])
                return false;
            bool v0 = pts[2] == pts[0], h0 = pts[3] == pts[1], v1 = pts[4] == pts[2], h1 = pts[5] == pts[3];
            bool v2 = pts[6] == pts[4], h2 = pts[7] == pts[5], v3 = pts[8] == pts[6], h3 = pts[9] == pts[7];
            return ((v0 && h1) || (h0 && v1)) && ((v2 && h3) || (h2 && v3));
        }
        size_t upperBound(float det) const {
            size_t cubics = 0;
            if (cubicSums != 0) {
                float s = sqrtf(sqrtf(fmaxf(1e-2f, det)));
                cubics = det < 1.f ? ceilf(s * (cubicSums + 2.f)) : ceilf(s) * cubicSums;
            }
            return cubics + 2 * (counts[kMove] + counts[kLine] + counts[kQuadratic] + counts[kCubic]);
        }
        size_t hash() {
            xxhash = xxhash ?: XXH64(points.base, points.end * sizeof(float), XXH64(types.base, types.end * sizeof(uint8_t), 0));
            return xxhash;
        }
        size_t refCount, xxhash = 0, cubicSums = 0, counts[kCountSize] = { 0, 0, 0, 0, 0 };
        float x0 = 0.f, y0 = 0.f, maxCurve = 0.f;  Row<uint8_t> types;  Row<float> points;
        Bounds bounds;  Row<Bounds> molecules;
        Row<Point16> p16s;  Row<uint8_t> p16cnts;  Row<Atom> atoms;
    };
    typedef Ref<Geometry> Path;
    
    typedef uint8_t Component;
    
    struct Color {
        Color() : b(0), g(0), r(0), a(255) {}
        Color(uint32_t rgba) : b((rgba >> 16) & 0xFF), g((rgba >> 8) & 0xFF), r(rgba & 0xFF), a(rgba >> 24) {};
        Color(uint8_t b, uint8_t g, uint8_t r, uint8_t a) : b(b), g(g), r(r), a(a) {}
        Color(Color c0, Color c1, float t) {
            float s = 1.f - t;
            b = s * c0.b + t * c1.b, g = s * c0.g + t * c1.g, r = s * c0.r + t * c1.r, a = s * c0.a + t * c1.a;
        }
        Color premultiplied() const {
            float alpha = a / 255.f;
            return Color(b * alpha, g * alpha, r * alpha, a);
        }
        Component b, g, r, a;
    };
    
    struct Paint {
        enum Type { kColor = 0, kLinear, kRadial, kImage };
        
        Paint() {}
        Paint(Color color) : color(color), minAlpha(color.a), maxAlpha(color.a) {}
        Paint(Color *stops, float *locations, size_t count, Transform transform, bool isRadial) {
            if (stops == nullptr || locations == nullptr || count < 2)
                return;
            setMinMaxAlpha(stops, count, 1, 0);
            if (maxAlpha == 0)
                return;
            type = isRadial ? kRadial : kLinear;
            colors.add(stops, count);
            locs.add(locations, count);
            ctm = transform;
            writeGradientStrip(strip.memory->alloc(kColorTextureWidth), kColorTextureWidth);
        }
        Paint(Color *buffer, size_t width, size_t height, size_t bpr) {
            if (buffer == nullptr || width == 0 || height == 0 || bpr == 0)
                return;
            setMinMaxAlpha(buffer, width, height, bpr);
            if (maxAlpha == 0)
                return;
            assert(bpr / sizeof(Color) >= width);
            type = kImage, w = width, h = height;
            size_t stride = bpr / sizeof(Color);
            if (stride == width)
                colors.add(buffer, width * height);
            else {
                for (size_t i = 0; i < height; i++)
                    colors.add(buffer + i * stride, width);
            }
            xxhash = XXH64(& width, sizeof(width), 0);
            xxhash = XXH64(& height, sizeof(height), xxhash);
            xxhash = XXH64(& colors[0], width * height * sizeof(Color), xxhash);
        }
        inline bool isValid() const {
            return maxAlpha != 0;
        }
        inline bool isOpaque() const {
            return !isImage() && minAlpha == 255;
        }
        inline bool isGradient() const {
            return type == kLinear || type == kRadial;
        }
        inline bool isImage() const {
            return type == kImage;
        }
        inline size_t hash() const {
            return xxhash;
        }
        inline void setMinMaxAlpha(Color *buffer, size_t width, size_t height, size_t bpr) {
            float alpha, min = 255, max = 0;
            size_t base = 0;
            for (size_t i = 0; i < height; i++, base += bpr / sizeof(*buffer))
                for (size_t j = 0; j < width; j++)
                    alpha = buffer[base + j].a, min = fminf(min, alpha), max = fmaxf(max, alpha);
            minAlpha = min, maxAlpha = max;
        }
        void writeGradientStrip(Color *dst, size_t size) const {
            size_t count = locs.end();
            if (count == 0)
                return;
            
            float *locations = & locs[0];
            Color *stops = & colors[0];
            std::sort(locations, locations + count);
            float lower = locations[0], upper = locations[count - 1];
            float t, *t0, *t1;
            size_t loc;
            for (size_t i = 0; i < size; i++) {
                t = fmaxf(lower, fminf(upper, float(i) / float(size - 1)));
                t0 = locations, t1 = t0 + 1;
                for (loc = 0; loc < count - 1; loc++, t0++, t1++)
                    if (t >= *t0 && t <= *t1)
                        break;
                t = fmaxf(0.f, fminf(1.f, (t - *t0) / (*t1 - *t0)));
                dst[i] = Color(stops[loc], stops[loc + 1], t).premultiplied();
            }
        }
        Type type = kColor;
        size_t refCount, xxhash = 0, w = 0, h = 0;
        Color color;
        Vector<Color> colors;
        Vector<float> locs;
        Transform ctm;
        Vector<Color> strip;
        Component minAlpha = 255, maxAlpha = 255;
    };
    
    struct Draw {
        Draw(const Path& path, const Transform& ctm, const Paint& paint, float width, uint8_t flags, Bounds *clipBounds = nullptr, Path *clipPath = nullptr)
        : path(path), ctm(ctm), paint(paint), width(width), flags(flags),
          clip(clipBounds ? *clipBounds : Bounds::huge()), clipPath(clipPath ? *clipPath : nullptr) {}
        
        inline Bounds bounds() const {
            return Bounds(path->bounds.inset(-0.5f * width, -0.5f * width).quad(ctm)).intersect(clip);
        }
        Path path;  Transform ctm;  Paint paint;  float width = 0.f;  uint8_t flags = 0;  Bounds clip;  Path clipPath = nullptr;
    };
    struct Scene {
        enum Flags { kFillEvenOdd = 1 << 1, kRoundCap = 1 << 2, kSquareCap = 1 << 3, kRoundJoin = 1 << 4 };

        struct Entry {
            Entry(const Geometry *g, size_t idx) : g(g), idx(idx) {}
            const Geometry *g;  size_t idx;
        };
        struct Index {
            Index(size_t hash, size_t i) : hash(hash), i(i)  {}
            inline bool operator< (const Index& other) const  { return hash < other.hash; }
            size_t hash, i;
        };
        void addPath(const Path& path, const Transform& ctm, const Paint& paint, float width, uint8_t flag, Bounds *clipBounds = nullptr, Path *clipPath = nullptr) {
            if (path->isValid() && paint.isValid() && (clipPath == nullptr || (*clipPath)->isValid())) {
                new (draws.memory->alloc(1)) Draw(path, ctm, paint, width, flag, clipBounds, clipPath);
                needPrepare = true;
            }
        }
        void addDraws(const Draw *src, size_t count) {
            if (src == nullptr || count == 0)
                return;
            for (size_t i = 0; i < count; i++)
                draws.add(src[i]);
            needPrepare = true;
        }
        void addScene(const Scene& scene) {
            addDraws(& scene.draws[0], scene.draws.end());
        }
        Bounds bounds() const {
            Bounds b;
            for (int i = 0; i < draws.end(); i++)
                b.extend(draws[i].bounds());
            return b;
        }
        size_t count() const {
            return draws.end();
        }
        size_t weight() const {
            size_t total = 0;
            for (int i = 0; i < draws.end(); i++)
                total += draws[i].path->types.end;
            return total;
        }
        void prepare() {
            if (!needPrepare)
                return;
            needPrepare = false;
            
            Row<Index> indices;  indices.prealloc(count());
            bnds.empty();
            Bounds *bs = bnds.alloc(count());
            
            for (size_t i = 0; i < count(); i++) {
                const Draw& draw = draws[i];
                *bs++ = draw.path->bounds;
                if (draw.width == 0)
                    new (indices.alloc(1)) Index(draw.path->hash(), i);
            }
            Index *index0 = indices.base, *index1 = indices.base + indices.end;
            std::sort(index0, index1);
            
            p16bases.empty(), p16entries.empty();
            uint32_t *bases = p16bases.alloc(count());
            
            size_t lastHash = 0, count = 0, total = 0, srcIndex = 0;
            for (Index *index = index0; index < index1; index++) {
                if (index == index0 || lastHash != index->hash) {
                    lastHash = index->hash;
                    srcIndex = index->i;
                    total += count;

                    Geometry *g = draws[index->i].path.ptr;
                    new (p16entries.alloc(1)) Entry(g, total);
                    
                    if (kMoleculesHeight && g->p16s.end == 0)
                        P16Writer().writeGeometry(g);
                    count = g->p16s.end;
                }
                bases[index->i] = uint32_t(total);
                if (srcIndex != index->i)
                    draws[index->i].path = draws[srcIndex].path;
            }
            total += count;
            p16total = uint32_t(total);
        }
        size_t refCount;
        RefVector<Draw> draws;
        bool needPrepare = false;
        Row<Bounds> bnds;  Row<uint32_t> p16bases;  Row<Entry> p16entries;  uint32_t p16total = 0;
    };
    typedef Ref<Scene> SceneRef;
    
    struct Params {
        bool useClips = true, useCurves = true, showOpaques = true, showOutlines = false;
        Color clearColor = { 255, 255, 255, 255 };
    };
    struct SceneList {
        Bounds bounds() const {
            Bounds b;
            for (int i = 0; i < scenes.size(); i++)
                b.extend(clips[i].intersect(scenes[i]->bounds().quad(ctms[i])));
            return b;
        }
        SceneList& addList(SceneList list) {
            for (int i = 0; i < list.scenes.size(); i++)
                addScene(list.scenes[i], list.ctms[i], list.clips[i]);
            return *this;
        }
        SceneList& addScene(SceneRef scene, Transform ctm = Transform(), Bounds clip = Bounds::huge()) {
            if (scene->count())
                pathsCount += scene->count(), scenes.emplace_back(scene), ctms.emplace_back(ctm), clips.emplace_back(clip);
            return *this;
        }
        void prepare() const {
            for (auto scene: scenes)
                scene->prepare();
        }
        Transform ctm;  Params params;
        size_t pathsCount = 0;  std::vector<SceneRef> scenes;  std::vector<Transform> ctms;  std::vector<Bounds> clips;
    };
    
    struct Segment {
        inline Segment(float x0, float y0, float x1, float y1, bool curve) : ix0((*((uint32_t *)& x0) & ~1) | curve), y0(y0), x1(x1), y1(y1) {}
        union { float x0; uint32_t ix0; };  float y0, x1, y1;
    };
    struct Cell {
        uint16_t lx, ly, ux, uy, ox, oy;
    };
    struct Quad {
        Cell cell;  short cover;  int base, biid, molsbase;
    };
    struct Quadratic {
        float x0, y0, x1, y1, x2, y2;
    };
    struct Outline {
        Quadratic quad;
        short prev, next;
    };
    struct Instance {
        enum Flags {
            kRoundJoin = 1 << 21,   kStencil = 1 << 21,
            kIsRadial = 1 << 22,    kDisableImage = 1 << 22,
            kIsGradient = 1 << 23,  kNextImage = 1 << 23,
            kIsImage = 1 << 24,     kIsCurve = 1 << 24,
            kMolecule = 1 << 25,    kPCap = 1 << 25,
            kFastEdges = 1 << 26,   kNCap = 1 << 26,
            kEdge = 1 << 27,        kF0 = 1 << 27,
            kRoundCap = 1 << 28,    kF1 = 1 << 28,
            kOutlines = 1 << 29,
            kSquareCap = 1 << 30,
            kEvenOdd = 1 << 31,
            kFragmentMask = (kOutlines | kSquareCap | kEvenOdd)
        };
        Instance(size_t iz) : iz(uint32_t(iz)) {}
        uint32_t iz;  union { Quad quad;  Outline outline; };
    };
    struct Opaque {
        uint32_t iz;  union { Cell cell;  Quadratic quad; };
    };
    struct Blend : Instance {
        Blend(size_t iz) : Instance(iz) {}
        struct { int count, idx; } data;
        Geometry *g;
    };
    struct Edge {
        uint32_t ic;  enum Flags { ue0 = 0xF << 28, ue1 = 0xF << 24, kMask = ~(ue0 | ue1) };
        uint16_t i0, ux;
    };
    struct Buffer {
        enum Type { kQuadEdges, kFastEdges, kFastMolecules, kQuadMolecules, kOpaques, kInstances, kSegmentsBase, kInstancesBase, kStencils, kDisableClip, kEnableClip, kNextImage, kDisableImage };
        struct Entry {
            Entry(Type type, size_t begin, size_t end) : type(type), begin(begin), end(end) {}
            Type type;  size_t begin, end;
        };
        
        void prepare(const SceneList& list) {
            params = list.params;
            pathsCount = list.pathsCount;
            texCount = 0;
            images.resize(0);
        
            size_t i, sizes[] = { sizeof(Color), sizeof(Transform), sizeof(Transform), sizeof(float), sizeof(Bounds), sizeof(Transform), sizeof(uint32_t) };
            size_t count = sizeof(sizes) / sizeof(*sizes), base = 0;
            Vector<size_t> bases(count);
            for (i = 0; i < count; i++)
                bases[i] = base, base += (pathsCount + 1) * sizes[i];
            colors = bases[0], ctms = bases[1], clips = bases[2], widths = bases[3], bounds = bases[4], texCtms = bases[5], texIdxs = bases[6];
            headerSize = (base + 15) & ~15, entries.empty();
        }
        uint8_t *base = nullptr;  Row<Entry> entries;
        RefVector<Paint> images;
        Params params;
        size_t colors, ctms, clips, widths, bounds, texCtms, texIdxs, texStrips, p16s;
        size_t idxs, pathsCount, texCount, headerSize;
    };
    struct Allocator {
        enum CountType { kFastEdges, kQuadEdges, kFastMolecules, kQuadMolecules };
        struct Pass {
            Pass(size_t idx) : idx(idx) {}
            size_t count() const { return counts[0] + counts[1] + counts[2] + counts[3]; }
            size_t idx, counts[4] = { 0, 0, 0, 0 };
        };
        void empty(Bounds device) {
            full = device, sheet = Bounds(0.f, 0.f, 0.f, 0.f), bzero(strips, sizeof(strips)), passes.empty();
        }
        void refill(size_t idx) {
            sheet = full, bzero(strips, sizeof(strips)), new (passes.alloc(1)) Pass(idx);
        }
        inline void alloc(float lx, float ly, float ux, float uy, size_t idx, Cell *cell, int type, size_t count) {
            float w = ux - lx, h = uy - ly;
            size_t i = h <= kStripHeight ? 0 : h <= kfh ? 1 : fmaxf(0.f, ceilf(log2f(h / kStripHeight))), hght = (1 << i) * kStripHeight;
            Bounds *strip = strips + i;
            if (strip->ux - strip->lx < w) {
                if (sheet.uy - sheet.ly < hght)
                    refill(idx);
                strip->lx = sheet.lx, strip->ly = sheet.ly, strip->ux = sheet.ux, strip->uy = sheet.ly + hght, sheet.ly = strip->uy;
            }
            cell->ox = strip->lx, cell->oy = strip->ly, cell->lx = lx, cell->ly = ly, cell->ux = ux, cell->uy = uy, strip->lx += w;
            passes.back().counts[type] += count;
        }
        Row<Pass> passes;
        Bounds full, sheet, strips[kStripCount];
    };
    
    struct Sample {
        struct Index {
            uint16_t lx, i;
            inline bool operator< (const Index& other) const { return lx < other.lx; }
        };
        Sample(float lx, float ux, float cover, size_t is): lx(lx), ux(ceilf(ux)), cover(cover), is(uint32_t(is)) {}
        int16_t lx, ux; float cover;  uint32_t is;
    };
    
    struct TexRef {
        TexRef(size_t iz, Color *strip) : iz(uint32_t(iz)), strip(strip) {}
        uint32_t iz;  Color *strip;
    };
    struct Context {
        void drawList(const SceneList& list, Bounds device, Transform view, size_t slz, size_t suz, Buffer *buffer) {
            empty(), allocator.empty(device), allocator.refill(0);
            size_t fatlines = 1.f + ceilf((device.uy - device.ly) * krfh);
            if (samples.end() != fatlines) {
                samples.resize(0);
                for (int i = 0; i < fatlines; i++)
                    samples.add(Row<Sample>());
            }
            Color *colors = (Color *)(buffer->base + buffer->colors);
            Transform *ctms = (Transform *)(buffer->base + buffer->ctms);
            Transform *clips = (Transform *)(buffer->base + buffer->clips);
            float *widths = (float *)(buffer->base + buffer->widths);
            Bounds *bounds = (Bounds *)(buffer->base + buffer->bounds);
            Transform *texCtms = (Transform *)(buffer->base + buffer->texCtms);
            bool clipActive = false;
            
            Color black(0, 0, 0, 255), red(0, 0, 255, 255);
            size_t lz, uz, i, clz, cuz, iz, is, cnt; uint32_t p16total = 0;
            Geometry *lastClipPath = nullptr, *currentClipPath = nullptr;
            uint8_t flags;
            float det, width, uw, softclipMargin = 0.5f;
            for (lz = uz = i = 0; i < list.scenes.size(); p16total += list.scenes[i]->p16total, i++, lz = uz ) {
                const Scene *scn = list.scenes[i].ptr;
                uz = lz + scn->count(), clz = lz < slz ? slz : lz > suz ? suz : lz, cuz = uz < slz ? slz : uz > suz ? suz : uz;
                Transform ctm = list.ctms[i].concat(view), clipquad, m, quad, invclip;
                Bounds dev, clip, *bnds, clipBounds = device, sceneclip = list.clips[i], lastClip;
                                
                for (is = clz - lz, iz = clz; iz < cuz; iz++, is++) {
                    Draw& draw = scn->draws[is];
                    flags = draw.flags;
                    
                    if (list.params.useClips) {
                        if (memcmp(& draw.clip, & lastClip, sizeof(Bounds)) != 0) {
                            lastClip = draw.clip;
                            clipActive = !lastClip.isHuge() || !sceneclip.isHuge();
                            clipquad = clipActive ? sceneclip.intersect(lastClip).quad(ctm) : Transform(1e12f, 0.f, 0.f, 1e12f, -5e11f, -5e11f);
                            softclipMargin = 0.5f + 1e-1f / fmaxf(1.f, clipquad.scale());
                            invclip = clipquad.invert();
                            clipBounds = Bounds(clipquad).integral().intersect(device);
                        }
                        Geometry *clipPath = draw.clipPath.ptr;
                        if (lastClipPath != clipPath) {
                            lastClipPath = clipPath;
                            Blend *inst = new (blends.alloc(1)) Blend(iz | Instance::kStencil);
                            inst->data.count = 0, inst->g = nullptr;
                            if (clipPath) {
                                if (currentClipPath != clipPath) {
                                    currentClipPath = clipPath;
                                    size_t i0, i1;
                                    i0 = stencils.end;
                                    Stenciler stenciler(clipPath, device, ctm, & stencils);
                                    stenciler.applyPath(clipPath, ctm, device, true, true);
                                    i1 = stencils.end;
                                    inst->data.idx = int(i0), inst->data.count = int(i1 - i0);
                                } else
                                    inst->data.idx = 1;
                            } else {
                                inst->data.idx = 0;
                            }
                        }
                    }
                    m = draw.ctm.concat(ctm), det = fabsf(m.a * m.d - m.b * m.c);
                    uw = draw.width;
                    width = list.params.showOutlines ? 1.f : uw * (uw > 0.f ? sqrtf(det) : -1.f);
                    bnds = scn->bnds.base + is, quad = bnds->quad(m), dev = Bounds(quad).inset(-width, -width);
                    clip = dev.integral().intersect(clipBounds);
                    
                    if (clip.lx < clip.ux && clip.ly < clip.uy) {
                        bool unclipped = clip.contains(dev);
                        float clipWidth = clip.width(), clipHeight = clip.height();
                        Paint *color = & draw.paint;
                        bool isOpaque = color->isOpaque();
                        bool isGradient = list.params.showOutlines ? false : color->isGradient();
                        bool isRadial = isGradient && color->type == Paint::kRadial;
                        bool isImage = list.params.showOutlines ? false : color->type == Paint::kImage;
                        size_t colorFlags = isImage ? Instance::kIsImage : (isGradient * Instance::kIsGradient | isRadial * Instance::kIsRadial);
                        if (isGradient) {
                            texCtms[iz] = color->ctm.concat(m).invert();
                            texs.add(TexRef(iz, & color->strip[0]));
                        } else if (isImage) {
                            texCtms[iz] = quad.invert();
                            new (blends.alloc(1)) Blend(iz | Instance::kIsImage | Instance::kNextImage);
                            images.add(color);
                        }
                        
                        if (list.params.showOutlines)
                            colors[iz] = uw == 0.f ? black : red;
                        else
                            colors[iz] = draw.paint.color.premultiplied();
                        
                        clips[iz] = invclip;
                        Geometry *g = draw.path.ptr;
                        if (width) {
                            widths[iz] = width;
                            Blend *inst = new (blends.alloc(1)) Blend(iz | colorFlags | Instance::kOutlines | bool(flags & Scene::kRoundCap) * Instance::kRoundCap | bool(flags & Scene::kSquareCap) * Instance::kSquareCap | bool(flags & Scene::kRoundJoin) * Instance::kRoundJoin);
                            
                            Bounds outlineClip = unclipped ? Bounds::huge() : clip.inset(-width, -width);
                            uint32_t i0 = uint32_t(outlines.idx), i1;
                            Outliner outliner;
                            outliner.iz = inst->iz, outliner.outlines = & outlines;
                            if (width > 4.f && isOpaque && lastClipPath == nullptr) {
                                bool softunclipped = true;
                                if (clipActive) {
                                    Bounds soft = quad.concat(invclip);
                                    softunclipped = fmaxf(fmaxf(fabsf(soft.lx - 0.5f), fabsf(soft.ux - 0.5f)), fmaxf(fabsf(soft.ly - 0.5f), fabsf(soft.uy - 0.5f))) < softclipMargin;
                                }
                                outliner.opaques = softunclipped ? & opaques : nullptr;
                            }
                            outliner.applyPath(g, m, outlineClip, unclipped, false);
                            i1 = uint32_t(outlines.idx);
                            inst->data.idx = i0, inst->data.count = i1 - i0;
                        } else if (clipWidth * clipHeight / g->types.end < kMoleculesPixelsPerEdge) {
                            ctms[iz] = m, bounds[iz] = *bnds;
                            bool fast = !buffer->params.useCurves || g->maxCurve * det < 16.f;
                            Blend *inst = new (blends.alloc(1)) Blend(iz | colorFlags | Instance::kMolecule | bool(flags & Scene::kFillEvenOdd) * Instance::kEvenOdd | fast * Instance::kFastEdges);
                            inst->g = g, inst->quad.cover = 0;
                            inst->quad.base = int(p16total + scn->p16bases.base[is]);
                            inst->quad.molsbase = int(g->p16s.idx / 2);
                            cnt = fast ? g->p16s.idx / kFastSegments : g->atoms.end;
                            int type = fast ? Allocator::kFastMolecules : Allocator::kQuadMolecules;
                            allocator.alloc(clip.lx, clip.ly, clip.ux, clip.uy, blends.end - 1, & inst->quad.cell, type, cnt);
                        } else {
                            bool fast = !buffer->params.useCurves || g->maxCurve * det < 4.f;
                            CurveIndexer idxr;
                            idxr.clip = clip, idxr.samples = & samples[0], idxr.fast = fast;
                            idxr.dst = idxr.dst0 = segments.alloc(2 * g->upperBound(det));
                            idxr.applyPath(g, m, clip, unclipped, true);
                            bool softunclipped = true;
                            if (clipActive) {
                                Bounds soft = quad.concat(invclip);
                                softunclipped = fmaxf(fmaxf(fabsf(soft.lx - 0.5f), fabsf(soft.ux - 0.5f)), fmaxf(fabsf(soft.ly - 0.5f), fabsf(soft.uy - 0.5f))) < softclipMargin;
                            }
                            writeSegmentInstances(clip, flags & Scene::kFillEvenOdd, iz, isOpaque && softunclipped && lastClipPath == nullptr, fast, colorFlags, *this);
                            segments.idx = segments.end = idxr.dst - segments.base;
                        }
                        if (isImage)
                            new (blends.alloc(1)) Blend(iz | Instance::kIsImage | Instance::kDisableImage);
                    }
                }
            }
        }
        void empty() {
            texTotal = 0, blends.empty(), opaques.empty(), stencils.empty(), outlines.empty(), segments.empty(), segmentsIndices.empty(), indices.empty(), texs.resize(0), images.resize(0);
            for (int i = 0; i < samples.end(); i++)
                samples[i].empty();
            entries = Vector<Buffer::Entry>();
        }
        void reset() {
            blends.reset(), opaques.reset(), stencils.reset(), outlines.reset(), segments.reset(), segmentsIndices.reset(), indices.reset(), entries = Vector<Buffer::Entry>(), texs.resize(0), images.resize(0);
            samples.resize(0);
        }
        
        size_t texTotal;
        Allocator allocator;  Vector<Buffer::Entry> entries;
        Vector<TexRef> texs;
        Vector<Paint *> images;
        Row<Opaque> opaques, stencils;  Row<Blend> blends;  Row<Instance> outlines;  Row<Segment> segments;
        Row<Sample::Index> indices;  RefVector<Row<Sample>> samples;  Row<uint32_t> segmentsIndices;
    };
    
    static void radixSort(uint32_t *in, int size, uint32_t lower, uint32_t range, bool single, uint16_t *counts) {
        range = range < 4 ? 4 : range;
        uint32_t mask = range - 1;
        uint32_t *tmp = (uint32_t *)alloca(size * sizeof(uint32_t));
        memset(counts, 0, sizeof(uint16_t) * range);
        for (int i = 0; i < size; i++)
            counts[(in[i] - lower) & mask]++;
        uint64_t *sums = (uint64_t *)counts, sum = 0, count;
        for (int i = 0; i < range / 4; i++) {
            count = sums[i], sum += count + (count << 16) + (count << 32) + (count << 48), sums[i] = sum;
            sum = sum & 0xFFFF000000000000, sum = sum | (sum >> 16) | (sum >> 32) | (sum >> 48);
        }
        for (int i = size - 1; i >= 0; i--)
            tmp[--counts[(in[i] - lower) & mask]] = in[i];
        if (single)
            memcpy(in, tmp, size * sizeof(uint32_t));
        else {
            memset(counts, 0, sizeof(uint16_t) * 64);
            for (int i = 0; i < size; i++)
                counts[(in[i] >> 8) & 0x3F]++;
            for (uint16_t *src = counts, *dst = src + 1, i = 1; i < 64; i++)
                *dst++ += *src++;
            for (int i = size - 1; i >= 0; i--)
                in[--counts[(tmp[i] >> 8) & 0x3F]] = tmp[i];
        }
    }
    static void writeSegmentInstances(Bounds clip, bool even, size_t iz, bool opaque, bool fast, size_t colorFlags, Context& ctx) {
        size_t ily = 0, iuy = ceilf(clip.height() * krfh), iy, i, begin, size;
        size_t edgeIz = iz | colorFlags | Instance::kEdge | even * Instance::kEvenOdd | fast * Instance::kFastEdges;
        uint32_t cellIz = uint32_t(iz | colorFlags);
        uint16_t counts[256], ly, uy, lx, ux;
        float h, cover, winding, wscale;
        Allocator::CountType type = fast ? Allocator::kFastEdges : Allocator::kQuadEdges;
        bool single = clip.ux - clip.lx < 256.f;
        uint32_t range = single ? 1 << uint32_t(ceilf(log2f(clip.ux - clip.lx + 1.f))) : 256;
        Row<Sample::Index> *indices = & ctx.indices;  Sample::Index *index, *idx;
        Row<Sample> *samples = & ctx.samples[0];  Sample *sample;
        
        for (iy = ily; iy < iuy; iy++, samples->empty(), samples++, indices->empty()) {
            if ((size = samples->end)) {
                for (sample = samples->base, idx = indices->alloc(size), i = 0; i < size; i++, sample++) {
                    if (sample->cover)
                        idx->lx = sample->lx, idx->i = i, idx++;
                }
                size = idx - indices->base;
                if (size > 32 && size < 65536)
                    radixSort((uint32_t *)indices->base, int(size), single ? clip.lx : 0, range, single, counts);
                else
                    std::sort(indices->base, indices->base + size);
                
                size_t siBase = ctx.segmentsIndices.end;
                uint32_t *si = ctx.segmentsIndices.alloc(size);
                
                ly = iy * kfh + clip.ly, ly = ly < clip.ly ? clip.ly : ly > clip.uy ? clip.uy : ly;
                uy = (iy + 1) * kfh + clip.ly, uy = uy < clip.ly ? clip.ly : uy > clip.uy ? clip.uy : uy;
                for (h = uy - ly, wscale = 1.f / h, cover = winding = 0.f, index = indices->base, lx = ux = index->lx, i = begin = 0; i < size; i++, index++) {
                    if (index->lx >= ux && fabsf((winding - floorf(winding)) - 0.5f) > 0.49999f) {
                        if (lx != ux) {
                            Blend *inst = new (ctx.blends.alloc(1)) Blend(edgeIz);
                            ctx.allocator.alloc(lx, ly, ux, uy, ctx.blends.end - 1, & inst->quad.cell, type, (i - begin + 1) / 2);
                            inst->quad.cover = short(cover), inst->quad.base = int(ctx.segments.idx), inst->data.count = int(i - begin), inst->data.idx = int(siBase + begin);
                        }
                        winding = cover = truncf(winding + copysign(0.5f, winding));
                        if ((even && (int(winding) & 1)) || (!even && winding)) {
                            if (opaque) {
                                Opaque *opaque = ctx.opaques.alloc(1);
                                Cell *cell = & opaque->cell;
                                opaque->iz = cellIz;
                                cell->lx = ux, cell->ly = ly, cell->ux = index->lx, cell->uy = uy;
                            } else {
                                Cell *cell = & (new (ctx.blends.alloc(1)) Blend(cellIz))->quad.cell;
                                cell->lx = ux, cell->ly = ly, cell->ux = index->lx, cell->uy = uy, cell->ox = kNullIndex;
                            }
                        }
                        begin = i, lx = ux = index->lx;
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
    
    struct GeometryWriter {
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
                float N = sqrtf(adot) / (fabsf(cubicScale) * kCubicMultiplier);
                count = N <= 1.f ? 1.f : ceilf(cbrtf(N));
                dt = 0.5f / count, dt2 = dt * dt;
                x = x0, bx *= dt2, ax *= dt2 * dt, f3x = 6.f * ax, f2x = f3x + 2.f * bx, f1x = ax + bx + cx * dt;
                y = y0, by *= dt2, ay *= dt2 * dt, f3y = 6.f * ay, f2y = f3y + 2.f * by, f1y = ay + by + cy * dt;
                while (--count) {
                    x += f1x, f1x += f2x, f2x += f3x, x1 = x,
                    x += f1x, f1x += f2x, f2x += f3x, x2 = x;
                    x1 = 2.f * x1 - 0.5f * (x0 + x2);
                    y += f1y, f1y += f2y, f2y += f3y, y1 = y;
                    y += f1y, f1y += f2y, f2y += f3y, y2 = y;
                    y1 = 2.f * y1 - 0.5f * (y0 + y2);
                    Quadratic(x0, y0, x1, y1, x2, y2);
                    x0 = x2, y0 = y2;
                }
                x += f1x, x1 = x, x1 = 2.f * x1 - 0.5f * (x0 + x3);
                y += f1y, y1 = y, y1 = 2.f * y1 - 0.5f * (y0 + y3);
                Quadratic(x0, y0, x1, y1, x3, y3);
            }
        }
        virtual void EndSubpath(float x0, float y0, float x1, float y1, bool closed) {}
        
        void applyPath(Geometry *g, Transform m0, Bounds clip0, bool unclipped0, bool polygon0) {
            this->m = m0, this->clip = clip0, this->unclipped = unclipped0, this->polygon = polygon0;
            
            bool closed, closeSubpath = false;  float *p = g->points.base, sx = FLT_MAX, sy = FLT_MAX, x0 = FLT_MAX, y0 = FLT_MAX, x1, y1, x2, y2, x3, y3;
            for (uint8_t *type = g->types.base, *end = type + g->types.end; type < end; )
                switch (*type) {
                    case Geometry::kMove:
                        if ((closed = (polygon || closeSubpath) && (sx != x0 || sy != y0)))
                            line(x0, y0, sx, sy);
                        if (sx != FLT_MAX)
                            EndSubpath(x0, y0, sx, sy, closeSubpath || closed);
                        sx = x0 = p[0] * m.a + p[1] * m.c + m.tx, sy = y0 = p[0] * m.b + p[1] * m.d + m.ty, p += 2, type++, closeSubpath = false;
                        break;
                    case Geometry::kLine:
                        x1 = p[0] * m.a + p[1] * m.c + m.tx, y1 = p[0] * m.b + p[1] * m.d + m.ty;
                        line(x0, y0, x1, y1);
                        x0 = x1, y0 = y1, p += 2, type++;
                        break;
                    case Geometry::kQuadratic:
                        x1 = p[0] * m.a + p[1] * m.c + m.tx, y1 = p[0] * m.b + p[1] * m.d + m.ty;
                        x2 = p[2] * m.a + p[3] * m.c + m.tx, y2 = p[2] * m.b + p[3] * m.d + m.ty;
                        if (unclipped)
                            Quadratic(x0, y0, x1, y1, x2, y2);
                        else {
                            ly = fminf(y0, fminf(y1, y2)), uy = fmaxf(y0, fmaxf(y1, y2));
                            if (ly < clip.uy && uy > clip.ly) {
                                lx = fminf(x0, fminf(x1, x2)), ux = fmaxf(x0, fmaxf(x1, x2));
                                if (polygon || !(ux < clip.lx || lx > clip.ux)) {
                                    if (ly < clip.ly || uy > clip.uy || lx < clip.lx || ux > clip.ux)
                                        clipQuadratic(x0, y0, x1, y1, x2, y2);
                                    else
                                        Quadratic(x0, y0, x1, y1, x2, y2);
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
                            Cubic(x0, y0, x1, y1, x2, y2, x3, y3);
                        else {
                            ly = fminf(fminf(y0, y1), fminf(y2, y3)), uy = fmaxf(fmaxf(y0, y1), fmaxf(y2, y3));
                            if (ly < clip.uy && uy > clip.ly) {
                                lx = fminf(fminf(x0, x1), fminf(x2, x3)), ux = fmaxf(fmaxf(x0, x1), fmaxf(x2, x3));
                                if (polygon || !(ux < clip.lx || lx > clip.ux)) {
                                    if (ly < clip.ly || uy > clip.uy || lx < clip.lx || ux > clip.ux)
                                        clipCubic(x0, y0, x1, y1, x2, y2, x3, y3);
                                    else
                                        Cubic(x0, y0, x1, y1, x2, y2, x3, y3);
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
                line(x0, y0, sx, sy);
            EndSubpath(x0, y0, sx, sy, closeSubpath || closed);
        }
        
        inline void line(float x0, float y0, float x1, float y1) {
            if (unclipped)
                writeSegment(x0, y0, x1, y1);
            else {
                ly = fminf(y0, y1), uy = fmaxf(y0, y1);
                if (ly < clip.uy && uy > clip.ly) {
                    lx = fminf(x0, x1), ux = fmaxf(x0, x1);
                    if (ly < clip.ly || uy > clip.uy || lx < clip.lx || ux > clip.ux)
                        clipLine(x0, y0, x1, y1);
                    else
                        writeSegment(x0, y0, x1, y1);
                }
            }
        }
        void clipLine(float x0, float y0, float x1, float y1) {
            float roots[6], *root = roots, *r, s, t, sx0, sy0, sx1, sy1, mx, my, vx;
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
                        writeSegment(sx0, sy0, sx1, sy1);
                    else if (polygon)
                        vx = mx <= clip.lx ? clip.lx : clip.ux, writeSegment(vx, sy0, vx, sy1);
                }
            }
        }
        void clipQuadratic(float x0, float y0, float x1, float y1, float x2, float y2) {
            float ax, bx, ay, by, roots[10], *root = roots, *r, t, mt, mx, my, vx, sx0, sy0, sx2, sy2;
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
                        Quadratic(x0, y0, x1, y1, x2, y2);
                    else if (polygon)
                        vx = lx <= clip.lx ? clip.lx : clip.ux, writeSegment(vx, y0, vx, y2);
                }
            } else {
                std::sort(roots + 1, root), *root = 1.f;
                for (sx0 = x0, sy0 = y0, r = roots; r < root; r++, sx0 = sx2, sy0 = sy2) {
                    t = r[1], mt = 0.5f * (r[0] + r[1]);
                    sx2 = t == 1.f ? x2 : (ax * t + bx) * t + x0;
                    sy2 = t == 1.f ? y2 : (ay * t + by) * t + y0;
                    mx = (ax * mt + bx) * mt + x0;
                    my = (ay * mt + by) * mt + y0;
                    if (my >= clip.ly && my < clip.uy) {
                        if (mx >= clip.lx && mx < clip.ux)
                            Quadratic(sx0, sy0, 2.f * mx - 0.5f * (sx0 + sx2), 2.f * my - 0.5f * (sy0 + sy2), sx2, sy2);
                        else if (polygon)
                            vx = mx <= clip.lx ? clip.lx : clip.ux, writeSegment(vx, sy0, vx, sy2);
                    }
                }
            }
        }
        void clipCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
            float cx, bx, ax, cy, by, ay, roots[14], *root = roots, *r, t, mt, mx, my, vx, x0t, y0t, x1t, y1t, x2t, y2t, x3t, y3t, fx, gx, fy, gy;
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
                        Cubic(x0, y0, x1, y1, x2, y2, x3, y3);
                    else if (polygon)
                        vx = lx <= clip.lx ? clip.lx : clip.ux, writeSegment(vx, y0, vx, y3);
                }
            } else {
                std::sort(roots + 1, root), *root = 1.f;
                for (x0t = x0, y0t = y0, r = roots; r < root; r++, x0t = x3t, y0t = y3t) {
                    t = r[1], mt = 0.5f * (r[0] + r[1]);
                    x3t = t == 1.f ? x3 : ((ax * t + bx) * t + cx) * t + x0;
                    y3t = t == 1.f ? y3 : ((ay * t + by) * t + cy) * t + y0;
                    mx = ((ax * mt + bx) * mt + cx) * mt + x0;
                    my = ((ay * mt + by) * mt + cy) * mt + y0;
                    if (my >= clip.ly && my < clip.uy) {
                        if (mx >= clip.lx && mx < clip.ux) {
                            const float u = 1.f / 3.f, v = 2.f / 3.f, u3 = 1.f / 27.f, v3 = 8.f / 27.f;
                            mt = v * r[0] + u * r[1], x1t = ((ax * mt + bx) * mt + cx) * mt + x0, y1t = ((ay * mt + by) * mt + cy) * mt + y0;
                            mt = u * r[0] + v * r[1], x2t = ((ax * mt + bx) * mt + cx) * mt + x0, y2t = ((ay * mt + by) * mt + cy) * mt + y0;
                            fx = x1t - v3 * x0t - u3 * x3t, gx = x2t - u3 * x0t - v3 * x3t;
                            fy = y1t - v3 * y0t - u3 * y3t, gy = y2t - u3 * y0t - v3 * y3t;
                            Cubic(
                                x0t, y0t,
                                3.f * fx - 1.5f * gx, 3.f * fy - 1.5f * gy,
                                3.f * gx - 1.5f * fx, 3.f * gy - 1.5f * fy,
                                x3t, y3t
                            );
                        } else if (polygon)
                            vx = mx <= clip.lx ? clip.lx : clip.ux, writeSegment(vx, y0t, vx, y3t);
                    }
                }
            }
        }
        
        Transform m;  Bounds clip;  bool unclipped;  bool polygon;
        float lx, ly, ux, uy;
        float quadraticScale = 1.f, cubicScale = kCubicPrecision;
    };
    
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
   
    static float *solveCubic(double B, double C, double D, double A, float *roots) {
        if (fabs(A) < 1e-3)
            return solveQuadratic(B, C, D, roots);
        else {
            double p, p3, q, q2, u1, v1, d, sd, t;
            B /= A, B /= 3.0, C /= A, D /= A;
            p = C - 3.0 * B * B, p3 = p * p * p;
            q = B * (2.0 * B * B - C) + D, q2 = q * 0.5;
            d = q2 * q2 + p3 / 27.0;
            if (d < 0) {
                double r = sqrt(-p3 / 27.0), tcos = -q / (2 * r), crtr = 2 * copysign(cbrt(fabs(r)), r), sine, cosine;
                __sincos(acos(fmax(-1, fmin(1, tcos))) / 3, & sine, & cosine);
                t = crtr * cosine - B; if (t > 0.f && t < 1.f)  *roots++ = t;
                t = crtr * (-0.5 * cosine - 0.866025403784439 * sine) - B; if (t > 0.0 && t < 1.0)  *roots++ = t;
                t = crtr * (-0.5 * cosine + 0.866025403784439 * sine) - B; if (t > 0.0 && t < 1.0)  *roots++ = t;
            } else if (d == 0) {
                u1 = copysign(cbrt(fabs(q2)), q2);
                t = 2 * u1 - B; if (t > 0.0 && t < 1.0)  *roots++ = t;
                t = -u1 - B; if (t > 0.0 && t < 1.0)  *roots++ = t;
            } else {
                sd = sqrt(d), u1 = copysign(cbrt(fabs(sd - q2)), sd - q2), v1 = copysign(cbrt(fabs(sd + q2)), sd + q2);
                t = u1 - v1 - B; if (t > 0.0 && t < 1.0)  *roots++ = t;
            }
        }
        return roots;
    }
    
    struct CurveIndexer: GeometryWriter {
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
            size_t si = dst - dst0;
            new (dst++) Segment(x0, y0, x1, y1, false);
            
            y0 -= clip.ly, y1 -= clip.ly;
            if ((uint32_t(y0) & kFatMask) == (uint32_t(y1) & kFatMask))
                new (samples[int(y0 * krfh)].alloc(1)) Sample(fminf(x0, x1), fmaxf(x0, x1), y1 - y0, si);
            else {
                float lx, ux, ly, uy, iy, m, c, ny, minx, maxx, sign = copysignf(1.f, y1 - y0);
                lx = fminf(x0, x1), ux = fmaxf(x0, x1);
                ly = fminf(y0, y1), uy = fmaxf(y0, y1);
                iy = floorf(ly * krfh), m = (x1 - x0) / (y1 - y0), c = x0 - m * y0, m *= kfh;
                minx = (iy + float(m < 0.f)) * m + c;
                maxx = (iy + float(m > 0.f)) * m + c;
                for (ny = iy * kfh; ly < uy; ly = ny, minx += m, maxx += m, iy++) {
                    ny = fminf(uy, ny + kfh);
                    new (samples[int(iy)].alloc(1)) Sample(fmaxf(lx, minx), fminf(ux, maxx), (ny - ly) * sign, si);
                }
            }
        }
        __attribute__((always_inline)) void writeQuadratic(float x0, float y0, float x1, float y1, float x2, float y2) {
            y0 = fmaxf(clip.ly, fminf(clip.uy, y0));
            y1 = fmaxf(clip.ly, fminf(clip.uy, y1));
            y2 = fmaxf(clip.ly, fminf(clip.uy, y2));
            size_t si = dst - dst0;
            new (dst++) Segment(x0, y0, x1, y1, true), new (dst++) Segment(x1, y1, x2, y2, false);
            
            y0 -= clip.ly, y1 -= clip.ly, y2 -= clip.ly;
            if ((uint32_t(y0) & kFatMask) == (uint32_t(y2) & kFatMask))
                new (samples[int(y0 * krfh)].alloc(1)) Sample(fminf(x0, x2), fmaxf(x0, x2), y2 - y0, si);
            else {
                float ay, by, ax, bx, ly, uy, lx, ux, d2a, ity, iy, t, ny, sign = copysignf(1.f, y2 - y0);
                ax = x2 - x1, bx = x1 - x0, ax -= bx, bx *= 2.f;
                ay = y2 - y1, by = y1 - y0, ay -= by, by *= 2.f;
                d2a = 0.5f / ay, ity = -by * d2a, d2a *= sign;
                lx = y0 < y2 ? x0 : x2, ly = fminf(y0, y2), uy = fmaxf(y0, y2);
                for (iy = floorf(ly * krfh), ny = iy * kfh; ly < uy; ly = ny, iy++, lx = ux) {
                    ny = fminf(uy, ny + kfh);
                    t = ay == 0 ? -(y0 - ny) / by : ity + sqrtf(fmaxf(0.f, by * by - 4.f * ay * (y0 - ny))) * d2a;
                    t = fmaxf(0.f, fminf(1.f, t)), ux = (ax * t + bx) * t + x0;
                    new (samples[int(iy)].alloc(1)) Sample(fminf(lx, ux), fmaxf(lx, ux), (ny - ly) * sign, si);
                }
            }
        }
    };
    
    struct Dasher: GeometryWriter {
        static Path CreateDashedPath(Path path, float phase, float *pattern, size_t count) {
            if (pattern == nullptr || count < 2 || count % 2 == 1)
                return path;
            float length = 0.f;
            for (size_t i = 0; i < count; i++)
                length += pattern[i];
            Dasher dasher(fmod(phase, length), pattern, count);
            dasher.applyPath(path.ptr, Transform(), Bounds(), true, false);
            return dasher.dashed;
        }
        
        Dasher(float phase, float *pattern, size_t count) {
            dashPhase = phase, dashPattern = pattern, dashCount = count;
            reset();
        }
        
        void writeSegment(float x0, float y0, float x1, float y1) {
            writeCurve(x0, y0, FLT_MAX, FLT_MAX, x1, y1);
        }
        void Quadratic(float x0, float y0, float x1, float y1, float x2, float y2) {
            writeCurve(x0, y0, x1, y1, x2, y2);
        }
        void EndSubpath(float x0, float y0, float x1, float y1, bool closed) {
            reset();
        }
        
        void reset() {
            moveTo = true, dashIndex = 0, len0 = 0.f, dash0 = -dashPhase, dash1 = dash0 + dashPattern[0];
            while (dash1 < FLT_MIN)
                nextDash();
        }
        void writeCurve(float cx0, float cy0, float cx1, float cy1, float cx2, float cy2) {
            x0 = cx0, y0 = cy0, x1 = cx1, y1 = cy1, x2 = cx2, y2 = cy2;
            
            len1 = len0 + curveLength();
            getDash();
            while (t0 != t1) {
                writeDash();
                if (t1 == 1.f)
                    break;
                else
                    nextDash(), getDash();
            }
            len0 = len1;
        }
        float curveLength() {
            float chord = segmentLength(x0, y0, x2, y2);
            if (x1 == FLT_MAX)
                return chord;
            float l0 = segmentLength(x0, y0, x1, y1), l1 = segmentLength(x1, y1, x2, y2);
            B = 2.f * l0 / (l0 + l1), A = 1.f - B;
            return (2.f * chord + l0 + l1) / 3.f;
        }
        void getDash() {
            t0 = fmaxf(0.f, fminf(1.f, (dash0 - len0) / (len1 - len0)));
            t1 = fmaxf(0.f, fminf(1.f, (dash1 - len0) / (len1 - len0)));
            if (x1 == FLT_MAX || fabsf(A) < 1e-3f)
                return;
            if (t0 > 0.f && t0 < 1.f)
                t0 = fmaxf(0.f, fminf(1.f, 0.5f * (-B + sqrtf(B * B - 4.f * A * -t0)) / A));
            if (t1 > 0.f && t1 < 1.f)
                t1 = fmaxf(0.f, fminf(1.f, 0.5f * (-B + sqrtf(B * B - 4.f * A * -t1)) / A));
        }
        void writeDash() {
            if (x1 == FLT_MAX) {
                float s0 = 1.f - t0, s1 = 1.f - t1;
                if (moveTo)
                    moveTo = false, dashed->moveTo(x0 * s0 + x2 * t0, y0 * s0 + y2 * t0);
                dashed->lineTo(x0 * s1 + x2 * t1, y0 * s1 + y2 * t1);
            } else {
                float t, s, sx0, sy0, sx1, sy1, sx2, sy2, tx0, ty0, tx1, ty1, tx2, ty2;
                t = t1, s = 1.f - t;
                sx0 = s * x0 + t * x1, sx2 = s * x1 + t * x2, sx1 = s * sx0 + t * sx2;
                sy0 = s * y0 + t * y1, sy2 = s * y1 + t * y2, sy1 = s * sy0 + t * sy2;
                
                t = t0 / t1, s = 1.f - t;
                tx0 = s * x0 + t * sx0, tx2 = s * sx0 + t * sx1, tx1 = s * tx0 + t * tx2;
                ty0 = s * y0 + t * sy0, ty2 = s * sy0 + t * sy1, ty1 = s * ty0 + t * ty2;
                
                if (moveTo)
                    moveTo = false, dashed->moveTo(tx1, ty1);
                dashed->quadTo(tx2, ty2, sx1, sy1);
            }
        }
        void nextDash() {
            moveTo = true;
            dash0 = dash1 + dashPattern[++dashIndex % dashCount];
            dash1 = dash0 + dashPattern[++dashIndex % dashCount];
        }
        
        size_t dashIndex, dashCount;
        float dashPhase, *dashPattern;
        bool moveTo;
        float A, B, t0, t1, x0, y0, x1, y1, x2, y2, len0, len1, dash0, dash1;
        Path dashed;
        
        static inline float segmentLength(float x0, float y0, float x1, float y1) {
            float dx = x1 - x0, dy = y1 - y0;
            return sqrtf(dx * dx + dy * dy);
        }
    };
    
    struct P16Writer: GeometryWriter {
        static const uint8_t isMoveTo = 0x80;
        
        void writeGeometry(Geometry *g) {
            float s = kMoleculesRange / fmaxf(g->bounds.ux - g->bounds.lx, g->bounds.uy - g->bounds.ly);
            m = Transform(s, 0.f, 0.f, s, s * -g->bounds.lx, s * -g->bounds.ly);
            cubicScale = -kCubicPrecision * (kMoleculesRange / kMoleculesHeight);
            
            p16s = & g->p16s, p16cnts = & g->p16cnts, atoms = & g->atoms;
            size_t count = g->points.end / 2;
            p16s->prealloc(count), p16cnts->prealloc(count / kFastSegments), atoms->prealloc(count);
            applyPath(g, m, Bounds(), true, true);
            
            Bounds *b = g->molecules.base;
            Point16 *bnd16 = p16s->alloc(g->molecules.end * 2);
            for (size_t i = 0; i < g->molecules.end; i++, b++) {
                *bnd16++ = Point16(
                   b->lx * m.a + b->ly * m.c + m.tx,
                   b->lx * m.b + b->ly * m.d + m.ty);
                *bnd16++ = Point16(
                   b->ux * m.a + b->uy * m.c + m.tx,
                   b->ux * m.b + b->uy * m.d + m.ty);
            }
        }
        void writeSegment(float x0, float y0, float x1, float y1) {
            (atoms->alloc(1))->i = uint32_t(p16s->end);
            new (p16s->alloc(1)) Point16(x0, y0);
        }
        void Quadratic(float x0, float y0, float x1, float y1, float x2, float y2) {
            (atoms->alloc(1))->i = uint32_t(p16s->end);
            
            Point16 *p = p16s->alloc(2);
            new (p++) Point16(x0, y0, true);
            new (p++) Point16(0.5f * x1 + 0.25f * (x0 + x2), 0.5f * y1 + 0.25f * (y0 + y2));
        }
        void EndSubpath(float x0, float y0, float x1, float y1, bool closed) {
            Point16 *p = p16s->alloc(1);
            new (p) Point16(x1, y1);
            
            if (atoms->idx < atoms->end)
                atoms->base[atoms->idx].i |= Atom::isMoveTo;
            atoms->idx = atoms->end;
            
            size_t segcnt, icount, last, rem;
            segcnt = p16s->end - p16s->idx - 1, icount = (segcnt + kFastSegments) / kFastSegments;
            last = icount - 1, rem = segcnt - last * kFastSegments;
            uint8_t *counts = p16cnts->alloc(icount);
            memset(counts, kFastSegments, last);
            counts[last] = rem, counts[0] |= isMoveTo;
            rem = p16cnts->end * kFastSegments - p16s->end;
            bzero(p16s->alloc(rem), rem * sizeof(*p));
            p16s->idx = p16s->end;
        }
        Row<Point16> *p16s;   Row<uint8_t> *p16cnts;  Row<Atom> *atoms;
    };
    
    struct Outliner: GeometryWriter {
        void writeSegment(float x0, float y0, float x1, float y1) {
            writeInstance(x0, y0, FLT_MAX, 0.f, x1, y1);
        }
        void Quadratic(float x0, float y0, float x1, float y1, float x2, float y2) {
            float ax, bx, ay, by, cx, cy, dot, adot, bdot, cdot, cosine, t0, t1, dt;
            ax = x2 - x1, bx = x1 - x0, ay = y2 - y1, by = y1 - y0;
            dot = ax * bx + ay * by, adot = ax * ax + ay * ay, bdot = bx * bx + by * by,
            cosine = dot / sqrt(adot * bdot + 1e-12f);
            if (cosine > 0.7071f) {
                const float tan30 = 0.577350269189626f;
                cx = x2 - x0, cy = y2 - y0, cdot = cx * cx + cy * cy;
                t0 = (cx * bx + cy * by) / cdot - 0.5f;
                t1 = (-cy * bx + cx * by) / cdot;
                dt = copysign(1.f, t0), t0 = fabsf(t0), t1 = fabsf(t1);
                dt *= fminf(t0, t1 / tan30) - t0;
                writeInstance(x0, y0, x1 + dt * cx, y1 + dt * cy, x2, y2);
            } else {
                float a, b, t, s, tx0, tx1, x, ty0, ty1, y;
                a = sqrtf(adot), b = sqrtf(bdot), t = b / (a + b), s = 1.0f - t;
                tx0 = s * x0 + t * x1, tx1 = s * x1 + t * x2, x = s * tx0 + t * tx1;
                ty0 = s * y0 + t * y1, ty1 = s * y1 + t * y2, y = s * ty0 + t * ty1;
                writeInstance(x0, y0, tx0, ty0, x, y);
                writeInstance(x, y, tx1, ty1, x2, y2);
            }
        }
        void EndSubpath(float x0, float y0, float x1, float y1, bool closed) {
            Instance *dst = outlines->base + outlines->end;
            Instance *dst0 = outlines->base + outlines->idx;
            if (dst - dst0 > 0) {
                Instance *first = dst0, *last = dst - 1;
                first->outline.prev = int(closed) * int(last - first), last->outline.next = -first->outline.prev;
                outlines->idx = outlines->end;
                
                if (opaques) {
                    Opaque *opaque0 = opaques->alloc(dst - dst0), *opaque = opaque0;
                    for (Instance *src = dst0; src < dst; src++, opaque++)
                        opaque->iz = iz, opaque->quad = src->outline.quad;
                    if (!closed) {
                        opaque0->iz |= Instance::kPCap;
                        (opaque - 1)->iz |= Instance::kNCap;
                    }
                }
            }
        }
        void writeInstance(float x0, float y0, float x1, float y1, float x2, float y2) {
            Instance *dst = outlines->alloc(1);
            struct Quadratic& quad = dst->outline.quad;
            dst->iz = iz, quad.x0 = x0, quad.y0 = y0, quad.x1 = x1, quad.y1 = y1, quad.x2 = x2, quad.y2 = y2;
            dst->outline.prev = -1, dst->outline.next = 1;
        }
        uint32_t iz;  Row<Instance> *outlines = nullptr;  Row<Opaque> *opaques = nullptr;
    };
    
    struct Stenciler: GeometryWriter {
        Stenciler(const Geometry *g, Bounds device, Transform m, Row<Opaque> *stencils) : device(device), molecule(g->molecules.base), m(m), stencils(stencils) {}
        
        void writeSegment(float x0, float y0, float x1, float y1) {
            Opaque *stencil = stencils->alloc(1);
            struct Quadratic& quad = stencil->quad;
            float cx = molecule->cx(), cy = molecule->cy();
            quad.x0 = m.a * cx + m.c * cy + m.tx;
            quad.y0 = m.b * cx + m.d * cy + m.ty;
            quad.x1 = x0, quad.y1 = y0;
            quad.x2 = x1, quad.y2 = y1;
        }
        void Quadratic(float x0, float y0, float x1, float y1, float x2, float y2) {
            Bounds quad, clip;
            quad.extend(x0, y0), quad.extend(x1, y1), quad.extend(x2, y2);
            clip = quad.intersect(device);
            bool offscreen = clip.lx == clip.ux || clip.ly == clip.uy;
            float ax, ay, a, count, dt, f2x, f1x, f2y, f1y;
            ax = x0 + x2 - x1 - x1, ay = y0 + y2 - y1 - y1, a = quadraticScale * (ax * ax + ay * ay);
            count = offscreen || a < quadraticScale ? 1.f : a < 8.f ? 2.f : 2.f + floorf(sqrtf(sqrtf(a))), dt = 1.f / count;
            ax *= dt * dt, f2x = 2.f * ax, f1x = ax + 2.f * (x1 - x0) * dt, x1 = x0;
            ay *= dt * dt, f2y = 2.f * ay, f1y = ay + 2.f * (y1 - y0) * dt, y1 = y0;
            while (--count) {
                x1 += f1x, f1x += f2x, y1 += f1y, f1y += f2y;
                writeSegment(x0, y0, x1, y1);
                x0 = x1, y0 = y1;
            }
            writeSegment(x0, y0, x2, y2);
        }
        
        void EndSubpath(float x0, float y0, float x1, float y1, bool closed) {
            molecule++;
        }
        Transform m;
        Bounds device, *molecule;
        Row<Opaque> *stencils;
    };
    
    static size_t resizeBuffer(const SceneList& list, Context *contexts, size_t count, size_t *begins, Buffer& buffer) {
        size_t size = buffer.headerSize, sz, i, j, instances;
        for (i = 0; i < count; i++)
            size += contexts[i].opaques.end * sizeof(Opaque);
        
        for (sz = i = 0; i < count; i++)
            sz += contexts[i].texs.end();
        buffer.texCount = sz;
        size += sz * kColorTextureWidth * sizeof(Color);
        
        for (auto& scene: list.scenes)
            size += scene->p16total * sizeof(Point16);
        
        Context *ctx = contexts;   Allocator::Pass *pass;
        for (ctx = contexts, i = 0; i < count; i++, ctx++) {
            for (j = 0; j < ctx->images.end(); j++)
                buffer.images.add(*ctx->images[j]);
            for (instances = 0, pass = ctx->allocator.passes.base, j = 0; j < ctx->allocator.passes.end; j++, pass++)
                instances += pass->count();
            begins[i] = size, size += instances * sizeof(Edge) + (ctx->outlines.end + ctx->blends.end) * sizeof(Instance) + ctx->segments.end * sizeof(Segment) + ctx->stencils.end * sizeof(Opaque);
        }
        return size;
    }
    
    static void writeOpaques(const SceneList& list, Context *contexts, size_t count, size_t *begins, Buffer& buffer) {
        size_t begin = buffer.headerSize, end = begin, i, j, sz;
        for (i = 0; i < count; i++)
            if ((sz = contexts[i].opaques.end * sizeof(Opaque)))
                memcpy(buffer.base + end, contexts[i].opaques.base, sz), end += sz;
        if (begin != end)
            new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::kOpaques, begin, end);
        
        buffer.texStrips = end;
        uint32_t *texIdxs = (uint32_t *)(buffer.base + buffer.texIdxs), texIdx;
        sz = kColorTextureWidth * sizeof(Color);
        for (texIdx = 0, i = 0; i < count; i++)
            for (j = 0; j < contexts[i].texs.end(); j++) {
                auto ref = contexts[i].texs[j];
                texIdxs[ref.iz] = texIdx++;
                memcpy(buffer.base + end, ref.strip, sz), end += sz;
            }
        buffer.p16s = end;
    }
    
    static void writeContextToBuffer(const SceneList& list, Context *ctx, size_t begin, size_t index, size_t contextCount, Buffer& buffer) {
        size_t i, j, count, size, ip, iz, ic, end, instbegin, passsize, stencilBegin = 0;
        {
            auto p16s = (Point16 *)(buffer.base + buffer.p16s);
            size_t p16paths = 0, p16total = 0, m0 = 0, m1 = 0, i0, i1, c0, c1;
            for (auto& scene: list.scenes)
                p16paths += scene->p16entries.end;
            i0 = index * p16paths / contextCount;
            i1 = (index + 1) * p16paths / contextCount;
            for (auto& scene: list.scenes) {
                m1 = m0 + scene->p16entries.end;
                c0 = m0 < i0 ? i0 : m0 > i1 ? i1 : m0;
                c1 = m1 < i0 ? i0 : m1 > i1 ? i1 : m1;
                for (; c0 < c1; c0++) {
                    auto& entry = scene->p16entries.base[c0 - m0];
                    memcpy(p16s + p16total + entry.idx, entry.g->p16s.base, entry.g->p16s.end * sizeof(Point16));
                }
                m0 = m1, p16total += scene->p16total;
            }
        }
        if (ctx->segments.end || ctx->stencils.end) {
            size = ctx->segments.end * sizeof(Segment), end = begin + size;
            ctx->entries.add(Buffer::Entry(Buffer::kSegmentsBase, begin, end));
            memcpy(buffer.base + begin, ctx->segments.base, size), begin = end;
                        
            stencilBegin = begin;
            end = begin + ctx->stencils.end * sizeof(Opaque);
            memcpy(buffer.base + stencilBegin, ctx->stencils.base, end - begin);
            begin = end;
        }
        
        Edge *quadEdge = nullptr, *fastEdge = nullptr, *fastMolecule = nullptr, *fastMolecule0 = nullptr, *quadMolecule = nullptr, *quadMolecule0 = nullptr;
        for (count = ctx->allocator.passes.end, ip = 0; ip < count; ip++) {
            Allocator::Pass *pass = ctx->allocator.passes.base + ip;
            passsize = (ip + 1 < count ? (pass + 1)->idx : ctx->blends.end) - pass->idx;
            instbegin = begin + pass->count() * sizeof(Edge);
            if (pass->count()) {
                ctx->entries.add(Buffer::Entry(Buffer::kInstancesBase, instbegin, 0));
                
                quadEdge = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kQuadEdges] * sizeof(Edge);
                ctx->entries.add(Buffer::Entry(Buffer::kQuadEdges, begin, end)), begin = end;
                
                fastEdge = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kFastEdges] * sizeof(Edge);
                ctx->entries.add(Buffer::Entry(Buffer::kFastEdges, begin, end)), begin = end;
                
                fastMolecule0 = fastMolecule = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kFastMolecules] * sizeof(Edge);
                ctx->entries.add(Buffer::Entry(Buffer::kFastMolecules, begin, end)), begin = end;
                
                quadMolecule0 = quadMolecule = (Edge *)(buffer.base + begin), end = begin + pass->counts[Allocator::kQuadMolecules] * sizeof(Edge);
                ctx->entries.add(Buffer::Entry(Buffer::kQuadMolecules, begin, end)), begin = end;
                assert(begin == instbegin);
            }
            
            Vector<size_t> batchBegins;
            Vector<Blend> batchCommands;
            
            Instance *dst0 = (Instance *)(buffer.base + begin), *dst = dst0;
            for (Blend *inst = ctx->blends.base + pass->idx, *endinst = inst + passsize; inst < endinst; inst++) {
                iz = inst->iz & kPathIndexMask;
                Geometry *g = inst->g;
                
                if (inst->iz & Instance::kOutlines) {
                    memcpy(dst, ctx->outlines.base + inst->data.idx, inst->data.count * sizeof(Instance));
                    dst += inst->data.count;
                } else {
                    dst->iz = inst->iz, dst->quad = inst->quad;
                    ic = dst - dst0, dst++;
                    bool fast = inst->iz & Instance::kFastEdges;
                    
                    bool isImage = (inst->iz & Instance::kIsImage) && ((inst->iz & Instance::kNextImage) || (inst->iz & Instance::kDisableImage));
                    bool isStencil = inst->iz & Instance::kStencil;
                    if (isImage || isStencil) {
                        dst--;
                        batchBegins.add(begin + (dst - dst0) * sizeof(Instance));
                        batchCommands.add(*inst);
                    } else if (inst->iz & Instance::kMolecule) {
                        Edge *molecule = fast ? fastMolecule : quadMolecule;
                        Instance *prev = dst - 1;
                        prev->quad.biid = int(molecule - (fast ? fastMolecule0 : quadMolecule0));
                        size_t molidx = 0;
                        if (fast) {
                            uint8_t *p16cnt = g->p16cnts.base;
                            for (j = 0, size = g->p16s.idx / kFastSegments; j < size; j++, p16cnt++, molecule++) {
                                molidx += (*p16cnt & P16Writer::isMoveTo) && j != 0;
                                molecule->ic = uint32_t(ic), molecule->i0 = *p16cnt & 0xF, molecule->ux = molidx;
                            }
                        } else {
                            Atom *atom = g->atoms.base;
                            for (j = 0, size = g->atoms.end; j < size; j++, atom++, molecule++) {
                                molidx += (atom->i & Atom::isMoveTo) && j != 0;
                                molecule->ic = uint32_t(ic) | ((atom->i & 0xF0000) << 12), molecule->i0 = atom->i & 0xFFFF, molecule->ux = molidx;
                            }
                        }
                        *(fast ? & fastMolecule : & quadMolecule) = molecule;
                    } else if (inst->iz & Instance::kEdge) {
                        Edge *edge = fast ? fastEdge : quadEdge;
                        uint32_t is0, is1, *si = ctx->segmentsIndices.base + inst->data.idx;
                        for (j = 0; j < inst->data.count; j++, edge++) {
                            is0 = si[j];
                            is1 = ++j < inst->data.count ? si[j] : ~0;
                            edge->ic = uint32_t(ic) | ((is0 << 12) & Edge::ue0) | ((is1 << 8) & Edge::ue1);
                            edge->i0 = is0 & 0xFFFF, edge->ux = is1 & 0xFFFF;
                        }
                        *(fast ? & fastEdge : & quadEdge) = edge;
                    }
                }
            }
            
            if ((size = dst - dst0) || batchBegins.end()) {
                end = begin + size * sizeof(Instance);
                size_t i0 = begin, i1;
                for (i = 0; i <= batchBegins.end(); i++, i0 = i1) {
                    i1 = i == batchBegins.end() ? end : batchBegins[i];
                    if (i0 != i1)
                        ctx->entries.add(Buffer::Entry(Buffer::kInstances, i0, i1));
                    if (i != batchBegins.end()) {
                        const Blend& cmd = batchCommands[i];
                        if (cmd.iz & Instance::kStencil) {
                            if (cmd.data.count) {
                                size_t s0 = stencilBegin + cmd.data.idx * sizeof(Opaque);
                                size_t s1 = s0 + cmd.data.count * sizeof(Opaque);
                                ctx->entries.add(Buffer::Entry(Buffer::kStencils, s0, s1));
                                ctx->entries.add(Buffer::Entry(Buffer::kEnableClip, 0, 0));
                            } else if (cmd.data.idx == 0)
                                ctx->entries.add(Buffer::Entry(Buffer::kDisableClip, 0, 0));
                            else
                                ctx->entries.add(Buffer::Entry(Buffer::kEnableClip, 0, 0));
                        } else if (cmd.iz & Instance::kIsImage) {
                            if (cmd.iz & Instance::kNextImage)
                                ctx->entries.add(Buffer::Entry(Buffer::kNextImage, 0, 0));
                            else if (cmd.iz & Instance::kDisableImage)
                                ctx->entries.add(Buffer::Entry(Buffer::kDisableImage, 0, 0));
                        }
                    }
                }
                begin = end;
            }
            ctx->entries.add(Buffer::Entry(Buffer::kDisableClip, 0, 0));
        }
    }
};
typedef Rasterizer Ra;
