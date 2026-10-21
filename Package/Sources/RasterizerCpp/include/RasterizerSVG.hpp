//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//  SPDX-License-Identifier: MIT
//

#import "Rasterizer.hpp"
#import "nanosvg.h"
#import <unordered_map>

struct RasterizerSVG {
    static const bool kWriteOneBigPath = false;
    
    static Ra::Transform addSvgToScene(const char *filename, Ra::SceneRef& scene) {
        struct NSVGimage *image = nsvgParseFromFile(filename, nullptr, 0);
        if (image == nullptr)
            return Ra::Transform();
        addSvgImageToScene(image, scene);
        Ra::Transform ctm = Ra::Transform(1, 0, 0, -1, 0, image->height);
        nsvgDelete(image);
        
        return ctm;
    }
    
    static void addSvgImageToScene(NSVGimage *image, Ra::SceneRef& scene) {
        if (image) {
            if (kWriteOneBigPath) {
                Ra::Path path;
                for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next)
                    if ((shape->flags & NSVG_FLAGS_VISIBLE) && shape->fill.type != NSVG_PAINT_NONE)
                        writePathFromShape(shape, path);
                scene->addPath(path, Ra::Transform(), Ra::Color(), 0.f, Ra::Draw::kFillEvenOdd);
            } else {
                Clips clips;
                for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next) {
                    if ((shape->flags & NSVG_FLAGS_VISIBLE) == 0)
                        continue;
                    // Its clip paths' intersection: rectangles' as clip bounds, & the innermost other clip path's exactly, as
                    // draws have one, & the rest by their bounds
                    Ra::Bounds clipBounds = Ra::Bounds::huge();  Ra::Path clipPath = nullptr;  int clipFlags = 0;  bool isClipped = false;
                    for (int i = 0; i < shape->clipPathCount; i++) {
                        const Clip& clip = clipFor(shape->clipPaths[i], clips);
                        if ((isClipped = clip.isEmpty))
                            break;
                        clipBounds = clipBounds.intersect(clip.bounds);
                        if (!clip.isRect)
                            clipPath = clip.path, clipFlags = clip.evenOdd ? Ra::Draw::kClipEvenOdd : 0;
                    }
                    if (isClipped)
                        continue;
                    Ra::Bounds *bounds = shape->clipPathCount ? & clipBounds : nullptr;
                    Ra::Path *clip = clipPath.ptr ? & clipPath : nullptr;
                    Ra::Path path;
                    writePathFromShape(shape, path);
                    if (!path->isValid())
                        continue;
                    Ra::Transform ctm;
                    if (shape->fill.type != NSVG_PAINT_NONE) {
                        int flags = shape->fillRule == NSVG_FILLRULE_EVENODD ? Ra::Draw::kFillEvenOdd : 0;
                        scene->addPath(path, ctm, paintFromPaint(shape->fill, shape->opacity), 0.f, flags | clipFlags, bounds, clip);
                    }
                    if (shape->stroke.type != NSVG_PAINT_NONE && shape->strokeWidth) {
                        if (shape->strokeDashCount) {
                            path = Ra::Dasher::CreateDashedPath(path, shape->strokeDashOffset, shape->strokeDashArray, shape->strokeDashCount);
                        }
                        char cap = shape->strokeLineCap;
                        int flags = cap == NSVG_CAP_ROUND ? Ra::Draw::kRoundCap : cap == NSVG_CAP_SQUARE ? Ra::Draw::kSquareCap : 0;
                        char join = shape->strokeLineJoin;
                        flags |= join == NSVG_JOIN_ROUND ? Ra::Draw::kRoundJoin : 0;
                        scene->addPath(path, ctm, paintFromPaint(shape->stroke, shape->opacity), shape->strokeWidth, flags | clipFlags, bounds, clip);
                    }
                }
            }
        }
    }
    
    // A clip path, as the union of its visible shapes' paths, filled even-odd if they all are, & its bounds, & whether it's
    // one axis-aligned rectangle, which its bounds clip exactly, without a clip mask. Shapes sharing a clip path share its path
    struct Clip {
        Ra::Path path;  Ra::Bounds bounds;  bool evenOdd = true, isEmpty = true, isRect = false;
    };
    typedef std::unordered_map<const NSVGclipPath *, Clip> Clips;
    
    static const Clip& clipFor(const NSVGclipPath *clipPath, Clips& clips) {
        auto it = clips.find(clipPath);
        if (it != clips.end())
            return it->second;
        Clip clip;
        for (NSVGshape *shape = clipPath->shapes; shape != NULL; shape = shape->next)
            if (shape->flags & NSVG_FLAGS_VISIBLE) {
                writePathFromShape(shape, clip.path);
                clip.evenOdd &= shape->fillRule == NSVG_FILLRULE_EVENODD;
            }
        clip.isEmpty = !clip.path->isValid();
        if (!clip.isEmpty)      // nanosvg's lines are flat cubics, which cubicTo() writes as lines, so a rectangle is one
            clip.isRect = clip.path->isRect(), clip.bounds = clip.path->bounds;
        return clips.emplace(clipPath, clip).first->second;
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
    
    static Ra::Paint paintFromPaint(const NSVGpaint& paint, float opacity) {
        if (paint.type == NSVG_PAINT_COLOR)
            return Ra::Color(paint.color).withOpacity(opacity);
        else {
            auto gradient = paint.gradient;
            size_t count = gradient->nstops;
            Ra::Vector<Ra::Color> stops(count);
            Ra::Vector<float> locs(count);
            for (int i = 0; i < count; i++) {
                stops[i] = Ra::Color(gradient->stops[i].color).withOpacity(opacity);
                locs[i] = gradient->stops[i].offset;
            }
            return Ra::Paint(& stops[0], & locs[0], count, *(Ra::Transform *)gradient->xform, paint.type != NSVG_PAINT_LINEAR_GRADIENT);
        }
    }
};

typedef RasterizerSVG RaSVG;
