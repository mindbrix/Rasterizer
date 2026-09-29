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

#import "Rasterizer.hpp"

struct RasterizerWinding {
    struct Pair {
        Pair() : i0(INT_MAX), i1(INT_MAX) {}
        Pair(size_t i0, size_t i1) : i0(int(i0)), i1(int(i1)) {}
        int i0, i1;
    };
    
    static Ra::Vector<Pair> indicesForRect(const Ra::SceneList& list, Ra::Bounds rect) {
        Ra::Vector<Pair> indices;
        const Ra::Geometry *lastClipPath = nullptr;  size_t lastScene = ~size_t(0);  Ra::Bounds lastClipRect;  bool lastClipTouches = false;
        for (size_t il = 0; il < list.scenes.size(); il++) {
            const Ra::Scene& scene = *list.scenes[il].ptr;
            const Ra::Transform ctm = list.ctms[il].concat(list.ctm);
            const Ra::Bounds& sceneclip = list.clips[il];

            for (size_t is = 0; is < scene.count(); is++) {
                const Ra::Draw& draw = scene.draws[is];
                if (draw.flags & Ra::Draw::kInvisible)
                    continue;
                const bool useClips = list.params.useClips, clipped = useClips && (!sceneclip.isHuge() || !draw.clip.isHuge());
                const Ra::Bounds clipBounds = sceneclip.intersect(draw.clip);
                Ra::Bounds r = rect;
                if (clipped)
                    r = r.intersect(Ra::Bounds(clipBounds.quad(ctm)));
                if (useClips && draw.clipPath.ptr)
                    r = r.intersect(Ra::Bounds(draw.clipPath->bounds.quad(ctm)));
                if (!r.isRect())
                    continue;

                const Ra::Transform m = draw.ctm.concat(ctm);
                const float dw = draw.width * (draw.width <= 0.f ? -1.f : m.scale());
                static const float miterOutset = 0.5f / sqrtf(0.5f * (1.f + float(kMiterLimit)));
                const float outset = dw * (draw.flags & Ra::Draw::kRoundJoin ? 1.f : miterOutset);
                const Ra::Bounds clip = Ra::Bounds(draw.bnds.quad(m)).inset(-outset, -outset);
                if (!clip.intersects(r))
                    continue;

                if (clipped && !Winder::TouchesRect(r, clipBounds, ctm))
                    continue;
                if (useClips && draw.clipPath.ptr) {
                    if (draw.clipPath.ptr != lastClipPath || il != lastScene || memcmp(& r, & lastClipRect, sizeof(r)) != 0) {
                        lastClipPath = draw.clipPath.ptr, lastScene = il, lastClipRect = r;
                        lastClipTouches = Winder::TouchesRect(r, draw.clipPath.ptr, ctm, 0, 0, 0);
                    }
                    if (!lastClipTouches)
                        continue;
                }
                if (r.contains(clip) || Winder::TouchesRect(r, draw.path.ptr, m, dw, outset, draw.flags))
                    indices.add(Pair(il, is));
            }
        }
        return indices;
    }
    
    struct Winder: Ra::GeometryWriter {
        static bool TouchesRect(Ra::Bounds rect, Ra::Geometry *g, Ra::Transform m, float dw, float outset, uint8_t flags) {
            Winder winder;  winder.dw = 0.5f * dw, winder.flags = flags, winder.rect = rect;
            winder.unit = rect.quad(Ra::Transform()).invert();
            winder.applyPath(g, m, rect.inset(-outset, -outset), false, dw == 0.f);
            float cover = fabsf(winder.winding);
            if (dw != 0.f)
                return cover > 1e-6f;
            return winder.crosses || (flags & Ra::Draw::kFillEvenOdd ? 1.f - fabsf(fmodf(cover, 2.f) - 1.f) : cover) > 0.5f;
        }
        
        static bool TouchesRect(Ra::Bounds rect, Ra::Bounds b, Ra::Transform ctm) {
            Winder winder;
            winder.unit = ctm.concat(rect.quad(Ra::Transform()).invert());
            winder.quad(b.lx, b.ly, b.lx, b.uy, b.ux, b.uy, b.ux, b.ly);
            return fabsf(winder.winding) > 1e-6f;
        }
        
        inline static float saturate(float t) {
            return fmaxf(0.f, fminf(1.f, t));
        }
        inline static float awinding(float x0, float y0, float x1, float y1) {
            float w0 = saturate(y0), w1 = saturate(y1), cover = w1 - w0;
            float dx = x1 - x0, dy = y1 - y0, a0 = dx * ((dx > 0.f ? w0 : w1) - y0) - dy * (1.f - x0);
            return saturate(-a0 / fmaf(fabsf(dx), cover, dy)) * cover;
        }
        
        inline float uwinding(float x0, float y0, float x1, float y1) {
            return awinding(
                fmaf(x0, unit.a, fmaf(y0, unit.c, unit.tx)),
                fmaf(x0, unit.b, fmaf(y0, unit.d, unit.ty)),
                fmaf(x1, unit.a, fmaf(y1, unit.c, unit.tx)),
                fmaf(x1, unit.b, fmaf(y1, unit.d, unit.ty))
            );
        }
        inline void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
            winding += fabsf(uwinding(x0, y0, x1, y1) + uwinding(x1, y1, x2, y2) + uwinding(x2, y2, x3, y3) + uwinding(x3, y3, x0, y0));
        }
        void disc(float x, float y) {
            float dx = fmaxf(0.f, fmaxf(rect.lx - x, x - rect.ux)), dy = fmaxf(0.f, fmaxf(rect.ly - y, y - rect.uy));
            if (dx * dx + dy * dy < dw * dw)
                winding += 1.f;
        }
        void cap(float x, float y, float tx, float ty) {
            if (flags & Ra::Draw::kRoundCap)
                disc(x, y);
            else if (flags & Ra::Draw::kSquareCap) {
                float ex = dw * tx, ey = dw * ty;
                quad(x + ey, y - ex, x - ey, y + ex, x - ey + ex, y + ex + ey, x + ey + ex, y - ex + ey);
            }
        }
        void endCap(float x0, float y0, float x1, float y1) {
            float tx = x1 - x0, ty = y1 - y0, rt = 1.f / sqrtf(tx * tx + ty * ty);
            cap(x1, y1, tx * rt, ty * rt);
        }
        void join(float x0, float y0, float x1, float y1, float x2, float y2) {
            float ax = x1 - x0, ay = y1 - y0, bx = x2 - x1, by = y2 - y1, ra, rb, dot, cross;
            ra = 1.f / sqrtf(ax * ax + ay * ay), rb = 1.f / sqrtf(bx * bx + by * by);
            ax *= ra, ay *= ra, bx *= rb, by *= rb, dot = ax * bx + ay * by, cross = ax * by - ay * bx;
            if (dot < kMiterLimit)
                cap(x1, y1, ax, ay), cap(x1, y1, -bx, -by);
            else if (flags & Ra::Draw::kRoundJoin)
                disc(x1, y1);
            else if (cross != 0.f) {
                float s = cross > 0.f ? -dw : dw, nax = -ay * s, nay = ax * s, nbx = -by * s, nby = bx * s, k = 1.f / (1.f + dot);
                quad(x1, y1, x1 + nax, y1 + nay, x1 + (nax + nbx) * k, y1 + (nay + nby) * k, x1 + nbx, y1 + nby);
            }
        }
        void writeSegment(float x0, float y0, float x1, float y1) {
            if (dw == 0) {
                winding += uwinding(x0, y0, x1, y1);
                if (!crosses && (x0 != x1 || y0 != y1))
                    crosses = !(x0 == x1 && (x0 == clip.lx || x0 == clip.ux)) && !(y0 == y1 && (y0 == clip.ly || y0 == clip.uy));
            } else if (x0 != x1 || y0 != y1) {
                float scale = dw / sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)), nx = scale * (y0 - y1), ny = scale * (x1 - x0);
                quad(x0 - nx, y0 - ny, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny);
                if (!hasFirst)
                    fx0 = x0, fy0 = y0, fx1 = x1, fy1 = y1, hasFirst = true;
                else if (px1 == x0 && py1 == y0)
                    join(px0, py0, x0, y0, x1, y1);
                px0 = x0, py0 = y0, px1 = x1, py1 = y1;
            }
        }
        void EndSubpath(float x0, float y0, float x1, float y1, bool closed) {
            if (hasFirst) {
                if (closed) {
                    if (px1 == fx0 && py1 == fy0)
                        join(px0, py0, fx0, fy0, fx1, fy1);
                } else {
                    if (fx0 == x1 && fy0 == y1)
                        endCap(fx1, fy1, fx0, fy0);
                    if (px1 == x0 && py1 == y0)
                        endCap(px0, py0, px1, py1);
                }
            }
            hasFirst = false;
        }
        float dw = 0, winding = 0;  uint8_t flags = 0;  Ra::Transform unit;  Ra::Bounds rect;
        float px0, py0, px1, py1, fx0, fy0, fx1, fy1;  bool hasFirst = false, crosses = false;
    };
};

typedef RasterizerWinding RaWnd;
