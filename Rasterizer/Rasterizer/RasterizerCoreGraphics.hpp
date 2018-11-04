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
    static CGRect CGRectFromBounds(Rasterizer::Bounds bounds) {
        return CGRectMake(bounds.lx, bounds.ly, bounds.ux - bounds.lx, bounds.uy - bounds.ly);
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
    static void writeGlyphGrid(NSString *fontName, std::vector<CGPathRef>& _glyphCGPaths, CGFloat _dimension, std::vector<Rasterizer::Path>& _glyphPaths, std::vector<Rasterizer::Bounds>& _glyphBounds) {
        CGPathRef shape = nullptr;
//        CGPathRef shape = CGPathCreateWithEllipseInRect(CGRectMake(0, 0, _dimension, _dimension), NULL);
//        CGPathRef shape = CGPathCreateWithRect(CGRectMake(0, 0, _dimension, _dimension), NULL);
//        CGPathRef shape = CGPathCreateWithRoundedRect(CGRectMake(0, 0, _dimension, _dimension), _dimension * 0.25, _dimension * 0.25, NULL);
        
        for (CGPathRef path : _glyphCGPaths)
            CFRelease(path);
        _glyphCGPaths.resize(0), _glyphPaths.resize(0), _glyphBounds.resize(0);
        CGFontRef cgFont = CGFontCreateWithFontName((__bridge CFStringRef)fontName);
        CTFontRef ctFont = CTFontCreateWithGraphicsFont(cgFont, _dimension, NULL, NULL);
        CFIndex glyphCount =  CTFontGetGlyphCount(ctFont);
        for (CFIndex glyph = 1; glyph < glyphCount; glyph++) {
            CGPathRef path = shape ? CGPathRetain(shape) : CTFontCreatePathForGlyph(ctFont, glyph, NULL);
            if (path) {
                _glyphCGPaths.emplace_back(path);
                _glyphPaths.emplace_back();
                writeCGPathToPath(path, _glyphPaths.back());
                _glyphBounds.emplace_back(boundsFromCGRect(CGPathGetPathBoundingBox(path)));
            }
        }
        CFRelease(cgFont);
        CFRelease(ctFont);
        CGPathRelease(shape);
    }
};
