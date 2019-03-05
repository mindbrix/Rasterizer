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
        if (paint.type == NSVG_PAINT_COLOR)
            r = paint.color & 0xFF, g = (paint.color >> 8) & 0xFF, b = (paint.color >> 16) & 0xFF, a = paint.color >> 24;
        else
            r = 0, g = 0, b = 0, a = 64;
        uint8_t bgra[4] = { b, g, r, a };
        return *((uint32_t *)bgra);
    }
    
    static void writePath(NSVGshape *shape, Rasterizer::Path p) {
        float *pts;
        int i;
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
            for (pts = path->pts, p.ref->moveTo(pts[0], pts[1]), i = 0; i < path->npts - 1; i += 3, pts += 6)
                p.ref->cubicTo(pts[2], pts[3], pts[4], pts[5], pts[6], pts[7]);
            if (path->closed)
                p.ref->close();
        }
    }
    static Rasterizer::Path createPhyllotaxisPath(size_t count) {
        Rasterizer::Path shapes;
        shapes.ref->addShapes(count);
        Rasterizer::AffineTransform *dst = shapes.ref->shapes;
        const float sine = 0.675490294261524f, cosine = -0.73736887807832f;
        float vx = 1.f, vy = 0.f, x, y, s;
        for (int i = 0; i < count; i++, dst++) {
            shapes.ref->circles[i] = i & 1;
            s = sqrtf(i);
            new (dst) Rasterizer::AffineTransform(1.f, 0.f, 0.f, 1.f, s * vx - 0.5f, s * vy - 0.5f);
            x = vx * cosine + vy * -sine, y = vx * sine + vy * cosine;
            vx = x, vy = y;
        }
        return shapes;
    }
    static void writeScene(const void *bytes, size_t size, RasterizerCoreGraphics::Scene& scene) {
        char *data = (char *)malloc(size + 1);
        memcpy(data, bytes, size);
        data[size] = 0;
        struct NSVGimage* image = data ? nsvgParse(data, "px", 96) : NULL;
        if (image) {
            int limit = 600000;
            for (NSVGshape *shape = image->shapes; shape != NULL && limit; shape = shape->next, limit--) {
                if (shape->fill.type == NSVG_PAINT_COLOR) {
                    scene.bgras.emplace_back(bgraFromPaint(shape->fill));
                    scene.paths.emplace_back();
                    writePath(shape, scene.paths.back());
                    scene.ctms.emplace_back(1, 0, 0, -1, 0, image->height);
                }
                if (shape->stroke.type == NSVG_PAINT_COLOR && shape->strokeWidth) {
                    scene.bgras.emplace_back(bgraFromPaint(shape->stroke));
                    
                    Rasterizer::Path s;
                    writePath(shape, s);
                    float w = s.ref->bounds.ux - s.ref->bounds.lx, h = s.ref->bounds.uy - s.ref->bounds.ly, dim = w < h ? w : h;
                    CGMutablePathRef path = CGPathCreateMutable();
                    RasterizerCoreGraphics::writePathToCGPath(shape->fill.type == NSVG_PAINT_NONE ? s : scene.paths.back(), path);
                    CGLineCap cap = shape->strokeLineCap == NSVG_CAP_BUTT ? kCGLineCapButt : shape->strokeLineCap == NSVG_CAP_SQUARE ? kCGLineCapSquare : kCGLineCapRound;
                    CGLineJoin join = shape->strokeLineJoin == NSVG_JOIN_MITER ? kCGLineJoinMiter : shape->strokeLineJoin == NSVG_JOIN_ROUND ? kCGLineJoinRound : kCGLineJoinBevel;
                    CGPathRef stroked = RasterizerCoreGraphics::createStrokedPath(path, shape->strokeWidth, cap, join, shape->miterLimit, shape->strokeWidth > dim ? 1 : 10);
                    
                    scene.paths.emplace_back();
                    RasterizerCoreGraphics::writeCGPathToPath(stroked, scene.paths.back());
                    CGPathRelease(path);
                    CGPathRelease(stroked);
                    scene.ctms.emplace_back(1, 0, 0, -1, 0, image->height);
                }
            }
            // Delete
            nsvgDelete(image);
            
            uint8_t bgra[4] = { 0, 0, 0, 255 };
            if (0) {
                Rasterizer::Path shape;
                shape.ref->addBounds(Rasterizer::Bounds(100.5, 100.5, 199.5, 199.5));
                scene.addPath(shape, Rasterizer::AffineTransform(1.f, 0.f, 0.f, 1.f, 0.f, 0.f), bgra);
            }
            if (0) {
                scene.addPath(createPhyllotaxisPath(100000), Rasterizer::AffineTransform(1.f, 0.f, 0.f, 1.f, 0.f, 0.f), bgra);
            }
        }
        free(data);
    }
};
