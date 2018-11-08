//
//  RasterizerCoreGraphics.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 23/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "Rasterizer.hpp"
#import <Accelerate/Accelerate.h>
#import <CoreGraphics/CoreGraphics.h>


struct RasterizerCoreGraphics {
    struct CGScene {
        ~CGScene() { empty(); }
        void empty() {
            for (CGPathRef path : paths)
                CGPathRelease(path);
            for (CGColorRef color : colors)
                CGColorRelease(color);
            ctms.resize(0), paths.resize(0), bounds.resize(0), colors.resize(0);
        }
        std::vector<CGColorRef> colors;
        std::vector<CGRect> bounds;
        std::vector<CGAffineTransform> ctms;
        std::vector<CGPathRef> paths;
    };
    
    struct BGRAColorConverter {
        BGRAColorConverter() : converter(nullptr) { reset(); }
        ~BGRAColorConverter() { reset(); }
        void reset() {
            if (converter)
                vImageConverter_Release(converter);
            converter = nullptr;
            bzero(&srcFormat, sizeof(srcFormat));
            bzero(&dstFormat, sizeof(dstFormat));
        }
        void set(CGColorSpaceRef srcSpace, CGColorSpaceRef dstSpace) {
            if (srcFormat.colorSpace != srcSpace || dstFormat.colorSpace != dstSpace) {
                reset();
                srcFormat.bitsPerPixel = 32;
                srcFormat.bitsPerComponent = 8;
                srcFormat.bitmapInfo = (CGBitmapInfo)kCGImageAlphaPremultipliedLast;
                srcFormat.colorSpace = srcSpace;
                dstFormat.bitsPerPixel = srcFormat.bitsPerPixel;
                dstFormat.bitsPerComponent = srcFormat.bitsPerComponent;
                dstFormat.bitmapInfo = srcFormat.bitmapInfo;
                dstFormat.colorSpace = dstSpace;
                /* Set up a converter. */
                vImage_Error error = kvImageNoError;
                converter = vImageConverter_CreateWithCGImageFormat(&srcFormat, &dstFormat, NULL, kvImageNoFlags, &error);
            }
        }
        void convert(uint32_t *src, size_t size, uint32_t *dst) {
            if (size && converter) {
                vImage_Buffer sourceBuffer;
                vImageBuffer_Init(&sourceBuffer, 1, size, 32, kvImageNoAllocate);
                sourceBuffer.data = (void *)src;
                vImage_Buffer destBuffer;
                vImageBuffer_Init(&destBuffer, 1, size, 32, kvImageNoFlags);
                destBuffer.data = (void *)dst;
                vImageConvert_AnyToAny(converter, &sourceBuffer, &destBuffer, NULL, kvImageNoFlags);
            }
        }
        vImageConverterRef converter;
        vImage_CGImageFormat srcFormat, dstFormat;
    };
    
    static uint32_t bgraFromCGColor(CGColorRef color) {
        uint32_t bgra = 0;
        uint8_t *dst = (uint8_t *) &bgra;
        const CGFloat *components = CGColorGetComponents(color);
        if (CGColorGetNumberOfComponents(color) == 2)
            *dst++ = components[0] * 255.5f, *dst++ = components[0] * 255.5f, *dst++ = components[0] * 255.5f, *dst++ = components[1] * 255.5f;
        else if (CGColorGetNumberOfComponents(color) == 4)
            *dst++ = components[2] * 255.5f, *dst++ = components[1] * 255.5f, *dst++ = components[0] * 255.5f, *dst++ = components[3] * 255.5f;
        return bgra;
    }
    static CGColorRef CGColorFromBGRA(uint32_t bgra) {
        uint8_t *src = (uint8_t *)& bgra;
        CGFloat r, g, b, a;
        r = CGFloat(src[2]) / 255, g = CGFloat(src[1]) / 255, b = CGFloat(src[0]) / 255, a = CGFloat(src[3]) / 255;
        return CGColorCreateGenericRGB(r, g, b, a);
    }
    static Rasterizer::AffineTransform transformFromCGAffineTransform(CGAffineTransform t) {
        return Rasterizer::AffineTransform(float(t.a), float(t.b), float(t.c), float(t.d), float(t.tx), float(t.ty));
    }
    static CGAffineTransform CGAffineTransformFromTransform(Rasterizer::AffineTransform t) {
        return CGAffineTransformMake(t.a, t.b, t.c, t.d, t.tx, t.ty);
    }
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
    static void writeGlyphGridToCGScene(NSString *fontName, CGFloat dimension, CGFloat phi, CGScene& scene) {
        CGColorRef black = CGColorGetConstantColor(kCGColorBlack);
        CGFontRef cgFont = CGFontCreateWithFontName((__bridge CFStringRef)fontName);
        CTFontRef ctFont = CTFontCreateWithGraphicsFont(cgFont, dimension * phi, NULL, NULL);
        CFIndex glyphCount = CTFontGetGlyphCount(ctFont);
        size_t square = ceilf(sqrtf(float(glyphCount)));
        CGFloat tx, ty;
        CGRect bounds;
        size_t idx = 0;
        for (CFIndex i = 1; i < glyphCount; i++) {
            CGPathRef path = CTFontCreatePathForGlyph(ctFont, i, NULL);
            if (path) {
                bounds = CGPathGetPathBoundingBox(path);
                tx = (idx % square) * dimension * phi - bounds.origin.x, ty = (idx / square) * dimension * phi - bounds.origin.y;
                idx++;
                scene.ctms.emplace_back(CGAffineTransformMake(1, 0, 0, 1, tx, ty));
                scene.paths.emplace_back(path);
                scene.bounds.emplace_back(bounds);
                scene.colors.emplace_back(CGColorRetain(black));
            }
        }
        CFRelease(cgFont);
        CFRelease(ctFont);
    }
    
    static void writeCGSceneToScene(CGScene& cgscene, Rasterizer::Scene& scene) {
        for (int i = 0; i < cgscene.paths.size(); i++) {
            scene.bgras.emplace_back(bgraFromCGColor(cgscene.colors[i]));
            scene.ctms.emplace_back(transformFromCGAffineTransform(cgscene.ctms[i]));
            scene.bounds.emplace_back(boundsFromCGRect(cgscene.bounds[i]));
            scene.paths.emplace_back();
            writeCGPathToPath(cgscene.paths[i], scene.paths.back());
        }
    }
    static void writeSceneToCGScene(Rasterizer::Scene& scene, CGScene& cgscene) {
        for (int i = 0; i < scene.paths.size(); i++) {
            cgscene.colors.emplace_back(CGColorFromBGRA(scene.bgras[i]));
            cgscene.ctms.emplace_back(CGAffineTransformFromTransform(scene.ctms[i]));
            cgscene.bounds.emplace_back(CGRectFromBounds(scene.bounds[i]));
            CGMutablePathRef path = CGPathCreateMutable();
            writePathToCGPath(scene.paths[i], path);
            cgscene.paths.emplace_back(CGPathCreateCopy(path));
            CGPathRelease(path);
        }
    }
};
