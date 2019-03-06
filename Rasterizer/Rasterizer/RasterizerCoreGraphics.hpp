//
//  RasterizerCoreGraphics.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 23/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//

#import "RasterizerScene.hpp"
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
    static void writeCGSceneToScene(CGScene& cgscene, RasterizerScene::Scene& scene) {
        for (int i = 0; i < cgscene.paths.size(); i++) {
            scene.bgras.emplace_back(bgraFromCGColor(cgscene.colors[i]));
            scene.ctms.emplace_back(transformFromCGAffineTransform(cgscene.ctms[i]));
            scene.paths.emplace_back();
            writeCGPathToPath(cgscene.paths[i], scene.paths.back());
        }
    }
    static void writeSceneToCGScene(RasterizerScene::Scene& scene, CGScene& cgscene) {
        for (int i = 0; i < scene.paths.size(); i++)
            if (scene.paths[i].ref) {
                cgscene.colors.emplace_back(createCGColorFromBGRA(scene.bgras[i]));
                cgscene.ctms.emplace_back(CGAffineTransformFromTransform(scene.ctms[i]));
                cgscene.bounds.emplace_back(CGRectFromBounds(scene.paths[i].ref->bounds));
                CGMutablePathRef path = CGPathCreateMutable();
                writePathToCGPath(scene.paths[i], path);
                cgscene.paths.emplace_back(CGPathCreateCopy(path));
                CGPathRelease(path);
            }
    }
    static void drawCGScene(CGScene& scene, const Rasterizer::AffineTransform ctm, const Rasterizer::Bounds bounds, CGContextRef ctx) {
        for (size_t i = 0; i < scene.paths.size(); i++) {
            Rasterizer::Bounds b = boundsFromCGRect(scene.bounds[i]);
            Rasterizer::AffineTransform t = transformFromCGAffineTransform(scene.ctms[i]);
            Rasterizer::Bounds clip = b.transform(ctm.concat(t)).integral().intersect(bounds);
            if (clip.lx != clip.ux && clip.ly != clip.uy) {
                CGContextSaveGState(ctx);
                CGContextSetFillColorWithColor(ctx, scene.colors[i]);
                CGContextConcatCTM(ctx, scene.ctms[i]);
                CGContextAddPath(ctx, scene.paths[i]);
                CGContextFillPath(ctx);
                CGContextRestoreGState(ctx);
            }
        }
    }
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
                    p.ref->moveTo(float(element->points[0].x), float(element->points[0].y));
                    break;
                case kCGPathElementAddLineToPoint:
                    p.ref->lineTo(float(element->points[0].x), float(element->points[0].y));
                    break;
                case kCGPathElementAddQuadCurveToPoint:
                    p.ref->quadTo(float(element->points[0].x), float(element->points[0].y), float(element->points[1].x), float(element->points[1].y));
                    break;
                case kCGPathElementAddCurveToPoint:
                    p.ref->cubicTo(float(element->points[0].x), float(element->points[0].y), float(element->points[1].x), float(element->points[1].y), float(element->points[2].x), float(element->points[2].y));
                    break;
                case kCGPathElementCloseSubpath:
                    p.ref->close();
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
        for (Rasterizer::Sequence::Atom& atom : p.ref->atoms) {
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
    static CGPathRef createStrokedPath(CGPathRef path, CGFloat width, CGLineCap cap, CGLineJoin join, CGFloat limit, CGFloat scale) {
        CGAffineTransform scaleUp = { scale, 0, 0, scale, 0, 0 };
        CGAffineTransform scaleDown = { 1.0 / scale, 0, 0, 1.0 / scale, 0, 0 };
        CGPathRef scaledUp = CGPathCreateCopyByTransformingPath(path, & scaleUp);
        CGPathRef stroked = CGPathCreateCopyByStrokingPath(scaledUp, NULL, width * scale, cap, join, limit);
        CGPathRef scaledDown = CGPathCreateCopyByTransformingPath(stroked, & scaleDown);
        CGPathRelease(scaledUp);
        CGPathRelease(stroked);
        return scaledDown;
    }
    
    struct CGTestScene {
        enum RasterizerType : int { kRasterizerMT = 0, kRasterizer, kCoreGraphics, kRasterizerCount };
        
        CGTestScene() : rasterizerType(0), dimension(24), phi((sqrt(5) - 1) / 2) { contexts.resize(8); }
        
        std::vector<Rasterizer::Context> contexts;
        int rasterizerType;
        RasterizerScene::Scene scene;
        CGScene cgscene;
        BGRAColorConverter converter;
        CGFloat dimension;
        CGFloat phi;
    };
    
    static void drawTestScene(CGTestScene& testScene, const Rasterizer::AffineTransform _ctm, Rasterizer::Path *clipPath, bool useOutline, CGContextRef ctx, CGColorSpaceRef dstSpace, Rasterizer::Bitmap bitmap, Rasterizer::Buffer *buffer, float dx, float dy) {
        Rasterizer::AffineTransform ctm = _ctm;//.concat(Rasterizer::AffineTransform(-1.f, 1.f, 0.f, 1.f, 0.f, 0.f));
        testScene.contexts[0].setBitmap(bitmap, Rasterizer::Bounds(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX));
        if (testScene.rasterizerType == CGTestScene::kCoreGraphics) {
            if (testScene.cgscene.paths.size() == 0)
                writeSceneToCGScene(testScene.scene, testScene.cgscene);
            drawCGScene(testScene.cgscene, ctm, testScene.contexts[0].bounds, ctx);
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
            
            if (useOutline) {
                uint8_t black[4] = { 0, 0, 0, 255 };
                memset_pattern4(bgras, & black, pathsCount * sizeof(uint32_t));
            }
            Rasterizer::AffineTransform clip = clipPath ? Rasterizer::Bounds(100, 100, 200, 200).unit(ctm) : Rasterizer::Context::nullclip();
            Rasterizer::AffineTransform *clips = (Rasterizer::AffineTransform *)alloca(pathsCount * sizeof(Rasterizer::AffineTransform)), *cl = clips;
            for (int i = 0; i < pathsCount; i++, cl++)
                *cl = clip;
            Rasterizer::Colorant *colorants = (Rasterizer::Colorant *)alloca(pathsCount * sizeof(Rasterizer::Colorant)), *dst = colorants;
            for (int i = 0; i < pathsCount; i++, dst++)
                new (dst) Rasterizer::Colorant((uint8_t *)& bgras[i]);
            float width = useOutline ? 1.f : 0.f;
            
            if (dx != FLT_MAX) {
                size_t index = RasterizerScene::pathIndexForPoint(& testScene.scene.paths[0], & testScene.scene.ctms[0], false, clips, ctm, Rasterizer::Bounds(0.f, 0.f, bitmap.width, bitmap.height), 0, pathsCount, dx, dy);
                if (index != INT_MAX)
                    colorants[index].src0 = 0, colorants[index].src1 = 0, colorants[index].src2 = 255, colorants[index].src3 = 255;
            }
            if (testScene.rasterizerType == CGTestScene::kRasterizerMT) {
                if (buffer) {
                    Rasterizer::Path *paths = & testScene.scene.paths[0];
                    size_t divisions = testScene.contexts.size(), total, p, i;
                    for (total = p = 0; p < pathsCount; p++)
                        total += paths[p].ref->atomsCount ?: (paths[p].ref->shapes ? paths[p].ref->end >> 4 : 0);
                    size_t begins[divisions + 1], target, *b = begins;
                    begins[0] = 0, begins[divisions] = pathsCount;
                    for (count = p = 0, i = 1; i < divisions; i++) {
                        for (target = total * i / divisions; count < target; p++)
                            count += paths[p].ref->atomsCount ?: (paths[p].ref->shapes ? paths[p].ref->end >> 4: 0);
                        begins[i] = p;
                    }
                    for (i = 0; i < divisions; i++)
                        testScene.contexts[i].setGPU(bitmap.width, bitmap.height);
                    dispatch_apply(divisions, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                        testScene.contexts[idx].drawPaths(& testScene.scene.paths[0], ctms, false, colorants, clips, width, b[idx], b[idx + 1]);
                    });
                    count = divisions;
                } else {
                    slice = (bitmap.height + testScene.contexts.size() - 1) / testScene.contexts.size(), slice = slice < 64 ? 64 : slice;
                    for (count = ly = 0; ly < bitmap.height; ly = uy) {
                        uy = ly + slice, uy = uy < bitmap.height ? uy : bitmap.height;
                        testScene.contexts[count].setBitmap(bitmap, Rasterizer::Bounds(0, ly, bitmap.width, uy));
                        count++;
                    }
                    dispatch_apply(count, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                        testScene.contexts[idx].drawPaths(& testScene.scene.paths[0], ctms, false, colorants, clips, width, 0, pathsCount);
                    });
                }
            } else {
                count = 1;
                if (buffer)
                    testScene.contexts[0].setGPU(bitmap.width, bitmap.height);
                testScene.contexts[0].drawPaths(& testScene.scene.paths[0], ctms, false, colorants, clips, width, 0, pathsCount);
            }
            if (buffer) {
                std::vector<Rasterizer::Buffer::Entry> entries[count], *e = & entries[0];
                size_t begins[count], *b = begins;
                size_t size = Rasterizer::Context::writeContextsToBuffer(& testScene.contexts[0], count, & testScene.scene.paths[0], & ctms[0], colorants, clips, testScene.scene.paths.size(), begins, *buffer);
                if (count == 1)
                    Rasterizer::Context::writeContextToBuffer(& testScene.contexts[0], & testScene.scene.paths[0], & ctms[0], colorants, b[0], e[0], *buffer);
                else {
                    dispatch_apply(count, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                        Rasterizer::Context::writeContextToBuffer(& testScene.contexts[idx], & testScene.scene.paths[0], & ctms[0], colorants, b[idx], e[idx], *buffer);
                    });
                }
                for (int i = 0; i < count; i++)
                    for (auto entry : e[i])
                        *(buffer->entries.alloc(1)) = entry;
                size_t end = buffer->entries.base[buffer->entries.end - 1].end;
                assert(size == end);
            }
        }
    }
};
