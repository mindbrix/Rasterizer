//
//  RasterizerScene.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 06/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//

#import "Rasterizer.hpp"

struct RasterizerScene {
    struct Scene {
        void empty() { ctms.resize(0), paths.resize(0), bgras.resize(0); }
        void addPath(Rasterizer::Path path, Rasterizer::AffineTransform ctm, uint8_t *bgra) {
            bgras.emplace_back(*((uint32_t *)bgra));
            paths.emplace_back(path);
            ctms.emplace_back(ctm);
        }
        std::vector<uint32_t> bgras;
        std::vector<Rasterizer::AffineTransform> ctms;
        std::vector<Rasterizer::Path> paths;
    };
    
    static size_t pathIndexForPoint(Rasterizer::Path *paths, Rasterizer::AffineTransform *ctms, bool even, Rasterizer::Colorant *colors, Rasterizer::AffineTransform view, Rasterizer::Bounds bounds, size_t begin, size_t end, float dx, float dy) {
        for (int i = int(end) - 1; i >= int(begin); i--) {
            int winding = pointWinding(paths[i], ctms[i], view, colors[i].ctm, bounds, dx, dy);
            if ((even && (winding & 1)) || winding)
                return i;
        }
        return INT_MAX;
    }
    static int pointWinding(Rasterizer::Path path, Rasterizer::AffineTransform ctm, Rasterizer::AffineTransform view, Rasterizer::AffineTransform device, Rasterizer::Bounds bounds, float x, float y) {
        int winding = 0;
        if (path.ref->atomsCount) {
            Rasterizer::AffineTransform inv, m, unit;
            Rasterizer::Bounds clip;
            float ux, uy;
            inv = device.invert(), ux = inv.a * x + inv.c * y + inv.tx, uy = inv.b * x + inv.d * y + inv.ty;
            if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
                m = view.concat(ctm), unit = path.ref->bounds.unit(m);
                clip = Rasterizer::Bounds(unit).intersect(bounds);
                if (clip.lx != clip.ux && clip.ly != clip.uy) {
                    inv = unit.invert(), ux = inv.a * x + inv.c * y + inv.tx, uy = inv.b * x + inv.d * y + inv.ty;
                    if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
                        winding = 1;
                    }
                }
            }
        }
        return winding;
    }
};
