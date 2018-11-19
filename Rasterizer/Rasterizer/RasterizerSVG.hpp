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
    
    static void writePath(NSVGshape *shape, float height, Rasterizer::Path& p) {
        float *pts;
        int i;
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
            if (height)
                for (pts = path->pts, i = 0; i < path->npts; i++, pts += 2)
                    pts[1] = height - pts[1];
            
            for (pts = path->pts, p.moveTo(pts[0], pts[1]), i = 0; i < path->npts - 1; i += 3, pts += 6)
                p.cubicTo(pts[2], pts[3], pts[4], pts[5], pts[6], pts[7]);
            if (path->closed)
                p.close();
        }
    }
    
    static void writeScene(const void *bytes, size_t size, RasterizerCoreGraphics::Scene& scene) {
        char *data = (char *)malloc(size + 1);
        memcpy(data, bytes, size);
        data[size] = 0;
        struct NSVGimage* image = data ? nsvgParse(data, "px", 96) : NULL;
        if (image) {
            int limit = 60000;
            for (NSVGshape *shape = image->shapes; shape != NULL && limit; shape = shape->next, limit--) {
                if (shape->fill.type == NSVG_PAINT_COLOR) {
                    scene.bgras.emplace_back(bgraFromPaint(shape->fill));
                    scene.paths.emplace_back();
                    writePath(shape, image->height, scene.paths.back());
                    scene.ctms.emplace_back(1, 0, 0, 1, 0, 0);
                }
                if (shape->stroke.type == NSVG_PAINT_COLOR && shape->strokeWidth) {
                    scene.bgras.emplace_back(bgraFromPaint(shape->stroke));
                    Rasterizer::Path p;
                    if (shape->fill.type == NSVG_PAINT_NONE)
                        writePath(shape, image->height, p);
                    CGMutablePathRef path = CGPathCreateMutable();
                    RasterizerCoreGraphics::writePathToCGPath(shape->fill.type == NSVG_PAINT_NONE ? p : scene.paths.back(), path);
                    CGLineCap cap = shape->strokeLineCap == NSVG_CAP_BUTT ? kCGLineCapButt : shape->strokeLineCap == NSVG_CAP_SQUARE ? kCGLineCapSquare : kCGLineCapRound;
                    CGLineJoin join = shape->strokeLineJoin == NSVG_JOIN_MITER ? kCGLineJoinMiter : shape->strokeLineJoin == NSVG_JOIN_ROUND ? kCGLineJoinRound : kCGLineJoinBevel;
                    CGPathRef stroked = RasterizerCoreGraphics::createStrokedPath(path, shape->strokeWidth, cap, join, shape->miterLimit);
                    scene.paths.emplace_back();
                    RasterizerCoreGraphics::writeCGPathToPath(stroked, scene.paths.back());
                    CGPathRelease(path);
                    CGPathRelease(stroked);
                    scene.ctms.emplace_back(1, 0, 0, 1, 0, 0);
                }
            }
            // Delete
            nsvgDelete(image);
        }
        free(data);
    }
    
    static void writeTestScene(const void *bytes, size_t size, RasterizerCoreGraphics::CGTestScene& testScene) {
        testScene.scene.empty();
        testScene.cgscene.empty();
        writeScene(bytes, size, testScene.scene);
        RasterizerCoreGraphics::writeSceneToCGScene(testScene.scene, testScene.cgscene);
    }
    
};
