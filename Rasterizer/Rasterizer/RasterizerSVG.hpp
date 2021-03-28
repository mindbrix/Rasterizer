//
//  RasterizerSVG.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 04/11/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"
#import "nanosvg.h"

struct RasterizerSVG {
    static Ra::Colorant colorFromPaint(NSVGpaint paint) {
        if (paint.type == NSVG_PAINT_COLOR)
            return Ra::Colorant((paint.color >> 16) & 0xFF, (paint.color >> 8) & 0xFF, paint.color & 0xFF, paint.color >> 24);
        else
            return Ra::Colorant(0, 0, 0, 64);
    }
    static inline float lengthsq(float x0, float y0, float x1, float y1) {
        return (x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0);
    }
    static void writePathFromShape(NSVGshape *shape, float height, Ra::Path& p) {
        float *pts, dot;  int i;
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
            for (dot = 0.f, i = path->npts - 1; i > 0 && dot < 1e-6f; i--) {
                if ((dot = lengthsq(path->pts[0], path->pts[1], path->pts[i * 2], path->pts[i * 2 + 1])) < 1e-6f)
                    path->pts[i * 2] = path->pts[0], path->pts[i * 2 + 1] = path->pts[1];
            }
            for (pts = path->pts, p.ref->moveTo(pts[0], height - pts[1]), i = 0; i < path->npts - 1; i += 3, pts += 6) {
                if (lengthsq(pts[0], pts[1], pts[6], pts[7]) > 1e-2f)
                    p.ref->cubicTo(pts[2], height - pts[3], pts[4], height - pts[5], pts[6], height - pts[7]);
            }
            if (path->closed)
                p.ref->close();
        }
    }
    static void writeScene(const void *bytes, size_t size, Ra::SceneList& list) {
        char *data = (char *)malloc(size + 1);
        memcpy(data, bytes, size);
        data[size] = 0;
        struct NSVGimage* image = data ? nsvgParse(data, "px", 96) : NULL;
        if (image) {
            Ra::Scene scene;
            if (0) {
                Ra::Path path;
                for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next)
                    if (shape->fill.type != NSVG_PAINT_NONE)
                        writePathFromShape(shape, image->height, path);
                scene.addPath(path, Ra::Transform(), Ra::Colorant(0, 0, 0, 255), 0.f, Ra::Scene::kFillEvenOdd);
            } else {
                for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next) {
                    Ra::Path path;
                    writePathFromShape(shape, image->height, path);
                    if (shape->fill.type != NSVG_PAINT_NONE)
                        scene.addPath(path, Ra::Transform(), colorFromPaint(shape->fill), 0.f, 0);
                    if (shape->stroke.type != NSVG_PAINT_NONE && shape->strokeWidth)
                        scene.addPath(path, Ra::Transform(), colorFromPaint(shape->stroke), shape->strokeWidth, 0);
                }
            }
            list.addScene(scene);
            nsvgDelete(image);
        }
        free(data);
    }
};
