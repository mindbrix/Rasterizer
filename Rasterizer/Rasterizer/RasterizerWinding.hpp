//
//  RasterizerWinding.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 06/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"

struct RasterizerWinding {
    static Ra::Range indicesForPoint(Ra::SceneList& list, Ra::Transform view, Ra::Bounds bounds, float dx, float dy) {
        if (dx >= bounds.lx && dx < bounds.ux && dy >= bounds.ly && dy < bounds.uy)
            for (int li = int(list.scenes.size()) - 1; li >= 0; li--) {
                Ra::Transform inv = view.concat(list.clips[li]).invert(), ctm = view.concat(list.ctms[li]);
                float ws = sqrtf(fabsf(ctm.det())), w, width;
                
                Ra::Scene& scene = list.scenes[li];
                for (int si = int(scene.count) - 1; si >= 0; si--) {
                    w = scene.widths[si], width = w * (w < 0.f ? -1.f : ws);
                    int winding = pointWinding(scene.paths[si], ctm.concat(scene.ctms[si]), inv, bounds, dx, dy, width);
                    bool even = scene.flags[si] & Ra::Scene::kFillEvenOdd;
                    if ((even && (winding & 1)) || (!even && winding))
                        return Ra::Range(li, si);
                }
            }
        return Ra::Range(INT_MAX, INT_MAX);
    }
    struct WindingInfo {
        WindingInfo(float dx, float dy, float width) : dx(dx), dy(dy), width(width), winding(0) {}
        inline void count(float x0, float y0, float x1, float y1) {
            if (dy >= (y0 < y1 ? y0 : y1) && dy < (y0 > y1 ? y0 : y1)) {
                float det = (x1 - x0) * (dy - y0) - (y1 - y0) * (dx - x0);
                if (y0 < y1 && det < 0.f)
                    winding++;
                else if (y0 > y1 && det > 0.f)
                    winding--;
            }
        }
        inline void distance(float x0, float y0, float x1, float y1) {
            float ax, ay, bx, by, cx, cy, t;
            ax = x1 - x0, ay = y1 - y0, bx = dx - x0, by = dy - y0;
            t = (ax * bx + ay * by) / (ax * ax + ay * ay), t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
            cx = (1.f - t) * x0 + t * x1 - dx, cy = (1.f - t) * y0 + t * y1 - dy;
            if (sqrtf(cx * cx + cy * cy) < 0.5f * width)
                winding = 1;
        }
        float dx, dy, width;  int winding;
        
        static void count(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
            ((WindingInfo *)info)->count(x0, y0, x1, y1);
        }
        static void countOutline(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
            if (x0 != x1 || y0 != y1)
                ((WindingInfo *)info)->distance(x0, y0, x1, y1);
        }
    };
    static int pointWinding(Ra::Path& path, Ra::Transform ctm, Ra::Transform inv, Ra::Bounds bounds, float dx, float dy, float width) {
        WindingInfo info(dx, dy, width);
        if (path->isDrawable) {
            float ux = inv.a * dx + inv.c * dy + inv.tx, uy = inv.b * dx + inv.d * dy + inv.ty;
            if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
                Ra::Transform unit = path.ref->bounds.inset(-width, -width).unit(ctm);
                Ra::Bounds clip = Ra::Bounds(unit).intersect(bounds.inset(-width, -width));
                if (clip.lx != clip.ux && clip.ly != clip.uy) {
                    inv = unit.invert(), ux = inv.a * dx + inv.c * dy + inv.tx, uy = inv.b * dx + inv.d * dy + inv.ty;
                    if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
                        if (path.ref->types.size()) {
                            if (width)
                                Ra::writePath(path.ref, ctm, clip, false, false, false, WindingInfo::countOutline, Ra::writeQuadratic, Ra::writeCubic, & info, 1.f, 1.f);
                            else
                                Ra::writePath(path.ref, ctm, clip, false, true, false, WindingInfo::count, Ra::writeQuadratic, Ra::writeCubic, & info, 1.f, 1.f);
                        } 
                    }
                }
            }
        }
        return info.winding;
    }
};
