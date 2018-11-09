//
//  RasterizerSVG.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 04/11/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "Rasterizer.hpp"

#define NANOSVG_IMPLEMENTATION    // Expands implementation
#import "nanosvg.h"

struct RasterizerSVG {
    static uint32_t bgraFromPaint(NSVGpaint paint) {
        uint8_t r, g, b, a;
        r = paint.color & 0xFF, g = (paint.color >> 8) & 0xFF, b = (paint.color >> 16) & 0xFF, a = paint.color >> 24;
        uint8_t bgra[4] = { b, g, r, a };
        return *((uint32_t *)bgra);
    }
    
    static Rasterizer::Bounds writePath(NSVGshape *shape, float height, Rasterizer::Path& p) {
        float lx, ly, ux, uy, *pts;
        int i;
        lx = ly = FLT_MAX, ux = uy = -FLT_MAX;
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
            for (pts = path->pts, i = 0; i < path->npts; i++, pts += 2)
                pts[1] = height - pts[1];
            
            for (pts = path->pts, i = 0; i < path->npts; i++, pts += 2) {
                lx = pts[0] < lx ? pts[0] : lx;
                ux = pts[0] > ux ? pts[0] : ux;
                ly = pts[1] < ly ? pts[1] : ly;
                uy = pts[1] > uy ? pts[1] : uy;
            }
            for (pts = path->pts, p.moveTo(pts[0], pts[1]), i = 0; i < path->npts - 1; i += 3, pts += 6)
                p.cubicTo(pts[2], pts[3], pts[4], pts[5], pts[6], pts[7]);
            p.close();
        }
        return Rasterizer::Bounds(lx, ly, ux, uy);
    }
    
    static void writeToScene(const void *bytes, size_t size, Rasterizer::Scene& scene) {
        char *data = (char *)malloc(size + 1);
        memcpy(data, bytes, size);
        data[size] = 0;
        struct NSVGimage* image = data ? nsvgParse(data, "px", 96) : NULL;
        if (image) {
            int limit = 60000;
            for (NSVGshape *shape = image->shapes; shape != NULL && limit; shape = shape->next, limit--) {
                if (shape->fill.type != NSVG_PAINT_NONE) {
                    if (shape->fill.type == NSVG_PAINT_COLOR) {
                        scene.bgras.emplace_back(bgraFromPaint(shape->fill));
                        scene.paths.emplace_back();
                        Rasterizer::Bounds bounds = writePath(shape, image->height, scene.paths.back());
                        scene.bounds.emplace_back(bounds);
                        scene.ctms.emplace_back(1, 0, 0, 1, 0, 0);
                    }
                }
            }
            // Delete
            nsvgDelete(image);
        }
        free(data);
    }
};
