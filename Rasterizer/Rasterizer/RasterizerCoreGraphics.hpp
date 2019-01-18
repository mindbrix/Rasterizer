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
    struct Scene {
        void empty() { ctms.resize(0), paths.resize(0), bgras.resize(0); }
        std::vector<uint32_t> bgras;
        std::vector<Rasterizer::AffineTransform> ctms;
        std::vector<Rasterizer::Path> paths;
    };
    
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
                srcFormat.bitmapInfo = (CGBitmapInfo)kCGImageAlphaFirst | kCGBitmapByteOrder32Little;
                srcFormat.colorSpace = srcSpace;
                dstFormat.bitsPerPixel = srcFormat.bitsPerPixel;
                dstFormat.bitsPerComponent = srcFormat.bitsPerComponent;
                dstFormat.bitmapInfo = srcFormat.bitmapInfo;
                dstFormat.colorSpace = dstSpace;
                converter = vImageConverter_CreateWithCGImageFormat(&srcFormat, &dstFormat, NULL, kvImageNoFlags, NULL);
            }
        }
        void convert(uint32_t *src, size_t size, uint32_t *dst) {
            if (size && converter) {
                vImage_Buffer sourceBuffer;
                vImageBuffer_Init(&sourceBuffer, 1, size, 32, kvImageNoAllocate);
                sourceBuffer.data = (void *)src;
                vImage_Buffer destBuffer;
                vImageBuffer_Init(&destBuffer, 1, size, 32, kvImageNoAllocate);
                destBuffer.data = (void *)dst;
                vImageConvert_AnyToAny(converter, &sourceBuffer, &destBuffer, NULL, kvImageNoFlags);
            }
        }
        vImageConverterRef converter;
        vImage_CGImageFormat srcFormat, dstFormat;
    };
    
    static CGColorSpaceRef createSrcColorSpace() {
        return CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    }
    
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
    static CGColorRef createCGColorFromBGRA(uint32_t bgra) {
        CGColorSpaceRef srcSpace = createSrcColorSpace();
        uint8_t *src = (uint8_t *)& bgra;
        CGFloat components[4] = { CGFloat(src[2]) / 255, CGFloat(src[1]) / 255, CGFloat(src[0]) / 255, CGFloat(src[3]) / 255 };
        CGColorRef color = CGColorCreate(srcSpace, components);
        CGColorSpaceRelease(srcSpace);
        return color;
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
        CGPathApplier(Rasterizer::Path path) : p(path) {}
        void apply(const CGPathElement *element) {
            switch (element->type) {
                case kCGPathElementMoveToPoint:
                    p.sequence->moveTo(float(element->points[0].x), float(element->points[0].y));
                    break;
                case kCGPathElementAddLineToPoint:
                    p.sequence->lineTo(float(element->points[0].x), float(element->points[0].y));
                    break;
                case kCGPathElementAddQuadCurveToPoint:
                    p.sequence->quadTo(float(element->points[0].x), float(element->points[0].y), float(element->points[1].x), float(element->points[1].y));
                    break;
                case kCGPathElementAddCurveToPoint:
                    p.sequence->cubicTo(float(element->points[0].x), float(element->points[0].y), float(element->points[1].x), float(element->points[1].y), float(element->points[2].x), float(element->points[2].y));
                    break;
                case kCGPathElementCloseSubpath:
                    p.sequence->close();
                    break;
            }
        }
        Rasterizer::Path p;
    };
    static void CGPathApplierFunction(void *info, const CGPathElement *element) {
        ((CGPathApplier *)info)->apply(element);
    };
    static void writeCGPathToPath(CGPathRef path, Rasterizer::Path p) {
        CGPathApplier applier(p);
        CGPathApply(path, & applier, CGPathApplierFunction);
    }
    
    static void writePathToCGPath(Rasterizer::Path p, CGMutablePathRef path) {
        float *points;
        for (Rasterizer::Sequence::Atom& atom : p.sequence->atoms) {
            size_t index = 0;
            auto type = 0xF & atom.types[0];
            while (type) {
                points = atom.points + index * 2;
                switch (type) {
                    case Rasterizer::Sequence::Atom::kMove:
                        CGPathMoveToPoint(path, NULL, points[0], points[1]);
                        index++;
                        break;
                    case Rasterizer::Sequence::Atom::kLine:
                        CGPathAddLineToPoint(path, NULL, points[0], points[1]);
                        index++;
                        break;
                    case Rasterizer::Sequence::Atom::kQuadratic:
                        CGPathAddQuadCurveToPoint(path, NULL, points[0], points[1], points[2], points[3]);
                        index += 2;
                        break;
                    case Rasterizer::Sequence::Atom::kCubic:
                        CGPathAddCurveToPoint(path, NULL, points[0], points[1], points[2], points[3], points[4], points[5]);
                        index += 3;
                        break;
                    case Rasterizer::Sequence::Atom::kClose:
                        CGPathCloseSubpath(path);
                        index++;
                        break;
                }
                type = 0xF & (atom.types[index / 2] >> ((index & 1) * 4));
            }
        }
    }
    static CGPathRef createStrokedPath(CGPathRef path, CGFloat width, CGLineCap cap, CGLineJoin join, CGFloat limit) {
        CGFloat scale = 10;
        CGAffineTransform scaleUp = { scale, 0, 0, scale, 0, 0 };
        CGAffineTransform scaleDown = { 1.0 / scale, 0, 0, 1.0 / scale, 0, 0 };
        CGPathRef scaledUp = CGPathCreateCopyByTransformingPath(path, & scaleUp);
        CGPathRef stroked = CGPathCreateCopyByStrokingPath(scaledUp, NULL, width * scale, cap, join, limit);
        CGPathRef scaledDown = CGPathCreateCopyByTransformingPath(stroked, & scaleDown);
        CGPathRelease(scaledUp);
        CGPathRelease(stroked);
        return scaledDown;
    }
    static void writeCGSceneToScene(CGScene& cgscene, Scene& scene) {
        for (int i = 0; i < cgscene.paths.size(); i++) {
            scene.bgras.emplace_back(bgraFromCGColor(cgscene.colors[i]));
            scene.ctms.emplace_back(transformFromCGAffineTransform(cgscene.ctms[i]));
            scene.paths.emplace_back();
            writeCGPathToPath(cgscene.paths[i], scene.paths.back());
        }
    }
    static void writeSceneToCGScene(Scene& scene, CGScene& cgscene) {
        for (int i = 0; i < scene.paths.size(); i++)
            if (scene.paths[i].sequence) {
                cgscene.colors.emplace_back(createCGColorFromBGRA(scene.bgras[i]));
                cgscene.ctms.emplace_back(CGAffineTransformFromTransform(scene.ctms[i]));
                cgscene.bounds.emplace_back(CGRectFromBounds(scene.paths[i].sequence->bounds));
                CGMutablePathRef path = CGPathCreateMutable();
                writePathToCGPath(scene.paths[i], path);
                cgscene.paths.emplace_back(CGPathCreateCopy(path));
                CGPathRelease(path);
            }
    }
    
    struct CGTestScene {
        enum RasterizerType : int { kRasterizerMT = 0, kRasterizer, kCoreGraphics, kRasterizerCount };
        
        CGTestScene() : rasterizerType(0), dimension(24), phi((sqrt(5) - 1) / 2) { contexts.resize(8); }
        
        std::vector<Rasterizer::Context> contexts;
        int rasterizerType;
        Scene scene;
        RasterizerCoreGraphics::CGScene cgscene;
        RasterizerCoreGraphics::BGRAColorConverter converter;
        CGFloat dimension;
        CGFloat phi;
    };
    
    static void drawTestScene(CGTestScene& testScene, const Rasterizer::AffineTransform ctm, Rasterizer::Bounds clip, Rasterizer::Path *clipPath, CGContextRef ctx, CGColorSpaceRef dstSpace, Rasterizer::Bitmap bitmap, Rasterizer::Buffer *buffer) {
        testScene.contexts[0].setBitmap(bitmap);
        testScene.contexts[0].intersectClip(clip);
        if (testScene.rasterizerType == CGTestScene::kCoreGraphics) {
            if (testScene.cgscene.paths.size() == 0)
                RasterizerCoreGraphics::writeSceneToCGScene(testScene.scene, testScene.cgscene);
            if (clipPath)
                testScene.contexts[0].intersectClip(*clipPath, ctm, false);
            for (size_t i = 0; i < testScene.cgscene.paths.size(); i++) {
                Rasterizer::Bounds bounds = RasterizerCoreGraphics::boundsFromCGRect(testScene.cgscene.bounds[i]);
                Rasterizer::AffineTransform t = RasterizerCoreGraphics::transformFromCGAffineTransform(testScene.cgscene.ctms[i]);
                Rasterizer::Bounds clipped = bounds.transform(ctm.concat(t)).integral().intersect(testScene.contexts[0].clip);
                if (clipped.lx != clipped.ux && clipped.ly != clipped.uy) {
                    CGContextSaveGState(ctx);
                    CGContextSetFillColorWithColor(ctx, testScene.cgscene.colors[i]);
                    CGContextConcatCTM(ctx, testScene.cgscene.ctms[i]);
                    CGContextAddPath(ctx, testScene.cgscene.paths[i]);
                    CGContextFillPath(ctx);
                    CGContextRestoreGState(ctx);
                }
            }
        } else {
            size_t pathsCount = testScene.scene.paths.size();
            size_t slice, ly, uy, count;
            Rasterizer::AffineTransform *ctms = (Rasterizer::AffineTransform *)alloca(pathsCount * sizeof(ctm));
            for (size_t i = 0; i < pathsCount; i++)
                ctms[i] = ctm.concat(testScene.scene.ctms[i]);
            CGColorSpaceRef srcSpace = createSrcColorSpace();
            uint32_t *bgras = (uint32_t *)alloca(pathsCount * sizeof(uint32_t));
            testScene.converter.set(srcSpace, dstSpace);
            testScene.converter.convert(& testScene.scene.bgras[0], pathsCount, bgras);
            CGColorSpaceRelease(srcSpace);
            
            if (testScene.rasterizerType == CGTestScene::kRasterizerMT) {
                if (buffer) {
                    Rasterizer::Path *paths = & testScene.scene.paths[0];
                    size_t divisions = testScene.contexts.size(), total, p, i;
                    for (total = p = 0; p < pathsCount; p++)
                        total += paths[p].sequence->atoms.size();
                    size_t begins[divisions + 1], target, *b = begins;
                    begins[0] = 0, begins[divisions] = pathsCount;
                    for (count = p = 0, i = 1; i < divisions; i++) {
                        target = total * i / divisions;
                        while (count < target)
                            count += paths[p++].sequence->atoms.size();
                        begins[i] = p;
                    }
                    for (i = 0; i < divisions; i++) {
                        if (buffer)
                            testScene.contexts[i].setGPU(bitmap.width, bitmap.height);
                        else
                            testScene.contexts[i].setBitmap(bitmap);
                        testScene.contexts[i].intersectClip(clip);
                        if (clipPath)
                            testScene.contexts[i].intersectClip(*clipPath, ctm, false);
                    }
                    dispatch_apply(divisions, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                        testScene.contexts[idx].drawPaths(& testScene.scene.paths[0], ctms, false, bgras, b[idx], b[idx + 1]);
                    });
                    count = divisions;
                } else {
                    slice = (bitmap.height + testScene.contexts.size() - 1) / testScene.contexts.size(), slice = slice < 64 ? 64 : slice;
                    for (count = ly = 0; ly < bitmap.height; ly = uy) {
                        uy = ly + slice, uy = uy < bitmap.height ? uy : bitmap.height;
                        if (buffer)
                            testScene.contexts[count].setGPU(bitmap.width, bitmap.height);
                        else
                            testScene.contexts[count].setBitmap(bitmap);
                        testScene.contexts[count].intersectClip(clip);
                        testScene.contexts[count].intersectClip(Rasterizer::Bounds(0, ly, bitmap.width, uy));
                        if (clipPath)
                            testScene.contexts[count].intersectClip(*clipPath, ctm, false);
                        count++;
                    }
                    dispatch_apply(count, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                        testScene.contexts[idx].drawPaths(& testScene.scene.paths[0], ctms, false, bgras, 0, pathsCount);
                    });
                }
            } else {
                count = 1;
                if (buffer)
                    testScene.contexts[0].setGPU(bitmap.width, bitmap.height);
                testScene.contexts[0].intersectClip(clip);
                if (clipPath)
                    testScene.contexts[0].intersectClip(*clipPath, ctm, false);
                testScene.contexts[0].drawPaths(& testScene.scene.paths[0], ctms, false, bgras, 0, pathsCount);
            }
            if (buffer) {
                size_t shapesCount = 1000, shapesSize = shapesCount * sizeof(Rasterizer::GPU::Colorant), begin, end;
                Rasterizer::Context::writeContextsToBuffer(& testScene.contexts[0], count, shapesSize, bgras, testScene.scene.ctms, testScene.scene.paths, *buffer);
                if (shapesCount) {
                    begin = (buffer->entries.base + buffer->entries.end - 1)->end, end = begin + shapesSize;
                    Rasterizer::GPU::Colorant *dst = (Rasterizer::GPU::Colorant *)(buffer->data.base + begin);
                    uint8_t bgra[4] = { 0, 0, 0, 255 };
                    for (int i = 0; i < shapesCount; i++, dst++) {
                        new (dst) Rasterizer::GPU::Colorant(bgra);
                    }
                    new (buffer->entries.alloc(1)) Rasterizer::Buffer::Entry(Rasterizer::Buffer::Entry::kColorants, begin, end);
                }
            }
        }
    }
};
