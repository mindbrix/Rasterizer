//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//
//  This software is provided 'as-is', without any express or implied
//  warranty. In no event will the authors be held liable for any damages
//  arising from the use of this software.
//
//  Permission is granted to anyone to use this software for personal use
//  (for a commercial licence please contact the author), and to alter it and
//  redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
//

#import "Rasterizer.hpp"
#import "nanosvg.h"

struct RasterizerSVG {
    static const bool kWriteOneBigPath = false;
    
    static Ra::Transform addSvgDataToScene(const void *data, size_t size, Ra::SceneRef& scene) {
        char *terminated = (char *)malloc(size + 1);
        memcpy(terminated, data, size);
        terminated[size] = 0;
        struct NSVGimage *image = nsvgParse(terminated, nullptr, 0);
        addSvgImageToScene(image, scene);
        Ra::Transform ctm = Ra::Transform(1, 0, 0, -1, 0, image->height);
        nsvgDelete(image);
        free(terminated);
        return ctm;
    }
    
    static void addSvgImageToScene(NSVGimage *image, Ra::SceneRef& scene) {
        if (image) {
            if (kWriteOneBigPath) {
                Ra::Path path;
                for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next)
                    if (shape->fill.type != NSVG_PAINT_NONE)
                        writePathFromShape(shape, path);
                scene->addPath(path, Ra::Transform(), Ra::Color(), 0.f, Ra::Scene::kFillEvenOdd);
            } else {
                for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next) {
                    Ra::Path path;
                    writePathFromShape(shape, path);
                    Ra::Transform ctm;
                    if (shape->fill.type != NSVG_PAINT_NONE) {
                        int flags = shape->fillRule == NSVG_FILLRULE_EVENODD ? Ra::Scene::kFillEvenOdd : 0;
                        scene->addPath(path, ctm, colorFromPaint(shape->fill), 0.f, flags);
                    }
                    if (shape->stroke.type != NSVG_PAINT_NONE && shape->strokeWidth) {
                        char cap = shape->strokeLineCap;
                        int flags = cap == NSVG_CAP_ROUND ? Ra::Scene::kRoundCap : cap == NSVG_CAP_SQUARE ? Ra::Scene::kSquareCap : 0;
                        char join = shape->strokeLineJoin;
                        flags |= join == NSVG_JOIN_ROUND ? Ra::Scene::kRoundJoin : 0;
                        scene->addPath(path, ctm, colorFromPaint(shape->stroke), shape->strokeWidth, flags);
                    }
                }
            }
        }
    }
    
    static void writePathFromShape(NSVGshape *shape, Ra::Path& p) {
        float *pts, dot, tolerance = 1e-6f;
        size_t i, count = 0;
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next)
            count += path->npts;
        p->prealloc(count / 2);
        
        for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
            for (dot = 0.f, i = path->npts - 1; i > 0 && dot < tolerance; i--)
                if ((dot = lengthSquared(path->pts, & path->pts[i * 2])) < tolerance)
                    path->pts[i * 2] = path->pts[0], path->pts[i * 2 + 1] = path->pts[1];

            for (pts = path->pts, p->moveTo(pts[0], pts[1]), i = 0; i < path->npts - 1; i += 3, pts += 6)
                if (lengthSquared(pts, pts + 6) > tolerance)
                    p->cubicTo(pts[2], pts[3], pts[4], pts[5], pts[6], pts[7]);
            
            if (path->closed)
                p->close();
        }
    }
    
    static inline float lengthSquared(const float *p0, const float *p1) {
        float dx = p1[0] - p0[0], dy = p1[1] - p0[1];
        return dx * dx + dy * dy;
    }
    
    static Ra::Color colorFromPaint(const NSVGpaint& paint) {
        if (paint.type == NSVG_PAINT_COLOR)
            return Ra::Color(paint.color);
        else {
            auto gradient = paint.gradient;
            size_t count = gradient->nstops;
            Ra::Vector<Ra::BGRA> stops(count);
            Ra::Vector<float> locs(count);
            for (int i = 0; i < count; i++) {
                stops[i] = Ra::BGRA(gradient->stops[i].color);
                locs[i] = gradient->stops[i].offset;
            }
            return Ra::Color(& stops[0], & locs[0], count, *(Ra::Transform *)gradient->xform, paint.type != NSVG_PAINT_LINEAR_GRADIENT);
        }
    }
};
