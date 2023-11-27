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
    static inline Ra::Colorant colorFromSVGColor(int color) {
        return Ra::Colorant((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, color >> 24);
    }
    static Ra::Colorant colorFromPaint(NSVGpaint paint) {
        if (paint.type == NSVG_PAINT_COLOR)
            return colorFromSVGColor(paint.color);
        else
            return colorFromSVGColor(paint.gradient->stops[0].color);
    }
    static void writePathFromShape(NSVGshape *shape, float height, Ra::Path& p) {
        float *pts;  int i;
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
            for (pts = path->pts, p->moveTo(pts[0], height - pts[1]), i = 0; i < path->npts - 1; i += 3, pts += 6)
                p->cubicTo(pts[2], height - pts[3], pts[4], height - pts[5], pts[6], height - pts[7]);
            if (path->closed)
                p->close();
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
