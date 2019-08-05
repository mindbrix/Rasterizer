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
    static Ra::Path createPathFromShape(NSVGshape *shape) {
        Ra::Path p;
        float *pts, x, y, dx, dy, dot;
        int i;
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
            for (dot = 0.f, i = path->npts - 1; i > 0 && dot < 1e-6f; i--) {
                dx = path->pts[i * 2] - path->pts[0], dy = path->pts[i * 2 + 1] - path->pts[1];
                if ((dot = dx * dx + dy * dy) < 1e-6f)
                    path->pts[i * 2] = path->pts[0], path->pts[i * 2 + 1] = path->pts[1];
            }
            for (x = path->pts[0], y = path->pts[1], p.ref->moveTo(x, y), i = 0; i < path->npts - 1; i += 3) {
                pts = path->pts + i * 2;
                p.ref->cubicTo(pts[2], pts[3], pts[4], pts[5], pts[6], pts[7]);
            }
            if (path->closed)
                p.ref->close();
        }
        return p;
    }
    static void writeScene(const void *bytes, size_t size, Ra::SceneList& list) {
        char *data = (char *)malloc(size + 1);
        memcpy(data, bytes, size);
        data[size] = 0;
        struct NSVGimage* image = data ? nsvgParse(data, "px", 96) : NULL;
        if (image) {
            Ra::Ref<Ra::Scene> scene;
            Ra::Transform flip(1, 0, 0, -1, 0, image->height);
            for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next) {
                if (shape->fill.type != NSVG_PAINT_NONE) {
                    scene.ref->addPath(createPathFromShape(shape), flip, colorFromPaint(shape->fill), 0.f, false);
                }
                if (shape->stroke.type != NSVG_PAINT_NONE && shape->strokeWidth) {
                    Ra::Path s = createPathFromShape(shape);
                    if (s.ref->isDrawable) {
                        scene.ref->addPath(s, flip, colorFromPaint(shape->stroke), shape->strokeWidth, false);
                    }
                }
            }
            list.addScene(scene, Ra::Transform(), Ra::Transform::nullclip());
            nsvgDelete(image);
        }
        free(data);
    }
};
