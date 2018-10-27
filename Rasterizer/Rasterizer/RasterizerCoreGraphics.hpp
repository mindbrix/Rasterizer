//
//  RasterizerCoreGraphics.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 23/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "Rasterizer.hpp"
#import <CoreGraphics/CoreGraphics.h>


struct RasterizerCoreGraphics {
    static Rasterizer::Bounds boundsFromCGRect(CGRect rect) {
        return Rasterizer::Bounds(float(rect.origin.x), float(rect.origin.y), float(rect.origin.x + rect.size.width), float(rect.origin.y + rect.size.height));
    }
    
    struct CGPathApplier {
        CGPathApplier(Rasterizer::Path& path) : p(& path) {}
        void apply(const CGPathElement *element) {
            switch (element->type) {
                case kCGPathElementMoveToPoint:
                    p->moveTo(float(element->points[0].x), float(element->points[0].y));
                    break;
                case kCGPathElementAddLineToPoint:
                    p->lineTo(float(element->points[0].x), float(element->points[0].y));
                    break;
                case kCGPathElementAddQuadCurveToPoint:
                    p->quadTo(float(element->points[0].x), float(element->points[0].y), float(element->points[1].x), float(element->points[1].y));
                    break;
                case kCGPathElementAddCurveToPoint:
                    p->cubicTo(float(element->points[0].x), float(element->points[0].y), float(element->points[1].x), float(element->points[1].y), float(element->points[2].x), float(element->points[2].y));
                    break;
                case kCGPathElementCloseSubpath:
                    p->close();
                    break;
            }
        }
        Rasterizer::Path *p;
    };
    static void CGPathApplierFunction(void *info, const CGPathElement *element) {
        ((CGPathApplier *)info)->apply(element);
    };
    static void writeCGPathToPath(CGPathRef path, Rasterizer::Path &p) {
        CGPathApplier applier(p);
        CGPathApply(path, & applier, CGPathApplierFunction);
    }
    
    static void writePathToCGPath(Rasterizer::Path &p, CGMutablePathRef path) {
        float *points;
        for (Rasterizer::Path::Atom& atom : p.atoms) {
            size_t index = 0;
            auto type = 0xF & atom.types[0];
            while (type) {
                points = atom.points + index * 2;
                switch (type) {
                    case Rasterizer::Path::Atom::kMove:
                        CGPathMoveToPoint(path, NULL, points[0], points[1]);
                        index++;
                        break;
                    case Rasterizer::Path::Atom::kLine:
                        CGPathAddLineToPoint(path, NULL, points[0], points[1]);
                        index++;
                        break;
                    case Rasterizer::Path::Atom::kQuadratic:
                        CGPathAddQuadCurveToPoint(path, NULL, points[0], points[1], points[2], points[3]);
                        index += 2;
                        break;
                    case Rasterizer::Path::Atom::kCubic:
                        CGPathAddCurveToPoint(path, NULL, points[0], points[1], points[2], points[3], points[4], points[5]);
                        index += 3;
                        break;
                    case Rasterizer::Path::Atom::kClose:
                        CGPathCloseSubpath(path);
                        index++;
                        break;
                }
                type = 0xF & (atom.types[index / 2] >> ((index & 1) * 4));
            }
        }
    }
};
