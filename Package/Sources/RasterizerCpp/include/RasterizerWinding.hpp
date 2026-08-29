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
    struct IndexPair {
        IndexPair() : i0(INT_MAX), i1(INT_MAX) {}
        IndexPair(size_t i0, size_t i1) : i0(int(i0)), i1(int(i1)) {}
        int i0, i1;
    };
    
    static IndexPair indicesForPoint(Ra::SceneList& list, Ra::Bounds bounds, float px, float py) {
        if (px >= bounds.lx && px < bounds.ux && py >= bounds.ly && py < bounds.uy)
            for (int il = int(list.scenes.size()) - 1; il >= 0; il--) {
                Ra::Scene& scene = *list.scenes[il].ptr;
                Ra::Transform ctm = list.ctms[il].concat(list.ctm), inv;
                Ra::Bounds sceneclip = list.clips[il], lastClip;
                for (int is = int(scene.count()) - 1; is >= 0; is--) {
                    Ra::Draw& draw = scene.draws[is];
                    if (memcmp(& draw.clip, & lastClip, sizeof(Ra::Bounds)) != 0) {
                        lastClip = draw.clip;
                        inv = sceneclip.intersect(draw.clip).quad(ctm).invert();
                    }
                    float ux = inv.a * px + inv.c * py + inv.tx, uy = inv.b * px + inv.d * py + inv.ty;
                    bool inClipRect = !list.params.useClips || fmaxf(fabsf(ux - 0.5f), fabsf(uy - 0.5f)) <= 0.5f;
                    if (inClipRect) {
                        bool inside = pointInside(px, py, draw.path.ptr, draw.path->bounds, draw.ctm.concat(ctm), draw.width, draw.flags);
                        if (inside)
                            return IndexPair(il, is);
                    }
                }
            }
        return IndexPair();
    }
    
    static bool pointInside(float px, float py, Ra::Geometry *g, Ra::Bounds bounds, Ra::Transform m, float w, uint8_t flags) {
        float ws = m.scale(), uw = w < 0.f ? -w / ws : w;
        Ra::Transform u = bounds.inset(-uw, -uw).quad(m);
        float vx = px - u.tx, vy = py - u.ty;
        float t0 = (vx * u.a + vy * u.b) / (u.a * u.a + u.b * u.b);
        float t1 = (vx * u.c + vy * u.d) / (u.c * u.c + u.d * u.d);
        bool inBounds = fmaxf(fabsf(t0 - 0.5f), fabsf(t1 - 0.5f)) <= 0.5f;
        if (!inBounds)
            return false;
        
        Counter cntr;  cntr.dx = px, cntr.dy = py, cntr.dw = w * (w < 0.f ? -1.f : ws), cntr.flags = flags;
        cntr.quadraticScale = cntr.cubicScale = 1.f;
        Ra::Bounds clip = Ra::Bounds(u);
        bool polygon = w == 0.f;
        Ra::applyPath(g, m, clip, false, polygon, cntr);
        int mask = flags & Ra::Scene::kFillEvenOdd ? 1 : ~0;
        return cntr.winding & mask;
    }
    
    struct Counter: Ra::GeometryWriter {
        float dx, dy, dw;  int winding = 0;  uint8_t flags = 0;
        
        void writeSegment(float x0, float y0, float x1, float y1) {
            if (dw && winding == 0) {
                if (x0 != x1 || y0 != y1) {
                    float ax, ay, adot, len, bx, by, cx, cy, t, s, sx, sy, cap;
                    bool square = flags & Ra::Scene::kSquareCap;
                    bool round = flags & Ra::Scene::kRoundCap;
                    cap = square ? 0.5f * dw : 0.f;
                    ax = x1 - x0, ay = y1 - y0, adot = ax * ax + ay * ay, len = sqrtf(adot), bx = dx - x0, by = dy - y0;
                    sx = (ax * bx + ay * by) / len, sy = (ax * by - ay * bx) / len;
                    t = (ax * bx + ay * by) / adot, t = fmaxf(0.f, fminf(1.f, t)), s = 1.f - t;
                    cx = s * x0 + t * x1 - dx, cy = s * y0 + t * y1 - dy;
                    if (round && sqrtf(cx * cx + cy * cy) < 0.5f * dw)
                        winding = 1;
                    else if (sx > -cap && sx < len + cap && fabsf(sy) < 0.5f * dw)
                        winding = 1;
                }
            } else {
                if (dy >= (y0 < y1 ? y0 : y1) && dy < (y0 > y1 ? y0 : y1)) {
                    float det = (x1 - x0) * (dy - y0) - (y1 - y0) * (dx - x0);
                    if (y0 < y1 && det < 0.f)
                        winding++;
                    else if (y0 > y1 && det > 0.f)
                        winding--;
                }
            }
        }
        void Quadratic(float x0, float y0, float x1, float y1, float x2, float y2) {
            float ax, ay, a, count, dt, f2x, f1x, f2y, f1y;
            ax = x0 + x2 - x1 - x1, ay = y0 + y2 - y1 - y1, a = quadraticScale * (ax * ax + ay * ay);
            count = a < quadraticScale ? 1.f : a < 8.f ? 2.f : 2.f + floorf(sqrtf(sqrtf(a))), dt = 1.f / count;
            ax *= dt * dt, f2x = 2.f * ax, f1x = ax + 2.f * (x1 - x0) * dt, x1 = x0;
            ay *= dt * dt, f2y = 2.f * ay, f1y = ay + 2.f * (y1 - y0) * dt, y1 = y0;
            while (--count) {
                x1 += f1x, f1x += f2x, y1 += f1y, f1y += f2y;
                writeSegment(x0, y0, x1, y1);
                x0 = x1, y0 = y1;
            }
            writeSegment(x0, y0, x2, y2);
        }
    };
};

typedef RasterizerWinding RaWnd;
