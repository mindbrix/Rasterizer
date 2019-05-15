//
//  RasterizerWinding.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 06/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"

struct RasterizerWinding {
    static Rasterizer::Range indicesForPoint(Rasterizer::SceneList& list, bool even, Rasterizer::Transform view, Rasterizer::Bounds bounds, float dx, float dy) {
        if (dx >= bounds.lx && dx < bounds.ux && dy >= bounds.ly && dy < bounds.uy)
            for (int j = int(list.scenes.size()) - 1; j >= 0; j--) {
                Rasterizer::Transform inv = list.clips[j].invert(), ctm = view.concat(list.ctms[j]);
                Rasterizer::Scene& scene = *list.scenes[j].ref;
                for (int i = int(scene.paths.size()) - 1; i >= 0; i--) {
                    int winding = pointWinding(scene.paths[i], ctm.concat(scene.ctms[i]), inv, bounds, dx, dy);
                    if ((even && (winding & 1)) || winding)
                        return Rasterizer::Range(j, i);
                }
            }
        return Rasterizer::Range(INT_MAX, INT_MAX);
    }
    struct WindingInfo {
        WindingInfo(float dx, float dy) : dx(dx), dy(dy), winding(0) {}
        float dx, dy;
        int winding;
    };
    static int pointWinding(Rasterizer::Path& path, Rasterizer::Transform ctm, Rasterizer::Transform inv, Rasterizer::Bounds bounds, float dx, float dy) {
        WindingInfo info(dx, dy);
        if (path.ref->atomsCount > 2 || path.ref->shapesCount != 0) {
            float ux = inv.a * dx + inv.c * dy + inv.tx, uy = inv.b * dx + inv.d * dy + inv.ty;
            if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
                Rasterizer::Transform unit = path.ref->bounds.unit(ctm);
                Rasterizer::Bounds clip = Rasterizer::Bounds(unit).intersect(bounds);
                if (clip.lx != clip.ux && clip.ly != clip.uy) {
                    inv = unit.invert(), ux = inv.a * dx + inv.c * dy + inv.tx, uy = inv.b * dx + inv.d * dy + inv.ty;
                    if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
                        if (path.ref->atomsCount)
                            Rasterizer::writePath(path, ctm, clip, countWinding, Rasterizer::Info((void *)& info));
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
    static void countWinding(float x0, float y0, float x1, float y1, Rasterizer::Info *info) {
        WindingInfo& winding = *((WindingInfo *)info->info);
        if (winding.dy >= (y0 < y1 ? y0 : y1) && winding.dy < (y0 > y1 ? y0 : y1)) {
            float det = (x1 - x0) * (winding.dy - y0) - (y1 - y0) * (winding.dx - x0);
            if (y0 < y1 && det < 0.f)
                winding.winding++;
            else if (y0 > y1 && det > 0.f)
                winding.winding--;
        }
    }
};
