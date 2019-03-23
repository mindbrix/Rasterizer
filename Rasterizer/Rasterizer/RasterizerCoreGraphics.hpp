//
//  RasterizerCoreGraphics.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 23/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "RasterizerEvent.hpp"
#import "RasterizerWinding.hpp"
#import "RasterizerTrueType.hpp"
#import <Accelerate/Accelerate.h>
#import <CoreGraphics/CoreGraphics.h>

struct RasterizerCoreGraphics {
    static void drawScenes(Rasterizer::Ref<Rasterizer::Scene> *scenes, size_t scenesCount, const Rasterizer::Transform view, const Rasterizer::Bounds bounds, CGContextRef ctx) {
        for (int j = 0; j < scenesCount; j++) {
            Rasterizer::Scene& scene = *scenes[j].ref;
            for (size_t i = 0; i < scene.paths.size(); i++) {
                Rasterizer::Path& p = scene.paths[i];
                Rasterizer::Transform t = scene.ctm.concat(scene.ctms[i]);
                Rasterizer::Bounds clip = Rasterizer::Bounds(p.ref->bounds.unit(view.concat(t))).integral().intersect(bounds);
                if (clip.lx != clip.ux && clip.ly != clip.uy) {
                    CGContextSaveGState(ctx);
                    CGContextSetRGBFillColor(ctx, scene.colors[i].src2 / 255.0, scene.colors[i].src1 / 255.0, scene.colors[i].src0 / 255.0, scene.colors[i].src3 / 255.0);
                    CGContextConcatCTM(ctx, CGFromTransform(t));
                    CGMutablePathRef path = CGPathCreateMutable();
                    writePathToCGPath(p, path);
                    CGContextAddPath(ctx, path);
                    CGPathRelease(path);
                    CGContextFillPath(ctx);
                    CGContextRestoreGState(ctx);
                }
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
    
    static Rasterizer::Transform transformFromCG(CGAffineTransform t) {
        return Rasterizer::Transform(float(t.a), float(t.b), float(t.c), float(t.d), float(t.tx), float(t.ty));
    }
    static CGAffineTransform CGFromTransform(Rasterizer::Transform t) {
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
        
        CGTestScene() : rasterizerType(0) { contexts.resize(8), scenes.emplace_back(Rasterizer::Ref<Rasterizer::Scene>()); }
        void reset() { for (auto& ctx : contexts) ctx.reset(); }
        std::vector<Rasterizer::Context> contexts;
        RasterizerEvent::State state;
        int rasterizerType;
        std::vector<Rasterizer::Ref<Rasterizer::Scene>> scenes;
        BGRAColorConverter converter;
    };
    
    static void writeGlyphs(NSString *fontName, CGFloat pointSize, NSString *string, CGRect bounds, CGTestScene& _testScene) {
        CTFontDescriptorRef fontRef = CTFontDescriptorCreateWithNameAndSize ((__bridge CFStringRef)fontName, 1);
        CFURLRef url = (CFURLRef)CTFontDescriptorCopyAttribute(fontRef, kCTFontURLAttribute);
        CFRelease(fontRef);
        NSData *data = [NSData dataWithContentsOfURL:(__bridge NSURL *)url];
        CFRelease(url);
        Rasterizer::Scene& scene = *_testScene.scenes[0].ref;
        scene.empty();
        RasterizerTrueType::Font font;
        if (font.set(data.bytes, fontName.UTF8String) != 0) {
            if (string)
                Rasterizer::Path shapes = RasterizerTrueType::writeGlyphs(font, float(pointSize), Rasterizer::Colorant(0, 0, 0, 255), boundsFromCGRect(bounds), false, string.UTF8String, scene);
            else
                RasterizerTrueType::writeGlyphGrid(font, float(pointSize), Rasterizer::Colorant(0, 0, 0, 255), scene);
        }
    }
    
    static void renderScenes(Rasterizer::Ref<Rasterizer::Scene> *scenes, size_t scenesCount, Rasterizer::Transform *ctms, Rasterizer::Transform *gpuctms, bool even, Rasterizer::Colorant *colors, float width, std::vector<Rasterizer::Context>& contexts, Rasterizer::Bitmap bitmap, Rasterizer::Buffer *buffer, bool multithread) {
        Rasterizer::Path *paths = & scenes->ref->paths[0];
        size_t pathsCount = scenes->ref->paths.size();
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
                    contexts[idx].drawScenes(scenes, scenesCount, ctms, false, colors, width, b[idx], b[idx + 1]);
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
                    contexts[idx].drawScenes(scenes, scenesCount, ctms, false, colors, width, 0, pathsCount);
                });
            }
        } else {
            count = 1;
            if (buffer)
                contexts[0].setGPU(bitmap.width, bitmap.height, gpuctms);
            else
                contexts[0].setBitmap(bitmap, Rasterizer::Bounds(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX));
            contexts[0].drawScenes(scenes, scenesCount, ctms, false, colors, width, 0, pathsCount);
        }
        if (buffer) {
            std::vector<Rasterizer::Buffer::Entry> entries[count], *e = & entries[0];
            size_t begins[count], *b = begins;
            size_t size = Rasterizer::writeContextsToBuffer(& contexts[0], count, paths, ctms, colors, scenes->ref->clip, pathsCount, begins, *buffer);
            if (count == 1)
                Rasterizer::writeContextToBuffer(& contexts[0], paths, ctms, colors, b[0], e[0], *buffer);
            else {
                dispatch_apply(count, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                    Rasterizer::writeContextToBuffer(& contexts[idx], paths, ctms, colors, b[idx], e[idx], *buffer);
                });
            }
            for (int i = 0; i < count; i++)
                for (auto entry : e[i])
                    *(buffer->entries.alloc(1)) = entry;
            size_t end = buffer->entries.base[buffer->entries.end - 1].end;
            assert(size == end);
        }
    }
    static void drawTestScene(CGTestScene& testScene, const Rasterizer::Transform view, bool useOutline, CGContextRef ctx, CGColorSpaceRef dstSpace, Rasterizer::Bitmap bitmap, Rasterizer::Buffer *buffer, float dx, float dy) {
        Rasterizer::Bounds bounds(0, 0, bitmap.width, bitmap.height);
        size_t pathsCount = 0;
        std::vector<Rasterizer::Ref<Rasterizer::Scene>> visibles;
        for (int i = 0; i < testScene.scenes.size(); i++)
            if (testScene.scenes[i].ref->isVisible(view, bounds))
                visibles.emplace_back(testScene.scenes[i]), pathsCount += testScene.scenes[i].ref->paths.size();
        if (pathsCount == 0)
            return;
        if (testScene.rasterizerType == CGTestScene::kCoreGraphics)
            drawScenes(& visibles[0], visibles.size(), view, bounds, ctx);
        else {
            assert(sizeof(uint32_t) == sizeof(Rasterizer::Colorant));
            Rasterizer::Transform *ctms = (Rasterizer::Transform *)malloc(pathsCount * sizeof(view));
            Rasterizer::Transform *gpuctms = (Rasterizer::Transform *)malloc(pathsCount * sizeof(view));
            uint32_t *bgras = (uint32_t *)malloc(pathsCount * sizeof(uint32_t));
            Rasterizer::Colorant *colors = (Rasterizer::Colorant *)malloc(pathsCount * sizeof(Rasterizer::Colorant)), *color = colors;
            
            CGColorSpaceRef srcSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            testScene.converter.set(srcSpace, dstSpace);
            for (size_t i = 0, iz = 0; i < visibles.size(); i++) {
                Rasterizer::Scene& scene = *visibles[i].ref;
                testScene.converter.convert((uint32_t *)& scene.colors[0].src0, scene.paths.size(), bgras + iz);
                Rasterizer::Transform ctm = view.concat(scene.ctm);
                for (size_t j = 0; j < scene.paths.size(); iz++, j++)
                    ctms[iz] = ctm.concat(scene.ctms[j]);
            }
            memcpy(gpuctms, ctms, pathsCount * sizeof(view));
            CGColorSpaceRelease(srcSpace);
            for (int i = 0; i < pathsCount; i++, color++)
                new (color) Rasterizer::Colorant((uint8_t *)& bgras[i]);
        
            float width = useOutline ? 1.f : 0.f;
            if (useOutline) {
                Rasterizer::Colorant black(0, 0, 0, 255);
                memset_pattern4(colors, & black, pathsCount * sizeof(Rasterizer::Colorant));
            }
            if (dx != FLT_MAX) {
                Rasterizer::Scene& scene = *visibles[0].ref;
                size_t index = RasterizerWinding::pathIndexForPoint(& scene.paths[0], & scene.ctms[0], false, scene.clip, view.concat(scene.ctm), Rasterizer::Bounds(0.f, 0.f, bitmap.width, bitmap.height), 0, pathsCount, dx, dy);
                if (index != INT_MAX)
                    colors[index].src0 = 0, colors[index].src1 = 0, colors[index].src2 = 255, colors[index].src3 = 255;
            }
            renderScenes(& visibles[0], visibles.size(), ctms, gpuctms, false, colors, width, testScene.contexts, bitmap, buffer, testScene.rasterizerType == CGTestScene::kRasterizerMT);
            free(ctms), free(gpuctms), free(bgras), free(colors);
        }
    }
};
