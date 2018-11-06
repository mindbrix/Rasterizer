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
    struct Scene {
        void empty() { paths.resize(0), bounds.resize(0), bgras.resize(0); }
        std::vector<uint32_t> bgras;
        std::vector<Rasterizer::Bounds> bounds;
        std::vector<Rasterizer::Path> paths;
    };
    
    static void writeSVGToScene(const void *bytes, size_t size, Scene& scene) {
        char *data = (char *)malloc(size + 1);
        memcpy(data, bytes, size);
        data[size] = 0;
        struct NSVGimage* image = data ? nsvgParse(data, "px", 96) : NULL;
        
        if (image) {
            printf("size: %f x %f\n", image->width, image->height);
            int limit = 60000;
            for (NSVGshape *shape = image->shapes; shape != NULL && limit; shape = shape->next, limit--) {
                if (shape->fill.type != NSVG_PAINT_NONE) {
                    float lx, ly, ux, uy, *pts;
                    uint8_t r, g, b, a;
                    unsigned int rgba;
                    if (shape->fill.type == NSVG_PAINT_COLOR) {
                        rgba = shape->fill.color;
                        r = rgba & 0xFF, g = (rgba >> 8) & 0xFF, b = (rgba >> 16) & 0xFF, a = rgba >> 24;
                        uint8_t bgra[4] = { b, g, r, a };
                        scene.bgras.emplace_back(*((uint32_t *)bgra));
                        scene.paths.emplace_back();
                        Rasterizer::Path& p = scene.paths.back();
                        
                        lx = ly = FLT_MAX, ux = uy = -FLT_MAX;
                        for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
                            for (int i = 0; i < path->npts; i++)
                                path->pts[i * 2 + 1] = image->height - path->pts[i * 2 + 1];
                            
                            pts = path->pts;
                            for (int i = 0; i < path->npts; i++, pts += 2) {
                                lx = pts[0] < lx ? pts[0] : lx;
                                ux = pts[0] > ux ? pts[0] : ux;
                                ly = pts[1] < ly ? pts[1] : ly;
                                uy = pts[1] > uy ? pts[1] : uy;
                            }
                            pts = path->pts;
                            p.moveTo(pts[0], pts[1]);
                            for (int i = 0; i < path->npts - 1; i += 3, pts += 6)
                                p.cubicTo(pts[2], pts[3], pts[4], pts[5], pts[6], pts[7]);
                            p.close();
                        }
                        scene.bounds.emplace_back(lx, ly, ux, uy);
                    }
                }
            }
            // Delete
            nsvgDelete(image);
        }
        free(data);
    }
};
