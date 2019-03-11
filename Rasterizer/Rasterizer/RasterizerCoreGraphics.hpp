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
    static void writeSceneToCGScene(RasterizerScene::Scene& scene, CGScene& cgscene) {
        for (int i = 0; i < scene.paths.size(); i++)
            if (scene.paths[i].ref) {
                cgscene.colors.emplace_back(createCGColorFromBGRA(& scene.bgras[i].src0));
                cgscene.ctms.emplace_back(CGAffineTransformFromTransform(scene.ctms[i]));
                cgscene.bounds.emplace_back(CGRectFromBounds(scene.paths[i].ref->bounds));
                CGMutablePathRef path = CGPathCreateMutable();
                writePathToCGPath(scene.paths[i], path);
                cgscene.paths.emplace_back(CGPathCreateCopy(path));
                CGPathRelease(path);
            }
    }
    static void drawCGScene(CGScene& scene, const Rasterizer::Transform ctm, const Rasterizer::Bounds bounds, CGContextRef ctx) {
        for (size_t i = 0; i < scene.paths.size(); i++) {
            Rasterizer::Bounds b = boundsFromCGRect(scene.bounds[i]);
            Rasterizer::Transform t = transformFromCGAffineTransform(scene.ctms[i]);
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
    static CGColorRef createCGColorFromBGRA(uint8_t *bgra) {
        CGColorSpaceRef srcSpace = createSrcColorSpace();
        CGFloat components[4] = { CGFloat(bgra[2]) / 255, CGFloat(bgra[1]) / 255, CGFloat(bgra[0]) / 255, CGFloat(bgra[3]) / 255 };
        CGColorRef color = CGColorCreate(srcSpace, components);
        CGColorSpaceRelease(srcSpace);
        return color;
    }
    static Rasterizer::Transform transformFromCGAffineTransform(CGAffineTransform t) {
        return Rasterizer::Transform(float(t.a), float(t.b), float(t.c), float(t.d), float(t.tx), float(t.ty));
    }
    static CGAffineTransform CGAffineTransformFromTransform(Rasterizer::Transform t) {
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
    static Rasterizer::Path createPathFromCGPath(CGPathRef path) {
        Rasterizer::Path p;
        CGPathApplier applier(p);
        CGPathApply(path, & applier, CGPathApplierFunction);
        return p;
    }
    
    static void writePathToCGPath(Rasterizer::Path p, CGMutablePathRef path) {
        float *points;
        for (Rasterizer::Geometry::Atom& atom : p.ref->atoms) {
            size_t index = 0;
            auto type = 0xF & atom.types[0];
            while (type) {
                points = atom.points + index * 2;
                switch (type) {
                    case Rasterizer::Geometry::Atom::kMove:
                        CGPathMoveToPoint(path, NULL, points[0], points[1]);
                        index++;
                        break;
                    case Rasterizer::Geometry::Atom::kLine:
                        CGPathAddLineToPoint(path, NULL, points[0], points[1]);
                        index++;
                        break;
                    case Rasterizer::Geometry::Atom::kQuadratic:
                        CGPathAddQuadCurveToPoint(path, NULL, points[0], points[1], points[2], points[3]);
                        index += 2;
                        break;
                    case Rasterizer::Geometry::Atom::kCubic:
                        CGPathAddCurveToPoint(path, NULL, points[0], points[1], points[2], points[3], points[4], points[5]);
                        index += 3;
                        break;
                    case Rasterizer::Geometry::Atom::kClose:
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
    
    static void renderPaths(std::vector<Rasterizer::Context>& contexts, Rasterizer::Path *paths, Rasterizer::Transform *ctms, Rasterizer::Transform *gpuctms, bool even, Rasterizer::Colorant *colors, Rasterizer::Transform *clips, float width, size_t pathsCount, Rasterizer::Bitmap bitmap, Rasterizer::Buffer *buffer, bool multithread) {
        size_t slice, ly, uy, count;
        if (multithread) {
            if (buffer) {
                size_t divisions = contexts.size(), total, p, i;
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
                    contexts[i].setGPU(bitmap.width, bitmap.height, gpuctms);
                dispatch_apply(divisions, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                    contexts[idx].drawPaths(paths, ctms, false, colors, clips, width, b[idx], b[idx + 1]);
                });
                count = divisions;
            } else {
                slice = (bitmap.height + contexts.size() - 1) / contexts.size(), slice = slice < 64 ? 64 : slice;
                for (count = ly = 0; ly < bitmap.height; ly = uy) {
                    uy = ly + slice, uy = uy < bitmap.height ? uy : bitmap.height;
                    contexts[count].setBitmap(bitmap, Rasterizer::Bounds(0, ly, bitmap.width, uy));
                    count++;
                }
                dispatch_apply(count, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                    contexts[idx].drawPaths(paths, ctms, false, colors, clips, width, 0, pathsCount);
                });
            }
        } else {
            count = 1;
            if (buffer)
                contexts[0].setGPU(bitmap.width, bitmap.height, gpuctms);
            contexts[0].drawPaths(paths, ctms, false, colors, clips, width, 0, pathsCount);
        }
        if (buffer) {
            std::vector<Rasterizer::Buffer::Entry> entries[count], *e = & entries[0];
            size_t begins[count], *b = begins;
            size_t size = Rasterizer::Context::writeContextsToBuffer(& contexts[0], count, paths, ctms, colors, clips, pathsCount, begins, *buffer);
            if (count == 1)
                Rasterizer::Context::writeContextToBuffer(& contexts[0], paths, ctms, colors, b[0], e[0], *buffer);
            else {
                dispatch_apply(count, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                    Rasterizer::Context::writeContextToBuffer(& contexts[idx], paths, ctms, colors, b[idx], e[idx], *buffer);
                });
            }
            for (int i = 0; i < count; i++)
                for (auto entry : e[i])
                    *(buffer->entries.alloc(1)) = entry;
            size_t end = buffer->entries.base[buffer->entries.end - 1].end;
            assert(size == end);
        }
    }
    static void drawTestScene(CGTestScene& testScene, const Rasterizer::Transform _ctm, Rasterizer::Path *clipPath, bool useOutline, CGContextRef ctx, CGColorSpaceRef dstSpace, Rasterizer::Bitmap bitmap, Rasterizer::Buffer *buffer, float dx, float dy) {
        Rasterizer::Transform ctm = _ctm;//.concat(Rasterizer::AffineTransform(-1.f, 1.f, 0.f, 1.f, 0.f, 0.f));
        testScene.contexts[0].setBitmap(bitmap, Rasterizer::Bounds(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX));
        if (testScene.rasterizerType == CGTestScene::kCoreGraphics) {
            if (testScene.cgscene.paths.size() == 0)
                writeSceneToCGScene(testScene.scene, testScene.cgscene);
            drawCGScene(testScene.cgscene, ctm, testScene.contexts[0].bounds, ctx);
        } else {
            assert(sizeof(uint32_t) == sizeof(Rasterizer::Colorant));
            size_t pathsCount = testScene.scene.paths.size();
            Rasterizer::Transform *ctms = (Rasterizer::Transform *)malloc(pathsCount * sizeof(ctm));
            Rasterizer::Transform *gpuctms = (Rasterizer::Transform *)malloc(pathsCount * sizeof(ctm));
            uint32_t *bgras = (uint32_t *)malloc(pathsCount * sizeof(uint32_t));
            Rasterizer::Colorant *colors = (Rasterizer::Colorant *)malloc(pathsCount * sizeof(Rasterizer::Colorant)), *dst = colors;
            Rasterizer::Transform *clips = (Rasterizer::Transform *)malloc(pathsCount * sizeof(Rasterizer::Transform)), *cl = clips;
            
            for (size_t i = 0; i < pathsCount; i++)
                ctms[i] = ctm.concat(testScene.scene.ctms[i]);
            memcpy(gpuctms, ctms, pathsCount * sizeof(ctm));
            
            CGColorSpaceRef srcSpace = createSrcColorSpace();
            testScene.converter.set(srcSpace, dstSpace);
            testScene.converter.convert((uint32_t *)& testScene.scene.bgras[0].src0, pathsCount, bgras);
            CGColorSpaceRelease(srcSpace);
            for (int i = 0; i < pathsCount; i++, dst++)
                new (dst) Rasterizer::Colorant((uint8_t *)& bgras[i]);
        
            Rasterizer::Transform clip = clipPath ? Rasterizer::Bounds(100, 100, 200, 200).unit(ctm) : Rasterizer::Context::nullclip();
            for (int i = 0; i < pathsCount; i++, cl++)
                *cl = clip;
            
            float width = useOutline ? 1.f : 0.f;
            if (useOutline) {
                uint8_t black[4] = { 0, 0, 0, 255 };
                memset_pattern4(colors, & black, pathsCount * sizeof(uint32_t));
            }
            if (dx != FLT_MAX) {
                size_t index = RasterizerScene::pathIndexForPoint(& testScene.scene.paths[0], & testScene.scene.ctms[0], false, clips, ctm, Rasterizer::Bounds(0.f, 0.f, bitmap.width, bitmap.height), 0, pathsCount, dx, dy);
                if (index != INT_MAX)
                    colors[index].src0 = 0, colors[index].src1 = 0, colors[index].src2 = 255, colors[index].src3 = 255;
            }
            renderPaths(testScene.contexts, & testScene.scene.paths[0], ctms, gpuctms, false, colors, clips, width, testScene.scene.paths.size(), bitmap, buffer, testScene.rasterizerType == CGTestScene::kRasterizerMT);
            free(ctms), free(gpuctms), free(bgras), free(colors), free(clips);
        }
    }
};
