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
    static Ra::Range indicesForPoint(Ra::SceneList& list, Ra::Transform view, Ra::Bounds bounds, float px, float py, uint64_t tag = ~0) {
        if (px >= bounds.lx && px < bounds.ux && py >= bounds.ly && py < bounds.uy)
            for (int li = int(list.scenes.size()) - 1; li >= 0; li--) {
                Ra::Scene& scene = list.scenes[li];
                if ((scene.tag & tag) == 0)
                    continue;
                Ra::Transform ctm = view.concat(list.ctms[li]);
                Ra::Bounds sceneclip = list.clips[li];
                for (int si = int(scene.count) - 1; si >= 0; si--) {
                    if (scene.flags->base[si] & Ra::Scene::kInvisible)
                        continue;
                    
                    Ra::Transform inv = sceneclip.intersect(scene.clips.base[si]).quad(ctm).invert();
                    float ux = inv.a * px + inv.c * py + inv.tx, uy = inv.b * px + inv.d * py + inv.ty;
                    
                    if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
                        int winding = pointWinding(scene.paths->base[si].ptr, scene.bnds.base[si], ctm.concat(scene.ctms->base[si]), px, py, scene.widths->base[si], scene.flags->base[si]);
                        bool even = scene.flags->base[si] & Ra::Scene::kFillEvenOdd;
                        if ((even && (winding & 1)) || (!even && winding))
                            return Ra::Range(li, si);
                    }
                }
            }
        return Ra::Range(INT_MAX, INT_MAX);
    }
    struct Counter: Ra::GeometryWriter {
        float dx, dy, dw;  int winding = 0;  uint8_t flags = 0;
        
        void writeSegment(float x0, float y0, float x1, float y1) {
            if (dw) {
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
                writeSegment(x0, y0, x1, y1), x0 = x1, y0 = y1;
            }
            writeSegment(x0, y0, x2, y2);
        }
    };
    
    static int pointWinding(Ra::Geometry *g, Ra::Bounds bounds, Ra::Transform m, float dx, float dy, float w, uint8_t flags) {
        float ws = m.scale(), uw = w < 0.f ? -w / ws : w;
        Counter cntr;  cntr.dx = dx, cntr.dy = dy, cntr.dw = w * (w < 0.f ? -1.f : ws), cntr.flags = flags;
        cntr.quadraticScale = cntr.cubicScale = 1.f;
        Ra::Transform unit = bounds.inset(-uw, -uw).quad(m), inv = unit.invert();
        Ra::Bounds clip = Ra::Bounds(unit);
        float ux = inv.a * dx + inv.c * dy + inv.tx, uy = inv.b * dx + inv.d * dy + inv.ty;
        if (clip.lx < clip.ux && clip.ly < clip.uy && ux >= 0.f && ux <= 1.f && uy >= 0.f && uy <= 1.f) {
            bool polygon = w != 0.f;
            Ra::divideGeometry(g, m, clip, false, polygon, cntr);
        }
        return cntr.winding;
    }
};
