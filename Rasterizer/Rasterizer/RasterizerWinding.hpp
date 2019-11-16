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
        float dx, dy, width;  int winding;
        
        static void count(float x0, float y0, float x1, float y1, uint32_t curve, void *info) {
            ((WindingInfo *)info)->count(x0, y0, x1, y1);
        }
        static void countOutline(float x0, float y0, float x1, float y1, uint32_t curve, void *inf) {
            WindingInfo *info = (WindingInfo *)inf;
            if (x0 != x1 || y0 != y1) {
                float dx = x1 - x0, dy = y1 - y0, rl = 1.f / sqrtf(dx * dx + dy * dy);
                float vx = -dy * rl * 0.5f * info->width, vy = dx * rl * 0.5f * info->width;
                info->count(x0 + vx, y0 + vy, x0 - vx, y0 - vy);
                info->count(x0 - vx, y0 - vy, x1 - vx, y1 - vy);
                info->count(x1 - vx, y1 - vy, x1 + vx, y1 + vy);
                info->count(x1 + vx, y1 + vy, x0 + vx, y0 + vy);
            }
        }
    };
    static int pointWinding(Ra::Path& path, Ra::Transform ctm, Ra::Transform inv, Ra::Bounds bounds, float dx, float dy, float width) {
        WindingInfo info(dx, dy, width);
        if (path.ref->types.size() > 2) {
            float ux = inv.a * dx + inv.c * dy + inv.tx, uy = inv.b * dx + inv.d * dy + inv.ty;
            if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
                Ra::Transform unit = path.ref->bounds.unit(ctm);
                Ra::Bounds clip = Ra::Bounds(unit).intersect(bounds);
                if (clip.lx != clip.ux && clip.ly != clip.uy) {
                    inv = unit.invert(), ux = inv.a * dx + inv.c * dy + inv.tx, uy = inv.b * dx + inv.d * dy + inv.ty;
                    if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
                        if (path.ref->types.size()) {
                            if (width)
                                Ra::writePath(path.ref, ctm, clip, false, false, false, WindingInfo::countOutline, Ra::writeQuadratic, Ra::writeCubic, & info);
                            else
                                Ra::writePath(path.ref, ctm, clip, false, true, false, WindingInfo::count, Ra::writeQuadratic, Ra::writeCubic, & info);
                        } 
                    }
                }
            }
        }
        return info.winding;
    }
};
