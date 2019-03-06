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
    static int pointWinding(Rasterizer::Path path, Rasterizer::AffineTransform ctm, Rasterizer::AffineTransform view, Rasterizer::Bounds device, float x, float y) {
        int winding = 0;
        if (path.ref->atomsCount) {
            ctm = view.concat(ctm);
            Rasterizer::AffineTransform unit = path.ref->bounds.unit(ctm);
            Rasterizer::Bounds clip = Rasterizer::Bounds(unit).intersect(device);
            if (x >= clip.lx && x < clip.ux && y >= clip.ly && y < clip.uy) {
                Rasterizer::AffineTransform inv = unit.invert();
                float ux = inv.a * x + inv.c * y + inv.tx, uy = inv.b * x + inv.d * y + inv.ty;
                if (ux >= 0.f && ux < 1.f && uy >= 0.f && uy < 1.f) {
                    winding = 1;
                }
            }
        }
        return winding;
    }
};
