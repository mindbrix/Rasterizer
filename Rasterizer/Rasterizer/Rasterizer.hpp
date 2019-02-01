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
    struct Clip {
        Clip() {}
        Clip(size_t begin, size_t end, AffineTransform _ctm, Bounds _bounds) : begin(begin), end(end), ctm(_bounds.unit(_ctm).invert()), bounds(_bounds.transform(_ctm)) {}
        size_t begin, end;
        AffineTransform ctm;
        Bounds bounds;
    };
    struct Colorant {
        enum Type { kNull = 0, kRect, kCircle };
        Colorant() {}
        Colorant(uint8_t *src) : src0(src[0]), src1(src[1]), src2(src[2]), src3(src[3]), ctm(0.f, 0.f, 0.f, 0.f, 0.f, 0.f), type(kNull)  {}
        Colorant(uint8_t *src, AffineTransform ctm, int type) : src0(src[0]), src1(src[1]), src2(src[2]), src3(src[3]), ctm(ctm), type(type) {}
        uint8_t src0, src1, src2, src3;
        AffineTransform ctm;
        int type;
    };
    struct Sequence {
        struct Atom {
            enum Type { kNull = 0, kMove, kLine, kQuadratic, kCubic, kClose, kCapacity = 15 };
            Atom() { bzero(points, sizeof(points)), bzero(types, sizeof(types)); }
            float       points[30];
            uint8_t     types[8];
        };
        Sequence() : end(Atom::kCapacity), px(0), py(0), bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX), refCount(1) {}
        
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
        size_t refCount, end;
        std::vector<Atom> atoms;
        float px, py;
        Bounds bounds;
    };
    struct Path {
        Path() {
            sequence = new Sequence();
        }
        Path(const Path& other) {
            if (this != & other) {
                this->~Path();
                if ((sequence = other.sequence))
                    sequence->refCount++;
            }
        }
        ~Path() {
            if (sequence && --(sequence->refCount) == 0)
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
        size_t bytes() const { return end * sizeof(T); }
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
        struct Index {
            Index() {}
            Index(size_t iy, size_t idx, size_t begin, size_t end) : iy(uint32_t(iy)), idx(uint32_t(idx)), begin(uint32_t(begin)), end(uint32_t(end)) {}
            uint32_t iy, idx, begin, end;
        };
        struct Cell {
            Cell() {}
            Cell(float lx, float ly, float ux, float uy, float ox, float oy, float cover)
            : lx(lx), ly(ly), ux(ux), uy(uy), ox(ox), oy(oy), cover(cover) {}
            short lx, ly, ux, uy, ox, oy;
            float cover;
        };
        struct Quad {
            enum Type { kNull = 0, kRect, kCircle, kCell };
            Quad() {}
            Quad(float lx, float ly, float ux, float uy, float ox, float oy, size_t iz, float cover)
            : cell(lx, ly, ux, uy, ox, oy, cover), iz((uint32_t)iz | kCell << 24) {}
            Quad(Colorant colorant, size_t iz, int type) : colorant(colorant), iz((uint32_t)iz | type << 24) {}
            union {
                Cell cell;
                Colorant colorant;
            };
            uint32_t iz;
        };
        struct Edge {
            Quad quad;
            Segment segments[kSegmentsCount];
        };
        GPU() : edgeInstances(0) {}
        void empty() {
            edgeInstances = 0, indices.empty(), quadIndices.empty(), quads.empty(), opaques.empty();
        }
        size_t width, height, edgeInstances;
        Allocator allocator;
        Row<Segment::Index> indices;
        Row<Index> quadIndices;
        Row<Quad> quads, opaques;
    };
    struct Buffer {
        struct Entry {
            enum Type { kEdges, kColorants, kShapes, kQuads, kOpaques, kShapeClip };
            Entry() {}
            Entry(Type type, size_t begin, size_t end) : type(type), begin(begin), end(end) {}
            Type type;
            size_t begin, end;
        };
        Pages<uint8_t> data;
        Row<Entry> entries;
        Colorant clearColor;
    };
    struct Context {
        static void writeContextsToBuffer(Context *contexts, size_t count, size_t shapesCount,
                                          uint32_t *bgras,
                                          std::vector<AffineTransform>& ctms,
                                          std::vector<Path>& paths,
                                          const Clip *clips, size_t clipSize,
                                          Buffer& buffer) {
            size_t size, i, j, k, kend, begin, end, qend, q, pathsCount = paths.size();
            size = (shapesCount != 0) * sizeof(AffineTransform) + shapesCount * sizeof(Colorant) + pathsCount * sizeof(Colorant);
            for (i = 0; i < count; i++)
                size += contexts[i].gpu.edgeInstances * sizeof(GPU::Edge) + contexts[i].gpu.quads.bytes() + contexts[i].gpu.opaques.bytes();
            
            buffer.data.alloc(size);
            begin = end = 0;
            
            AffineTransform ctm = { 1e12f, 0.f, 0.f, 1e12f, 5e-11f, 5e-11f };
            if (clips && clipSize)
                ctm = clips->ctm.invert();
            
            end += pathsCount * sizeof(Colorant);
            Colorant *dst = (Colorant *)(buffer.data.base + begin);
            for (size_t idx = 0; idx < pathsCount; idx++)
                new (dst++) Colorant((uint8_t *)& bgras[idx], ctm, Colorant::kRect);
            new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::Entry::kColorants, begin, end);
            begin = end;
        
            Context *ctx;
            size_t opaquesBegin = begin;
            for (ctx = contexts, i = 0; i < count; i++, ctx++) {
                end += ctx->gpu.opaques.bytes();
                if (begin != end) {
                    memcpy(buffer.data.base + begin, ctx->gpu.opaques.base, end - begin);
                    begin = end;
                }
            }
            if (opaquesBegin != end)
                new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::Entry::kOpaques, opaquesBegin, end);
            
            for (ctx = contexts, i = 0; i < count; i++, ctx++) {
                GPU::Index *index = ctx->gpu.quadIndices.base;
                while (ctx->gpu.quads.idx != ctx->gpu.quads.end) {
                    for (qend = ctx->gpu.quads.idx + 1; qend < ctx->gpu.quads.end; qend++)
                        if (ctx->gpu.quads.base[qend].cell.oy == 0 && ctx->gpu.quads.base[qend].cell.ox == 0)
                            break;
                    
                    GPU::Quad *quad = ctx->gpu.quads.base + ctx->gpu.quads.idx;
                    GPU::Edge *dst = (GPU::Edge *)(buffer.data.base + begin);
                    Segment *segments, *ds;   Segment::Index *is;
                    for (q = ctx->gpu.quads.idx; q < qend; q++, quad++) {
                        if (quad->cell.ox != kSolidQuad) {
                            end += (index->end - index->begin + kSegmentsCount - 1) / kSegmentsCount * sizeof(GPU::Edge);
                            segments = ctx->segments[index->iy].base + index->idx;
                            is = ctx->gpu.indices.base + index->begin;
                            for (j = index->begin; j < index->end; j += kSegmentsCount, dst++) {
                                dst->quad = *quad;
                                for (ds = dst->segments, k = j, kend = j + kSegmentsCount; k < index->end && k < kend; k++, is++)
                                    *ds++ = segments[is->i];
                                if (k < kend)
                                    memset(ds, 0, (kend - k) * sizeof(Segment));
                            }
                            index++;
                        }
                    }
                    if (begin != end) {
                        new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::Entry::kEdges, begin, end);
                        begin = end;
                    }
                    end += (qend - ctx->gpu.quads.idx) * sizeof(GPU::Quad);
                    memcpy(buffer.data.base + begin, ctx->gpu.quads.base + ctx->gpu.quads.idx, end - begin);
                    new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::Entry::kQuads, begin, end);
                    begin = end;
                    
                    ctx->gpu.quads.idx = qend;
                }
                ctx->gpu.empty();
                for (j = 0; j < ctx->segments.size(); j++)
                    ctx->segments[j].empty();
            }
            if (shapesCount) {
                end = begin + sizeof(AffineTransform);
                *((AffineTransform *)(buffer.data.base + begin)) = ctm;
                new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::Entry::kShapeClip, begin, end);
                
                begin = end, end = begin + shapesCount * sizeof(Colorant);
                new (buffer.entries.alloc(1)) Buffer::Entry(Buffer::Entry::kShapes, begin, end);
            }
        }
        Context() {}
        void setBitmap(Bitmap bm, Bounds cl) {
            bitmap = bm;
            setDevice(Bounds(0.f, 0.f, bm.width, bm.height));
            clip = clip.intersect(cl.integral());
            deltas.resize((bm.width + 1) * kfh);
            memset(& deltas[0], 0, deltas.size() * sizeof(deltas[0]));
        }
        void setGPU(size_t width, size_t height) {
            bitmap = Bitmap();
            setDevice(Bounds(0.f, 0.f, width, height));
            gpu.width = width, gpu.height = height;
            gpu.allocator.init(width, height);
        }
        void setDevice(Bounds dev) {
            device = clip = dev;
            clips.resize(0);
            size_t size = ceilf((dev.uy - dev.ly) * krfh);
            if (segments.size() != size)
                segments.resize(size);
        }
        void drawPaths(Path *paths, AffineTransform *ctms, bool even, Colorant *colorants, size_t begin, size_t end) {
            if (begin == end)
                return;
            AffineTransform nullclip = { 1e-12f, 0.f, 0.f, 1e-12f, 0.5f, 0.5f };
            AffineTransform clip0 = bitmap.width ? nullclip : colorants->ctm.invert();
            Bounds bounds0 = Bounds(0.f, 0.f, 1.f, 1.f).transform(colorants->ctm).integral().intersect(clip);
            const AffineTransform *ctm = & clip0;
            const Bounds *clipbounds = & bounds0;
            size_t iz;
            AffineTransform *units = (AffineTransform *)malloc((end - begin) * sizeof(AffineTransform)), *un = units;
            AffineTransform *cls = (AffineTransform *)malloc((end - begin) * sizeof(AffineTransform)), *cl = cls;
            Bounds *cbs = (Bounds *)malloc((end - begin) * sizeof(Bounds)), *cb = cbs;
            paths += begin, ctms += begin;
            for (iz = begin; iz < end; iz++, paths++, ctms++, un++)
                *un = paths->sequence->bounds.unit(*ctms);
            paths -= (end - begin), ctms -= (end - begin), un = units;
            for (iz = begin; iz < end; iz++, paths++, ctms++, un++, cl++)
                *cl = (*ctm).concat(*un);
            un = units;
            for (iz = begin; iz < end; iz++, un++, cb++)
                *cb = Bounds(
                     un->tx + (un->a < 0.f ? un->a : 0.f) + (un->c < 0.f ? un->c : 0.f), un->ty + (un->b < 0.f ? un->b : 0.f) + (un->d < 0.f ? un->d : 0.f),
                     un->tx + (un->a > 0.f ? un->a : 0.f) + (un->c > 0.f ? un->c : 0.f), un->ty + (un->b > 0.f ? un->b : 0.f) + (un->d > 0.f ? un->d : 0.f)
                ).integral().intersect(*clipbounds);
            paths -= (end - begin), ctms -= (end - begin), un = units, cl = cls, cb = cbs;
            for (iz = begin; iz < end; iz++, paths++, ctms++, un++, cl++, cb++)
                if (paths->sequence && paths->sequence->bounds.lx != FLT_MAX)
                    if (cb->lx != cb->ux && cb->ly != cb->uy) {
                        Bounds clu(
                            cl->tx + (cl->a < 0.f ? cl->a : 0.f) + (cl->c < 0.f ? cl->c : 0.f), cl->ty + (cl->b < 0.f ? cl->b : 0.f) + (cl->d < 0.f ? cl->d : 0.f),
                            cl->tx + (cl->a > 0.f ? cl->a : 0.f) + (cl->c > 0.f ? cl->c : 0.f), cl->ty + (cl->b > 0.f ? cl->b : 0.f) + (cl->d > 0.f ? cl->d : 0.f));
                        bool hit = clu.lx < 0.f || clu.ux > 1.f || clu.ly < 0.f || clu.uy > 1.f;
                        if (clu.ux >= 0.f && clu.lx < 1.f && clu.uy >= 0.f && clu.ly < 1.f)
                            drawPath(*paths, *ctms, even, & colorants[iz].src0, iz, *cb, hit);
                    }
            free(units), free(cls), free(cbs);
        }
        void drawPath(Path& path, AffineTransform ctm, bool even, uint8_t *src, size_t iz, Bounds clipped, bool hit) {
            if (bitmap.width) {
                float w = clipped.ux - clipped.lx, h = clipped.uy - clipped.ly, stride = w + 1.f;
                if (stride * h < deltas.size()) {
                    writePath(path, AffineTransform(ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx - clipped.lx, ctm.ty - clipped.ly), Bounds(0.f, 0.f, w, h), & deltas[0], stride, nullptr);
                    writeDeltas(& deltas[0], stride, clipped, even, src, & bitmap);
                } else {
                    writePath(path, ctm, clipped, nullptr, 0, & segments[0]);
                    writeSegments(& segments[0], clipped, even, & deltas[0], stride, src, & bitmap);
                }
            } else {
                writePath(path, ctm, clipped, nullptr, 0, & segments[0]);
                writeSegments(& segments[0], clipped, even, src, iz, hit, & gpu);
            }
        }
        void setClips(std::vector<Clip>& cls) {
            for (Clip& cl : cls)
                clips.emplace_back(cl), clips.back().bounds = clips.back().bounds.integral().intersect(clip);
        }
        Bitmap bitmap;
        GPU gpu;
        Bounds device, clip;
        static constexpr float kfh = kFatHeight, krfh = 1.0 / kfh;
        std::vector<Clip> clips;
        std::vector<float> deltas;
        std::vector<Row<Segment>> segments;
    };
    
    static void writePath(Path& path, AffineTransform ctm, Bounds clip, float *deltas, uint32_t stride, Row<Segment> *segments) {
        float sx = FLT_MAX, sy = FLT_MAX, x0 = FLT_MAX, y0 = FLT_MAX, x1, y1, x2, y2, x3, y3;
        bool fs = false, f0 = false, f1, f2, f3;
        for (Sequence::Atom& atom : path.sequence->atoms)
            for (uint8_t index = 0, type = 0xF & atom.types[0]; type != Sequence::Atom::kNull; type = 0xF & (atom.types[index / 2] >> ((index & 1) * 4))) {
                float *p = atom.points + index * 2;
                switch (type) {
                    case Sequence::Atom::kMove:
                        if (sx != FLT_MAX && (sx != x0 || sy != y0)) {
                            if (f0 || fs)
                                writeClippedLine(x0, y0, sx, sy, clip, deltas, stride, segments);
                            else
                                writeLine(x0, y0, sx, sy, deltas, stride, segments);
                        }
                        sx = x0 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, sy = y0 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        fs = f0 = x0 < clip.lx || x0 >= clip.ux || y0 < clip.ly || y0 >= clip.uy;
                        index++;
                        break;
                    case Sequence::Atom::kLine:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        f1 = x1 < clip.lx || x1 >= clip.ux || y1 < clip.ly || y1 >= clip.uy;
                        if (f0 || f1)
                            writeClippedLine(x0, y0, x1, y1, clip, deltas, stride, segments);
                        else
                            writeLine(x0, y0, x1, y1, deltas, stride, segments);
                        x0 = x1, y0 = y1, f0 = f1;
                        index++;
                        break;
                    case Sequence::Atom::kQuadratic:
                        x1 = p[0] * ctm.a + p[1] * ctm.c + ctm.tx, y1 = p[0] * ctm.b + p[1] * ctm.d + ctm.ty;
                        f1 = x1 < clip.lx || x1 >= clip.ux || y1 < clip.ly || y1 >= clip.uy;
                        x2 = p[2] * ctm.a + p[3] * ctm.c + ctm.tx, y2 = p[2] * ctm.b + p[3] * ctm.d + ctm.ty;
                        f2 = x2 < clip.lx || x2 >= clip.ux || y2 < clip.ly || y2 >= clip.uy;
                        if (f0 || f1 || f2)
                            writeClippedQuadratic(x0, y0, x1, y1, x2, y2, clip, deltas, stride, segments);
                        else
                            writeQuadratic(x0, y0, x1, y1, x2, y2, deltas, stride, segments);
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
                            writeClippedCubic(x0, y0, x1, y1, x2, y2, x3, y3, clip, deltas, stride, segments);
                        else
                            writeCubic(x0, y0, x1, y1, x2, y2, x3, y3, deltas, stride, segments);
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
                writeClippedLine(x0, y0, sx, sy, clip, deltas, stride, segments);
            else
                writeLine(x0, y0, sx, sy, deltas, stride, segments);
        }
    }
    static void writeClippedLine(float x0, float y0, float x1, float y1, Bounds clip, float *deltas, uint32_t stride, Row<Segment> *segments) {
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
                        writeLine(sx0, sy0, sx1, sy1, deltas, stride, segments);
                    } else {
                        vx = mx < clip.lx ? clip.lx : clip.ux;
                        writeLine(vx, sy0, vx, sy1, deltas, stride, segments);
                    }
                }
        }
    }
    static void writeLine(float x0, float y0, float x1, float y1, float *deltas, uint32_t stride, Row<Segment> *segments) {
        if (y0 != y1) {
            if (segments) {
                float iy0 = y0 * Context::krfh, iy1 = y1 * Context::krfh;
                if (floorf(iy0) == floorf(iy1))
                    new (segments[size_t(iy0)].alloc(1)) Segment(x0, y0, x1, y1);
                else {
                    float ily, fly, fuy, dx, dy, fy0, fy1, sy0, sy1, t0, t1;
                    if (y0 < y1)
                        ily = floorf(iy0), fly = ily * Context::kfh, fuy = ceilf(iy1) * Context::kfh;
                    else
                        ily = floorf(iy1), fly = ily * Context::kfh, fuy = ceilf(iy0) * Context::kfh;
                    dx = x1 - x0, dy = y1 - y0;
                    for (segments += size_t(ily), fy0 = fly; fy0 < fuy; fy0 = fy1, segments++) {
                        fy1 = fy0 + Context::kfh;
                        sy0 = y0 < fy0 ? fy0 : y0 > fy1 ? fy1 : y0;
                        sy1 = y1 < fy0 ? fy0 : y1 > fy1 ? fy1 : y1;
                        t0 = (sy0 - y0) / dy, t0 = t0 < 0.f ? 0.f : t0 > 1.f ? 1.f : t0;
                        t1 = (sy1 - y0) / dy, t1 = t1 < 0.f ? 0.f : t1 > 1.f ? 1.f : t1;
                        new (segments->alloc(1)) Segment(t0 * dx + x0, sy0, t1 * dx + x0, sy1);
                    }
                }
            } else {
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
    static void writeClippedQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, Bounds clip, float *deltas, uint32_t stride, Row<Segment> *segments) {
        float ly, cly, uy, cuy, lx, ux, A, B, C, ts[8], t, s, x, y, vx, x01, x12, x012, y01, y12, y012, tx01, tx12, ty01, ty12, tx012, ty012;
        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2, cly = ly < clip.ly ? clip.ly : ly > clip.uy ? clip.uy : ly;
        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2, cuy = uy < clip.ly ? clip.ly : uy > clip.uy ? clip.uy : uy;
        if (cly != cuy) {
            lx = x0 < x1 ? x0 : x1, lx = lx < x2 ? lx : x2;
            ux = x0 > x1 ? x0 : x1, ux = ux > x2 ? ux : x2;
            A = y0 + y2 - y1 - y1, B = 2.f * (y1 - y0), C = y0;
            int end = 0;
            if (clip.ly >= ly && clip.ly < uy)
                solveQuadratic(A, B, C - clip.ly, ts[end], ts[end + 1]), end += 2;
            if (clip.uy >= ly && clip.uy < uy)
                solveQuadratic(A, B, C - clip.uy, ts[end], ts[end + 1]), end += 2;
            A = x0 + x2 - x1 - x1, B = 2.f * (x1 - x0), C = x0;
            if (clip.lx >= lx && clip.lx < ux)
                solveQuadratic(A, B, C - clip.lx, ts[end], ts[end + 1]), end += 2;
            if (clip.ux >= lx && clip.ux < ux)
                solveQuadratic(A, B, C - clip.ux, ts[end], ts[end + 1]), end += 2;
            if (end < 8)
                ts[end] = 0.f, ts[end + 1] = 1.f, end += 2;
            std::sort(& ts[0], & ts[end]);
            for (int i = 0; i < end - 1; i++)
                if (ts[i] != ts[i + 1]) {
                    t = (ts[i] + ts[i + 1]) * 0.5f, s = 1.f - t;
                    y = y0 * s * s + y1 * 2.f * s * t + y2 * t * t;
                    if (y >= clip.ly && y < clip.uy) {
                        x = x0 * s * s + x1 * 2.f * s * t + x2 * t * t;
                        bool visible = x >= clip.lx && x < clip.ux;
                        t = ts[i + 1], s = 1.f - t;
                        x01 = x0 * s + x1 * t, x12 = x1 * s + x2 * t, x012 = x01 * s + x12 * t;
                        y01 = y0 * s + y1 * t, y12 = y1 * s + y2 * t, y012 = y01 * s + y12 * t;
                        t = ts[i] / ts[i + 1], s = 1.f - t;
                        ty01 = y0 * s + y01 * t, ty12 = y01 * s + y012 * t, ty012 = ty01 * s + ty12 * t;
                        ty012 = ty012 < clip.ly ? clip.ly : ty012 > clip.uy ? clip.uy : ty012;
                        y012 = y012 < clip.ly ? clip.ly : y012 > clip.uy ? clip.uy : y012;
                        if (visible) {
                            tx01 = x0 * s + x01 * t, tx12 = x01 * s + x012 * t, tx012 = tx01 * s + tx12 * t;
                            tx012 = tx012 < clip.lx ? clip.lx : tx012 > clip.ux ? clip.ux : tx012;
                            x012 = x012 < clip.lx ? clip.lx : x012 > clip.ux ? clip.ux : x012;
                            writeQuadratic(tx012, ty012, tx12, ty12, x012, y012, deltas, stride, segments);
                        } else {
                            vx = x <= clip.lx ? clip.lx : clip.ux;
                            writeLine(vx, ty012, vx, y012, deltas, stride, segments);
                        }
                    }
                }
        }
    }
    static void writeQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, float *deltas, uint32_t stride, Row<Segment> *segments) {
        float ax, ay, count, a, dt, s, t, px0, py0, px1, py1;
        ax = x0 + x2 - x1 - x1, ay = y0 + y2 - y1 - y1, a = ax * ax + ay * ay;
        if (a < 0.1f)
            writeLine(x0, y0, x2, y2, deltas, stride, segments);
        else if (a < 8.f) {
            px0 = (x0 + x2) * 0.25f + x1 * 0.5f, py0 = (y0 + y2) * 0.25f + y1 * 0.5f;
            writeLine(x0, y0, px0, py0, deltas, stride, segments);
            writeLine(px0, py0, x2, y2, deltas, stride, segments);
        } else {
            count = 3.f + floorf(sqrtf(sqrtf(a - 8.f))), dt = 1.f / count, t = 0.f, px0 = x0, py0 = y0;
            while (--count) {
                t += dt, s = 1.f - t, px1 = x0 * s * s + x1 * 2.f * s * t + x2 * t * t, py1 = y0 * s * s + y1 * 2.f * s * t + y2 * t * t;
                writeLine(px0, py0, px1, py1, deltas, stride, segments);
                px0 = px1, py0 = py1;
            }
            writeLine(px0, py0, x2, y2, deltas, stride, segments);
        }
    }
    static void solveCubic(double A, double B, double C, double D, float& t0, float& t1, float& t2) {
        if (fabs(D) < 1e-3)
            solveQuadratic(A, B, C, t0, t1), t2 = FLT_MAX;
        else {
            double a3, p, q, q2, u1, v1, p3, discriminant, mp3, mp33, r, t, cosphi, phi, crtr, sd;
            A /= D, B /= D, C /= D, a3 = A / 3;
            p = (3.0 * B - A * A) / 3.0, p3 = p / 3.0, q = (2 * A * A * A - 9.0 * A * B + 27.0 * C) / 27.0, q2 = q / 2.0;
            discriminant = q2 * q2 + p3 * p3 * p3;
            if (discriminant < 0) {
                mp3 = -p / 3, mp33 = mp3 * mp3 * mp3, r = sqrt(mp33), t = -q / (2 * r), cosphi = t < -1 ? -1 : t > 1 ? 1 : t;
                phi = acos(cosphi), crtr = 2 * copysign(cbrt(fabs(r)), r);
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
    static void writeClippedCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Bounds clip, float *deltas, uint32_t stride, Row<Segment> *segments) {
        float ly, cly, uy, cuy, lx, ux;
        ly = y0 < y1 ? y0 : y1, ly = ly < y2 ? ly : y2, ly = ly < y3 ? ly : y3, cly = ly < clip.ly ? clip.ly : ly > clip.uy ? clip.uy : ly;
        uy = y0 > y1 ? y0 : y1, uy = uy > y2 ? uy : y2, uy = uy > y3 ? uy : y3, cuy = uy < clip.ly ? clip.ly : uy > clip.uy ? clip.uy : uy;
        if (cly != cuy) {
            float A, B, C, D, ts[12], t, s, w0, w1, w2, w3, x, y, vx, x01, x12, x23, x012, x123, x0123, y01, y12, y23, y012, y123, y0123;
            float tx01, tx12, tx23, tx012, tx123, tx0123, ty01, ty12, ty23, ty012, ty123, ty0123;
            lx = x0 < x1 ? x0 : x1, lx = lx < x2 ? lx : x2, lx = lx < x3 ? lx : x3;
            ux = x0 > x1 ? x0 : x1, ux = ux > x2 ? ux : x2, ux = ux > x3 ? ux : x3;
            B = 3.f * (y1 - y0), A = 3.f * (y2 - y1) - B, D = y3 - y0 - B - A, C = y0;
            int end = 0;
            if (clip.ly >= ly && clip.ly < uy)
                solveCubic(A, B, C - clip.ly, D, ts[end], ts[end + 1], ts[end + 2]), end += 3;
            if (clip.uy >= ly && clip.uy < uy)
                solveCubic(A, B, C - clip.uy, D, ts[end], ts[end + 1], ts[end + 2]), end += 3;
            B = 3.f * (x1 - x0), A = 3.f * (x2 - x1) - B, D = x3 - x0 - B - A, C = x0;
            if (clip.lx >= lx && clip.lx < ux)
                solveCubic(A, B, C - clip.lx, D, ts[end], ts[end + 1], ts[end + 2]), end += 3;
            if (clip.ux >= lx && clip.ux < ux)
                solveCubic(A, B, C - clip.ux, D, ts[end], ts[end + 1], ts[end + 2]), end += 3;
            if (end < 12)
                ts[end] = 0.f, ts[end + 1] = 1.f, end += 2;
            std::sort(& ts[0], & ts[end]);
            for (int i = 0; i < end - 1; i++)
                if (ts[i] != ts[i + 1]) {
                    t = (ts[i] + ts[i + 1]) * 0.5f, s = 1.f - t;
                    w0 = s * s * s, w1 = 3.f * s * s * t, w2 = 3.f * s * t * t, w3 = t * t * t;
                    y = y0 * w0 + y1 * w1 + y2 * w2 + y3 * w3;
                    if (y >= clip.ly && y < clip.uy) {
                        x = x0 * w0 + x1 * w1 + x2 * w2 + x3 * w3;
                        bool visible = x >= clip.lx && x < clip.ux;
                        t = ts[i + 1], s = 1.f - t;
                        x01 = x0 * s + x1 * t, x12 = x1 * s + x2 * t, x23 = x2 * s + x3 * t;
                        y01 = y0 * s + y1 * t, y12 = y1 * s + y2 * t, y23 = y2 * s + y3 * t;
                        x012 = x01 * s + x12 * t, x123 = x12 * s + x23 * t;
                        y012 = y01 * s + y12 * t, y123 = y12 * s + y23 * t;
                        x0123 = x012 * s + x123 * t;
                        y0123 = y012 * s + y123 * t;
                        t = ts[i] / ts[i + 1], s = 1.f - t;
                        ty01 = y0 * s + y01 * t, ty12 = y01 * s + y012 * t, ty23 = y012 * s + y0123 * t;
                        ty012 = ty01 * s + ty12 * t, ty123 = ty12 * s + ty23 * t;
                        ty0123 = ty012 * s + ty123 * t;
                        ty0123 = ty0123 < clip.ly ? clip.ly : ty0123 > clip.uy ? clip.uy : ty0123;
                        y0123 = y0123 < clip.ly ? clip.ly : y0123 > clip.uy ? clip.uy : y0123;
                        if (visible) {
                            tx01 = x0 * s + x01 * t, tx12 = x01 * s + x012 * t, tx23 = x012 * s + x0123 * t;
                            tx012 = tx01 * s + tx12 * t, tx123 = tx12 * s + tx23 * t;
                            tx0123 = tx012 * s + tx123 * t;
                            tx0123 = tx0123 < clip.lx ? clip.lx : tx0123 > clip.ux ? clip.ux : tx0123;
                            x0123 = x0123 < clip.lx ? clip.lx : x0123 > clip.ux ? clip.ux : x0123;
                            writeCubic(tx0123, ty0123, tx123, ty123, tx23, ty23, x0123, y0123, deltas, stride, segments);
                        } else {
                            vx = x <= clip.lx ? clip.lx : clip.ux;
                            writeLine(vx, ty0123, vx, y0123, deltas, stride, segments);
                        }
                    }
                }
        }
    }
    static void writeCubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float *deltas, uint32_t stride, Row<Segment> *segments) {
        const float w0 = 8.0 / 27.0, w1 = 4.0 / 9.0, w2 = 2.0 / 9.0, w3 = 1.0 / 27.0;
        float cx, bx, ax, cy, by, ay, count, s, t, a, px0, py0, px1, py1, dt, pw0, pw1, pw2, pw3;
        cx = 3.f * (x1 - x0), bx = 3.f * (x2 - x1) - cx, ax = x3 - x0 - cx - bx;
        cy = 3.f * (y1 - y0), by = 3.f * (y2 - y1) - cy, ay = y3 - y0 - cy - by;
        s = fabsf(ax) + fabsf(bx), t = fabsf(ay) + fabsf(by), a = s * s + t * t;
        if (a < 0.1f)
            writeLine(x0, y0, x3, y3, deltas, stride, segments);
        else if (a < 8.f) {
            px0 = (x0 + x3) * 0.125f + (x1 + x2) * 0.375f, py0 = (y0 + y3) * 0.125f + (y1 + y2) * 0.375f;
            writeLine(x0, y0, px0, py0, deltas, stride, segments);
            writeLine(px0, py0, x3, y3, deltas, stride, segments);
        } else if (a < 16.f) {
            px0 = x0 * w0 + x1 * w1 + x2 * w2 + x3 * w3, py0 = y0 * w0 + y1 * w1 + y2 * w2 + y3 * w3;
            px1 = x0 * w3 + x1 * w2 + x2 * w1 + x3 * w0, py1 = y0 * w3 + y1 * w2 + y2 * w1 + y3 * w0;
            writeLine(x0, y0, px0, py0, deltas, stride, segments);
            writeLine(px0, py0, px1, py1, deltas, stride, segments);
            writeLine(px1, py1, x3, y3, deltas, stride, segments);
        } else {
            count = 4.f + floorf(sqrtf(sqrtf(a - 16.f))), dt = 1.f / count, t = 0.f, px0 = x0, py0 = y0;
            while (--count) {
                t += dt, s = 1.f - t, pw0 = s * s * s, pw1 = 3.f * s * s * t, pw2 = 3.f * s * t * t, pw3 = t * t * t;
                px1 = x0 * pw0 + x1 * pw1 + x2 * pw2 + x3 * pw3, py1 = y0 * pw0 + y1 * pw1 + y2 * pw2 + y3 * pw3;
                writeLine(px0, py0, px1, py1, deltas, stride, segments);
                px0 = px1, py0 = py1;
            }
            writeLine(px0, py0, x3, y3, deltas, stride, segments);
        }
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
                if (clip.ux - clip.lx < 16.f) {
                    for (index = indices.alloc(count), i = 0; i < count; i++, index++)
                        new (index) Segment::Index(0.f, i);
                    gpu->allocator.alloc(clip.ux - clip.lx, ox, oy);
                    new (gpu->quads.alloc(1)) GPU::Quad(clip.lx, ly, clip.ux, uy, ox, oy, iz, 0.f);
                    new (gpu->quadIndices.alloc(1)) GPU::Index(iy, segments->idx, indices.idx, indices.end);
                    gpu->edgeInstances += (indices.end - indices.idx + kSegmentsCount - 1) / kSegmentsCount;
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
                                new (gpu->quads.alloc(1)) GPU::Quad(lx, ly, ux, uy, ox, oy, iz, cover);
                                new (gpu->quadIndices.alloc(1)) GPU::Index(iy, segments->idx, begin, i);
                                gpu->edgeInstances += (i - begin + kSegmentsCount - 1) / kSegmentsCount;
                            }
                            begin = i;
                            if (alphaForCover(winding, even) > 0.998f) {
                                lx = ux, ux = index->x;
                                if (lx != ux) {
                                    if (src[3] == 255 && !hit)
                                        new (gpu->opaques.alloc(1)) GPU::Quad(lx, ly, ux, uy, 0.f, 0.f, iz, 1.f);
                                    else
                                        new (gpu->quads.alloc(1)) GPU::Quad(lx, ly, ux, uy, kSolidQuad, 0.f, iz, 1.f);
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
                        new (gpu->quads.alloc(1)) GPU::Quad(lx, ly, ux, uy, ox, oy, iz, cover);
                        new (gpu->quadIndices.alloc(1)) GPU::Index(iy, segments->idx, begin, i);
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
                    writeLine(segment->x0 - lx, segment->y0 - ly, segment->x1 - lx, segment->y1 - ly, deltas, stride, nullptr);
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
