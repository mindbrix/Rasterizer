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
                uint32_t ip, lastip = ~0;
                Ra::Scene& scene = list.scenes[li];
                if ((scene.tag & tag) == 0)
                    continue;
                Ra::Transform ctm = view.concat(list.ctms[li]), nullinv = Ra::Bounds::huge().unit(Ra::Transform()).invert(), inv = nullinv;
                
                for (int si = int(scene.count) - 1; si >= 0; si--) {
                    if (scene.flags->base[si] & Ra::Scene::kInvisible)
                        continue;
                    ip = scene.clipCache->ips.base[si];
                    if (ip != lastip) {
                        lastip = ip;
                        Ra::Bounds *pclip = ip ? scene.clipCache->entryAt(si) : nullptr;
                        if (pclip)
                            inv = pclip->unit(ctm).invert();
                        else
                            inv = nullinv;
                    }
                    float ux = inv.a * dx + inv.c * dy + inv.tx, uy = inv.b * dx + inv.d * dy + inv.ty;
                    if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
                        int winding = pointWinding(scene.cache->entryAt(si)->g, scene.bnds->base[si], ctm.concat(scene.ctms->base[si]), device, dx, dy, scene.widths->base[si], scene.flags->base[si]);
                        bool even = scene.flags->base[si] & Ra::Scene::kFillEvenOdd;
                        if ((even && (winding & 1)) || (!even && winding))
                            return Ra::Range(li, si);
                    }
                }
            }
        return Ra::Range(INT_MAX, INT_MAX);
    }
    struct Counter {
        float dx, dy, dw;  int winding = 0;  uint8_t flags = 0;
    };
    static void divideQuadratic(float x0, float y0, float x1, float y1, float x2, float y2, Ra::SegmentFunction function, void *info, float s) {
        float ax, ay, a, count, dt, f2x, f1x, f2y, f1y;
        ax = x0 + x2 - x1 - x1, ay = y0 + y2 - y1 - y1, a = s * (ax * ax + ay * ay);
        count = a < s ? 1.f : a < 8.f ? 2.f : 2.f + floorf(sqrtf(sqrtf(a))), dt = 1.f / count;
        ax *= dt * dt, f2x = 2.f * ax, f1x = ax + 2.f * (x1 - x0) * dt, x1 = x0;
        ay *= dt * dt, f2y = 2.f * ay, f1y = ay + 2.f * (y1 - y0) * dt, y1 = y0;
        while (--count) {
            x1 += f1x, f1x += f2x, y1 += f1y, f1y += f2y;
            (*function)(x0, y0, x1, y1, 1, info), x0 = x1, y0 = y1;
        }
        (*function)(x0, y0, x2, y2, dt == 1.f ? 0 : 2, info);
    }
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
            float ax, ay, adot, len, bx, by, cx, cy, t, s, sx, sy, cap;
            bool square = cntr->flags & Ra::Scene::kSquareCap;
            bool round = cntr->flags & Ra::Scene::kRoundCap;
            cap = square ? 0.5f * cntr->dw : 0.f;
            ax = x1 - x0, ay = y1 - y0, adot = ax * ax + ay * ay, len = sqrtf(adot), bx = cntr->dx - x0, by = cntr->dy - y0;
            sx = (ax * bx + ay * by) / len, sy = (ax * by - ay * bx) / len;
            t = (ax * bx + ay * by) / adot, t = t < 0.f ? 0.f : t > 1.f ? 1.f : t, s = 1.f - t;
            cx = s * x0 + t * x1 - cntr->dx, cy = s * y0 + t * y1 - cntr->dy;
            if (round && sqrtf(cx * cx + cy * cy) < 0.5f * cntr->dw)
                cntr->winding = 1;
            else if (sx > -cap && sx < len + cap && fabsf(sy) < 0.5f * cntr->dw)
                cntr->winding = 1;
        }
    }
    static int pointWinding(Ra::Geometry *g, Ra::Bounds bounds, Ra::Transform m, Ra::Bounds device, float dx, float dy, float w, uint8_t flags) {
        float ws = m.scale(), uw = w < 0.f ? -w / ws : w;
        Counter cntr;  cntr.dx = dx, cntr.dy = dy, cntr.dw = w * (w < 0.f ? -1.f : ws), cntr.flags = flags;
        Ra::Transform unit = bounds.inset(-uw, -uw).unit(m), inv = unit.invert();
        Ra::Bounds clip = Ra::Bounds(unit);
        float ux = inv.a * dx + inv.c * dy + inv.tx, uy = inv.b * dx + inv.d * dy + inv.ty;
        if (clip.lx < clip.ux && clip.ly < clip.uy && ux >= 0.f && ux <= 1.f && uy >= 0.f && uy <= 1.f) {
            if (w)
                Ra::divideGeometry(g, m, clip, false, false, false, & cntr, countOutline, divideQuadratic, 1.f, Ra::divideCubic, 1.f);
            else
                Ra::divideGeometry(g, m, clip, false, true, false, & cntr, count, divideQuadratic, 1.f, Ra::divideCubic, 1.f);
        }
        return cntr.winding;
    }
};
