//
//  Copyright 2025 Nigel Timothy Barber
//

#import "Rasterizer.hpp"
#import "nanosvg.h"

struct RasterizerSVG {
    static const bool kWriteOneBigPath = false;
    
    static inline Ra::Colorant colorFromSVGColor(int color) {
        return Ra::Colorant((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, color >> 24);
    }
    static Ra::Colorant colorFromPaint(NSVGpaint paint) {
        if (paint.type == NSVG_PAINT_COLOR)
            return colorFromSVGColor(paint.color);
        else
            return colorFromSVGColor(paint.gradient->stops[0].color);
    }
    static inline float lengthsq(float x0, float y0, float x1, float y1) {
        float dx = x1 - x0, dy = y1 - y0;
        return dx * dx + dy * dy;
    }
    static void writePathFromShape(NSVGshape *shape, float height, Ra::Path& p) {
        constexpr float tolerance = 1e-2f;  float *pts, dot;  int i;
        size_t count = 0;
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next)
            count += path->npts;
        p->prealloc(count / 2);
        
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
            for (dot = 0.f, i = path->npts - 1; i > 0 && dot < tolerance; i--) {
                if ((dot = lengthsq(path->pts[0], path->pts[1], path->pts[i * 2], path->pts[i * 2 + 1])) < tolerance)
                    path->pts[i * 2] = path->pts[0], path->pts[i * 2 + 1] = path->pts[1];
            }
            for (pts = path->pts, p->moveTo(pts[0], height - pts[1]), i = 0; i < path->npts - 1; i += 3, pts += 6) {
                if (lengthsq(pts[0], pts[1], pts[6], pts[7]) > tolerance) {
                    p->cubicTo(pts[2], height - pts[3], pts[4], height - pts[5], pts[6], height - pts[7]);
                }
            }
            if (path->closed)
                p->close();
        }
    }
    static Ra::Scene createScene(const void *data, size_t size) {
        char *terminated = (char *)malloc(size + 1);
        memcpy(terminated, data, size);
        terminated[size] = 0;
        
        Ra::Scene scene;
        struct NSVGimage *image = terminated ? nsvgParse(terminated, "px", 96) : NULL;
        if (image) {
            if (kWriteOneBigPath) {
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
            nsvgDelete(image);
        }
        free(terminated);
        return scene;
    }
};
