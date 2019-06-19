//
//  RasterizerCoreGraphics.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 23/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "RasterizerState.hpp"
#import "RasterizerDB.hpp"
#import "RasterizerWinding.hpp"
#import "RasterizerFont.hpp"
#import <Accelerate/Accelerate.h>
#import <CoreGraphics/CoreGraphics.h>

struct RasterizerCG {
    static void drawScenes(Rasterizer::SceneList& list, const Rasterizer::Transform view, const Rasterizer::Bounds bounds, CGContextRef ctx) {
        for (int j = 0; j < list.scenes.size(); j++) {
            CGContextSaveGState(ctx);
            Rasterizer::Scene& scene = *list.scenes[j].ref;
            Rasterizer::Transform ctm = list.ctms[j], clip = list.clips[j];
            CGContextClipToRect(ctx, CGRectMake(clip.tx, clip.ty, clip.a, clip.d));
            for (size_t i = 0; i < scene.paths.size(); i++) {
                Rasterizer::Path& p = scene.paths[i];
                Rasterizer::Transform t = ctm.concat(scene.ctms[i]);
                if (Rasterizer::isVisible(p.ref->bounds, view.concat(t), view.concat(clip), bounds)) {
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
            CGContextRestoreGState(ctx);
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
        void convert(void *src, size_t size, void *dst) {
            if (size && converter) {
                vImage_Buffer sourceBuffer, destBuffer;
                vImageBuffer_Init(& sourceBuffer, 1, size, 32, kvImageNoAllocate);
                vImageBuffer_Init(& destBuffer, 1, size, 32, kvImageNoAllocate);
                sourceBuffer.data = src, destBuffer.data = dst;
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
    
    struct CGTestContext {
        enum RasterizerType : int { kRasterizerMT = 0, kRasterizer, kCoreGraphics, kRasterizerCount };
        
        CGTestContext() : rasterizerType(0) { contexts.resize(8); }
        void reset() { for (auto& ctx : contexts) ctx.reset(); }
        std::vector<Rasterizer::Context> contexts;
        int rasterizerType;
        BGRAColorConverter converter;
    };
    
    static void writeFontsTable(RasterizerDB& db) {
        const char *values[5], *kFontsTable = "fonts";
        NSArray *names = (__bridge_transfer NSArray *)CTFontManagerCopyAvailablePostScriptNames();
        const char *columnNames[] = { "name", "_url", "_family", "_style" };
        db.beginImport(kFontsTable, columnNames, 4);
        for (NSString *fontName in names) {
            if (![fontName hasPrefix:@"."]) {
                CTFontDescriptorRef fontRef = CTFontDescriptorCreateWithNameAndSize((__bridge CFStringRef)fontName, 1);
                NSURL *fontURL = (__bridge_transfer NSURL *)CTFontDescriptorCopyAttribute(fontRef, kCTFontURLAttribute);
                NSString *fontFamily = (__bridge_transfer NSString *)CTFontDescriptorCopyAttribute(fontRef, kCTFontFamilyNameAttribute);
                NSString *fontStyle = (__bridge_transfer NSString *)CTFontDescriptorCopyAttribute(fontRef, kCTFontStyleNameAttribute);
                CFRelease(fontRef);
                values[0] = fontName.UTF8String, values[1] = [fontURL.absoluteString stringByRemovingPercentEncoding].UTF8String, values[2] = fontFamily.UTF8String, values[3] = fontStyle.UTF8String;
                db.insert(kFontsTable, 4, (char **)values);
            }
        }
        db.endImport(kFontsTable, columnNames, 4);
    }
    
    static NSURL *fontURL(NSString *fontName) {
        CTFontDescriptorRef fontRef = CTFontDescriptorCreateWithNameAndSize((__bridge CFStringRef)fontName, 1);
        NSURL *URL = (__bridge_transfer NSURL *)CTFontDescriptorCopyAttribute(fontRef, kCTFontURLAttribute);
        CFRelease(fontRef);
        return URL;
    }
    static void writeGlyphs(NSString *fontName, CGFloat pointSize, NSString *string, CGRect bounds, Rasterizer::Scene& scene) {
        NSData *data = [NSData dataWithContentsOfURL:fontURL(fontName)];
        RasterizerFont font;
        if (font.set(data.bytes, fontName.UTF8String) != 0) {
            if (string)
                RasterizerFont::writeGlyphs(font, float(pointSize), Rasterizer::Colorant(0, 0, 0, 255), boundsFromCGRect(bounds), false, false, false, string.UTF8String, scene);
            else
                RasterizerFont::writeGlyphGrid(font, float(pointSize), Rasterizer::Colorant(0, 0, 0, 255), scene);
        }
    }
    
    static void renderScenes(Rasterizer::SceneList& list, Rasterizer::Transform *ctms, Rasterizer::Transform *gpuctms, bool even, Rasterizer::Colorant *colors, Rasterizer::Transform *clips, float width, Rasterizer::Context *contexts, size_t contextsCount, Rasterizer::Bitmap bitmap, Rasterizer::Buffer *buffer, bool multithread) {
        size_t eiz = 0, total = 0;
        for (int j = 0; j < list.scenes.size(); j++) {
            Rasterizer::Scene& scene = *list.scenes[j].ref;
            eiz += scene.paths.size();
            for (int p = 0; p < scene.paths.size(); p++)
                total += scene.paths[p].ref->atomsCount ?: (scene.paths[p].ref->shapes ? scene.paths[p].ref->end >> 4 : 0);
        }
        size_t slice, ly, uy, count, divisions = contextsCount, base, i, iz, izeds[divisions + 1], target, *izs = izeds;
        if (multithread) {
            if (buffer) {
                izeds[0] = 0, izeds[divisions] = eiz;
                auto scene = & list.scenes[0];
                for (count = base = iz = 0, i = 1; i < divisions; i++) {
                    for (target = total * i / divisions; count < target; iz++) {
                        if (iz - base == scene->ref->paths.size())
                            scene++, base = iz;
                        Rasterizer::Path& path = scene->ref->paths[iz - base];
                        count += path.ref->atomsCount ?: (path.ref->shapes ? path.ref->shapesCount >> 4: 0);
                    }
                    izeds[i] = iz;
                }
                for (i = 0; i < divisions; i++)
                    contexts[i].setGPU(bitmap.width, bitmap.height, gpuctms);
                dispatch_apply(divisions, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                    contexts[idx].drawScenes(list, ctms, false, colors, clips, width, izs[idx], izs[idx + 1]);
                });
                count = divisions;
            } else {
                slice = (bitmap.height + contextsCount - 1) / contextsCount, slice = slice < 64 ? 64 : slice;
                for (count = ly = 0; ly < bitmap.height; ly = uy) {
                    uy = ly + slice, uy = uy < bitmap.height ? uy : bitmap.height;
                    contexts[count].setBitmap(bitmap, Rasterizer::Bounds(0, ly, bitmap.width, uy));
                    count++;
                }
                dispatch_apply(count, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                    contexts[idx].drawScenes(list, ctms, false, colors, clips, width, 0, eiz);
                });
            }
        } else {
            count = 1;
            if (buffer)
                contexts[0].setGPU(bitmap.width, bitmap.height, gpuctms);
            else
                contexts[0].setBitmap(bitmap, Rasterizer::Bounds(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX));
            contexts[0].drawScenes(list, ctms, false, colors, clips, width, 0, eiz);
        }
        if (buffer) {
            std::vector<Rasterizer::Buffer::Entry> entries[count], *e = & entries[0];
            size_t begins[count], *b = begins;
            size_t size = Rasterizer::writeContextsToBuffer(contexts, count, ctms, colors, clips, eiz, begins, *buffer);
            if (count == 1)
                Rasterizer::writeContextToBuffer(contexts, list, ctms, colors, b[0], 0, e[0], *buffer);
            else {
                dispatch_apply(count, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t idx) {
                    Rasterizer::writeContextToBuffer(& contexts[idx], list, ctms, colors, b[idx], izs[idx], e[idx], *buffer);
                });
            }
            for (int i = 0; i < count; i++)
                for (auto entry : e[i])
                    *(buffer->entries.alloc(1)) = entry;
            size_t end = buffer->entries.base[buffer->entries.end - 1].end;
            assert(size >= end);
        }
    }
    static void drawTestScene(CGTestContext& testScene, Rasterizer::SceneList& list, const Rasterizer::Transform view, bool useOutline, CGContextRef ctx, CGColorSpaceRef dstSpace, Rasterizer::Bitmap bitmap, Rasterizer::Buffer *buffer, size_t index) {
        Rasterizer::Bounds bounds(0, 0, bitmap.width, bitmap.height);
        Rasterizer::SceneList visibles;
        size_t pathsCount = list.writeVisibles(view, bounds, visibles);
        if (pathsCount == 0)
            return;
        if (testScene.rasterizerType == CGTestContext::kCoreGraphics)
            drawScenes(visibles, view, bounds, ctx);
        else {
            assert(sizeof(uint32_t) == sizeof(Rasterizer::Colorant));
            Rasterizer::Transform *ctms = (Rasterizer::Transform *)malloc(pathsCount * sizeof(view));
            Rasterizer::Transform *gpuctms = (Rasterizer::Transform *)malloc(pathsCount * sizeof(view));
            Rasterizer::Colorant *colors = (Rasterizer::Colorant *)malloc(pathsCount * sizeof(Rasterizer::Colorant));
            Rasterizer::Transform *clips = (Rasterizer::Transform *)malloc(pathsCount * sizeof(view));
            CGColorSpaceRef srcSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            testScene.converter.set(srcSpace, dstSpace);
            for (size_t i = 0, iz = 0; i < visibles.scenes.size(); i++) {
                Rasterizer::Scene& scene = *visibles.scenes[i].ref;
                testScene.converter.convert(& scene.colors[0].src0, scene.paths.size(), colors + iz);
                Rasterizer::Transform ctm = view.concat(visibles.ctms[i]);
                Rasterizer::Transform clip = view.concat(visibles.clips[i]);
                for (size_t j = 0; j < scene.paths.size(); iz++, j++)
                    ctms[iz] = ctm.concat(scene.ctms[j]), clips[iz] = clip;
            }
            memcpy(gpuctms, ctms, pathsCount * sizeof(view));
            CGColorSpaceRelease(srcSpace);
            
            float width = useOutline ? 1.f : 0.f;
            if (useOutline) {
                Rasterizer::Colorant black(0, 0, 0, 255);
                memset_pattern4(colors, & black, pathsCount * sizeof(Rasterizer::Colorant));
            }
            if (index != INT_MAX)
                colors[index].src0 = 0, colors[index].src1 = 0, colors[index].src2 = 255, colors[index].src3 = 255;
            
            renderScenes(visibles, ctms, gpuctms, false, colors, clips, width, & testScene.contexts[0], testScene.contexts.size(), bitmap, buffer, testScene.rasterizerType == CGTestContext::kRasterizerMT);
            free(ctms), free(gpuctms), free(colors), free(clips);
        }
    }
};
