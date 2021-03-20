//
//  RasterizerWinding.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 06/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"

struct RasterizerWinding {
    static Ra::Range indicesForPoint(Ra::SceneList& list, Ra::Transform view, Ra::Bounds device, float dx, float dy, uint64_t tag = ~0) {
        if (dx >= device.lx && dx < device.ux && dy >= device.ly && dy < device.uy)
            for (int li = int(list.scenes.size()) - 1; li >= 0; li--) {
                Ra::Scene& scene = list.scenes[li];
                if ((scene.tag & tag) == 0)
                    continue;
                Ra::Transform inv = view.concat(list.clips[li]).invert(), ctm = view.concat(list.ctms[li]);
                float ux = inv.a * dx + inv.c * dy + inv.tx, uy = inv.b * dx + inv.d * dy + inv.ty;
                if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
                    for (int si = int(scene.count) - 1; si >= 0; si--) {
                        if (scene.flags[si] & Ra::Scene::kInvisible)
                            continue;
                        int winding = pointWinding(scene.paths[si], scene.bnds[si], ctm.concat(scene.ctms[si]), device, dx, dy, scene.widths[si]);
                        bool even = scene.flags[si] & Ra::Scene::kFillEvenOdd;
                        if ((even && (winding & 1)) || (!even && winding))
                            return Ra::Range(li, si);
                    }
                }
            }
        return Ra::Range(INT_MAX, INT_MAX);
    }
    struct Counter {
        float dx, dy, dw;  int winding = 0;
    };
    static void count(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
        Counter *cntr = (Counter *)info;
        if (cntr->dy >= (y0 < y1 ? y0 : y1) && cntr->dy < (y0 > y1 ? y0 : y1)) {
            float det = (x1 - x0) * (cntr->dy - y0) - (y1 - y0) * (cntr->dx - x0);
            if (y0 < y1 && det < 0.f)
                cntr->winding++;
            else if (y0 > y1 && det > 0.f)
                cntr->winding--;
        }
    }
    static void countOutline(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
        if (x0 != x1 || y0 != y1) {
            Counter *cntr = (Counter *)info;
            float ax, ay, bx, by, cx, cy, t, s;
            ax = x1 - x0, ay = y1 - y0, bx = cntr->dx - x0, by = cntr->dy - y0;
            t = (ax * bx + ay * by) / (ax * ax + ay * ay), t = t < 0.f ? 0.f : t > 1.f ? 1.f : t, s = 1.f - t;
            cx = s * x0 + t * x1 - cntr->dx, cy = s * y0 + t * y1 - cntr->dy;
            if (sqrtf(cx * cx + cy * cy) < 0.5f * cntr->dw)
                cntr->winding = 1;
        }
    }
    static int pointWinding(Ra::Path& path, Ra::Bounds bounds, Ra::Transform ctm, Ra::Bounds device, float dx, float dy, float w) {
        float ws = sqrtf(fabsf(ctm.det())), uw = w < 0.f ? -w / ws : w;
        Counter cntr;  cntr.dx = dx, cntr.dy = dy, cntr.dw = w * (w < 0.f ? -1.f : ws);
        Ra::Transform unit = bounds.inset(-uw, -uw).unit(ctm), inv = unit.invert();
        Ra::Bounds clip = Ra::Bounds(unit).intersect(device);
        float ux = inv.a * dx + inv.c * dy + inv.tx, uy = inv.b * dx + inv.d * dy + inv.ty;
        if (clip.lx != clip.ux && clip.ly != clip.uy && ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
            if (w)
                Ra::divideGeometry(path.ref, ctm, clip, false, false, false, & cntr, countOutline, Ra::divideQuadratic, 1.f, Ra::divideCubic, 1.f);
            else
                Ra::divideGeometry(path.ref, ctm, clip, false, true, false, & cntr, count, Ra::divideQuadratic, 1.f, Ra::divideCubic, 1.f);
        }
        return cntr.winding;
    }
};
