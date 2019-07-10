//
//  RasterizerWinding.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 06/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"

struct RasterizerWinding {
    static Ra::Range indicesForPoint(Ra::SceneList& list, bool even, Ra::Transform view, Ra::Bounds bounds, float dx, float dy) {
        if (dx >= bounds.lx && dx < bounds.ux && dy >= bounds.ly && dy < bounds.uy)
            for (int li = int(list.scenes.size()) - 1; li >= 0; li--) {
                Ra::Transform inv = view.concat(list.clips[li]).invert(), ctm = view.concat(list.ctms[li]);
                float width = list.widths[li] * (list.widths[li] < 0.f ? -1.f : sqrtf(fabsf(ctm.a * ctm.d - ctm.b * ctm.c)));
                Ra::Scene& scene = *list.scenes[li].ref;
                for (int si = int(scene.paths.size()) - 1; si >= 0; si--) {
                    int winding = pointWinding(scene.paths[si], ctm.concat(scene.ctms[si]), inv, bounds, dx, dy, width);
                    if ((even && (winding & 1)) || winding)
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
        float dx, dy, width;
        int winding;
    };
    static void countWinding(float x0, float y0, float x1, float y1, Ra::Info *info) {
        ((WindingInfo *)info->info)->count(x0, y0, x1, y1);
    }
    static int pointWinding(Ra::Path& path, Ra::Transform ctm, Ra::Transform inv, Ra::Bounds bounds, float dx, float dy, float width) {
        WindingInfo info(dx, dy, width);
        if (path.ref->atomsCount > 2 || path.ref->shapesCount != 0) {
            float ux = inv.a * dx + inv.c * dy + inv.tx, uy = inv.b * dx + inv.d * dy + inv.ty;
            if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
                Ra::Transform unit = path.ref->bounds.unit(ctm);
                Ra::Bounds clip = Ra::Bounds(unit).intersect(bounds);
                if (clip.lx != clip.ux && clip.ly != clip.uy) {
                    inv = unit.invert(), ux = inv.a * dx + inv.c * dy + inv.tx, uy = inv.b * dx + inv.d * dy + inv.ty;
                    if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
                        if (path.ref->atomsCount)
                            Ra::writePath(path, ctm, clip, true, countWinding, Ra::Info((void *)& info));
                        else {
                            for (int i = 0; i < path.ref->shapesCount; i++) {
                                inv = ctm.concat(path.ref->shapes[i]).invert(), ux = inv.a * dx + inv.c * dy + inv.tx, uy = inv.b * dx + inv.d * dy + inv.ty;
                                if (path.ref->circles[i]) {
                                    ux = ux * 2.f - 1.f, uy = uy * 2.f - 1.f;
                                    if (ux * ux + uy * uy < 1.f)
                                        return 1;
                                } else if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f)
                                    return 1;
                            }
                        }
                    }
                }
            }
        }
        return info.winding;
    }
};
