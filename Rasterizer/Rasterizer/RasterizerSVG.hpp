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
        float *pts, x, y, w, h, point, limit, cub[8], dx, dy;
        int i, j;
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
            Ra::Bounds b;
            for (pts = path->pts, b.extend(pts[0], pts[1]), i = 0; i < path->npts - 1; i += 3, pts += 6)
                b.extend(pts[2], pts[3]), b.extend(pts[4], pts[5]), b.extend(pts[6], pts[7]);
            w = b.ux - b.lx, h = b.uy - b.ly, point = 1e-2f * (w > h ? w : h), limit = point * point;
            cub[0] = path->pts[0], cub[1] = path->pts[1];
            for (x = path->pts[0], y = path->pts[1], p.ref->moveTo(x, y), i = 0; i < path->npts - 1; i += 3) {
                pts = path->pts + i * 2;
                if (1) {
                    for (j = 0; j < 8; j += 2)
                        if ((pts[j] - x) * (pts[j] - x) + (pts[j + 1] - y) * (pts[j + 1] - y) > limit) {
                            p.ref->cubicTo(pts[2], pts[3], pts[4], pts[5], pts[6], pts[7]);
                            x = pts[6], y = pts[7];
                            break;
                        }
                } else {
                    memcpy(cub + 2, pts + 2, 6 * sizeof(float));
                    for (j = 0; j < 6; j += 2) {
                        dx = cub[j] - cub[j + 2], dy = cub[j + 1] - cub[j + 3];
                        if (dx * dx + dy * dy < limit)
                            cub[j + 2] = cub[j], cub[j + 3] = cub[j + 1];
                    }
                    p.ref->cubicTo(cub[2], cub[3], cub[4], cub[5], cub[6], cub[7]);
                    cub[0] = cub[6], cub[1] = cub[7];
                }
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
            list.addScene(strokes, Ra::Transform(), Ra::Transform::nullclip(), width, false);
            nsvgDelete(image);
        }
        free(data);
    }
};
