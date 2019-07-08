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
        float *pts;
        int i;
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
            for (pts = path->pts, p.ref->moveTo(pts[0], pts[1]), i = 0; i < path->npts - 1; i += 3, pts += 6)
                p.ref->cubicTo(pts[2], pts[3], pts[4], pts[5], pts[6], pts[7]);
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
            Ra::Ref<Ra::Scene> fills, strokes;
            float width = 1.f;
            Ra::Transform flip(1, 0, 0, -1, 0, image->height);
            for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next) {
                if (shape->fill.type == NSVG_PAINT_COLOR)
                    fills.ref->addPath(createPathFromShape(shape), flip, colorFromPaint(shape->fill));
                if (shape->stroke.type == NSVG_PAINT_COLOR && shape->strokeWidth) {
                    Ra::Path s = createPathFromShape(shape);
                    if (s.ref->isDrawable) {
                        if (0) {
                            strokes.ref->addPath(s, flip, colorFromPaint(shape->stroke));
                            width = shape->strokeWidth;
                        } else {
                            float w = s.ref->bounds.ux - s.ref->bounds.lx, h = s.ref->bounds.uy - s.ref->bounds.ly, dim = w < h ? w : h;
                            CGMutablePathRef path = CGPathCreateMutable();
                            RasterizerCG::writePathToCGPath(shape->fill.type == NSVG_PAINT_NONE ? s : fills.ref->paths.back(), path);
                            CGPathRef stroked = RasterizerCG::createStrokedPath(path, shape->strokeWidth,
                                                                                shape->strokeLineCap == NSVG_CAP_BUTT ? kCGLineCapButt : shape->strokeLineCap == NSVG_CAP_SQUARE ? kCGLineCapSquare : kCGLineCapRound,
                                                                                shape->strokeLineJoin == NSVG_JOIN_MITER ? kCGLineJoinMiter : shape->strokeLineJoin == NSVG_JOIN_ROUND ? kCGLineJoinRound : kCGLineJoinBevel,
                                                                                shape->miterLimit, shape->strokeWidth > dim ? 1 : 10);
                            fills.ref->addPath(RasterizerCG::createPathFromCGPath(stroked), flip, colorFromPaint(shape->stroke));
                            CGPathRelease(path);
                            CGPathRelease(stroked);
                        }
                    }
                }
            }
            
            list.addScene(fills);
            list.addScene(strokes, Ra::Transform::identity(), Ra::Transform::nullclip(), width, false);
            nsvgDelete(image);
        }
        free(data);
    }
};
