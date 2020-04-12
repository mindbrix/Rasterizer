//
//  RasterizerCoreGraphics.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 23/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "RasterizerQueue.hpp"
#import "RasterizerState.hpp"
#import "RasterizerDB.hpp"
#import "RasterizerWinding.hpp"
#import "RasterizerFont.hpp"
#import <Accelerate/Accelerate.h>
#import <CoreGraphics/CoreGraphics.h>

struct RasterizerCG {
    static void drawScenes(Ra::SceneList& list, const Ra::Transform view, const Ra::Bounds device, float outlineWidth, CGContextRef ctx) {
        for (int j = 0; j < list.scenes.size(); j++) {
            Ra::Scene& scene = list.scenes[j];
            Ra::Transform ctm = list.ctms[j], clip = list.clips[j], om;
            CGContextSaveGState(ctx);
            CGContextClipToRect(ctx, CGRectMake(clip.tx, clip.ty, clip.a, clip.d));
            for (size_t i = 0; i < scene.count; i++) {
                Ra::Path& path = scene.paths[i];
                Ra::Transform t = ctm.concat(scene.ctms[i]);
                if (Ra::isVisible(path.ref->bounds, view.concat(t), view.concat(clip), device, scene.widths[i])) {
                    CGContextSaveGState(ctx);
                    CGContextConcatCTM(ctx, CGFromTransform(t));
                    writePathToCGContext(path, ctx);
                    if (outlineWidth || scene.widths[i]) {
                        if (outlineWidth == 0.f)
                            CGContextSetRGBStrokeColor(ctx, scene.colors[i].src2 / 255.0, scene.colors[i].src1 / 255.0, scene.colors[i].src0 / 255.0, scene.colors[i].src3 / 255.0);
                        CGContextSetLineWidth(ctx, outlineWidth ? (CGFloat)-109.05473e+14 : (scene.widths[i]) / sqrtf(fabsf(scene.ctms[i].det())));
                        bool end = scene.flags[i] & Ra::Scene::kOutlineEndCap;
                        bool round = scene.flags[i] & Ra::Scene::kOutlineRounded;
                        CGContextSetLineCap(ctx, round ? kCGLineCapRound : end ? kCGLineCapSquare : kCGLineCapButt);
                        CGContextStrokePath(ctx);
                    } else {
                        CGContextSetRGBFillColor(ctx, scene.colors[i].src2 / 255.0, scene.colors[i].src1 / 255.0, scene.colors[i].src0 / 255.0, scene.colors[i].src3 / 255.0);
                        if (scene.flags[i] & Ra::Scene::kFillEvenOdd)
                            CGContextEOFillPath(ctx);
                        else
                            CGContextFillPath(ctx);
                    }
                    CGContextRestoreGState(ctx);
                }
            }
            CGContextRestoreGState(ctx);
        }
    }
    struct BGRAColorConverter {
        BGRAColorConverter() { reset(); }
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
        void convert(uint64_t hash, void *src, size_t count, void *dst) {
            if (count && converter) {
                hash += CFHash(dstFormat.colorSpace);
                size_t size = count * sizeof(uint32_t);
                auto it = cache.find(hash);
                if (it != cache.end()) {
                    memcpy(dst, it->second.get(), size);
                } else {
                    std::shared_ptr<void> buf(malloc(size), free);
                    cache.emplace(hash, buf);
                    vImage_Buffer sourceBuffer, destBuffer;
                    vImageBuffer_Init(& sourceBuffer, 1, count, 32, kvImageNoAllocate);
                    vImageBuffer_Init(& destBuffer, 1, count, 32, kvImageNoAllocate);
                    sourceBuffer.data = src, destBuffer.data = buf.get();
                    vImageConvert_AnyToAny(converter, &sourceBuffer, &destBuffer, NULL, kvImageNoFlags);
                    memcpy(dst, buf.get(), size);
                }
            }
        }
        vImageConverterRef converter = nullptr;
        vImage_CGImageFormat srcFormat, dstFormat;
        std::unordered_map<uint64_t, std::shared_ptr<void>> cache;
    };
    
    static Ra::Transform transformFromCG(CGAffineTransform t) {
        return Ra::Transform(float(t.a), float(t.b), float(t.c), float(t.d), float(t.tx), float(t.ty));
    }
    static CGAffineTransform CGFromTransform(Ra::Transform t) {
        return CGAffineTransformMake(t.a, t.b, t.c, t.d, t.tx, t.ty);
    }
    static Ra::Bounds boundsFromCGRect(CGRect rect) {
        return Ra::Bounds(float(rect.origin.x), float(rect.origin.y), float(rect.origin.x + rect.size.width), float(rect.origin.y + rect.size.height));
    }
    static CGRect CGRectFromBounds(Ra::Bounds bounds) {
        return CGRectMake(bounds.lx, bounds.ly, bounds.ux - bounds.lx, bounds.uy - bounds.ly);
    }
    static void writePathToCGContext(Ra::Path path, CGContextRef ctx) {
        for (size_t index = 0; index < path.ref->types.size(); ) {
            float *p = & path.ref->points[0] + index * 2;
            switch (path.ref->types[index]) {
                case Ra::Geometry::kMove:
                    CGContextMoveToPoint(ctx, p[0], p[1]);
                    index++;
                    break;
                case Ra::Geometry::kLine:
                    CGContextAddLineToPoint(ctx, p[0], p[1]);
                    index++;
                    break;
                case Ra::Geometry::kQuadratic:
                    CGContextAddQuadCurveToPoint(ctx, p[0], p[1], p[2], p[3]);
                    index += 2;
                    break;
                case Ra::Geometry::kCubic:
                    CGContextAddCurveToPoint(ctx, p[0], p[1], p[2], p[3], p[4], p[5]);
                    index += 3;
                    break;
                case Ra::Geometry::kClose:
                    CGContextClosePath(ctx);
                    index++;
                    break;
            }
        }
    }
    struct CGTestContext {
        static const int kQueueCount = 8;
        void reset() { for (auto& ctx : contexts) ctx.reset(); }
        BGRAColorConverter converter;
        Ra::Context contexts[kQueueCount];
        RasterizerQueue queues[kQueueCount];
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
    
    struct ThreadInfo {
        Ra::Context *context;
        Ra::SceneList *list;
        Ra::Transform view, *ctms, *clips;
        uint32_t *idxs;
        Ra::Colorant *colors;
        float *widths, outlineWidth;
        Ra::Bounds *bounds;
        Ra::Buffer *buffer;
        std::vector<Ra::Buffer::Entry> *entries;
        size_t begin;
        
        static void drawList(void *info) {
            ThreadInfo *ti = (ThreadInfo *)info;
            ti->context->drawList(*ti->list, ti->view, ti->idxs, ti->ctms, ti->colors, ti->clips, ti->widths, ti->bounds, ti->outlineWidth, ti->buffer);
        }
        static void writeContexts(void *info) {
            ThreadInfo *ti = (ThreadInfo *)info;
            Ra::writeContextToBuffer(*ti->list, ti->context, ti->idxs, ti->begin, *ti->entries, *ti->buffer);
        }
    };
    static void renderScenes(Ra::SceneList& list, RasterizerState& state, size_t pathsCount, uint32_t *idxs, Ra::Transform *ctms, Ra::Colorant *colors, Ra::Transform *clips, float *widths, Ra::Bounds *bounds, float outlineWidth, Ra::Context *contexts, Ra::Buffer *buffer, bool multithread, RasterizerQueue *queues) {
        size_t eiz = 0, total = 0, count, divisions = CGTestContext::kQueueCount, base, i, iz, izeds[divisions + 1], target, *izs = izeds;
        for (int j = 0; j < list.scenes.size(); j++)
            eiz += list.scenes[j].count, total += list.scenes[j].weight;
        if (buffer)
            buffer->useCurves = state.useCurves;
        ThreadInfo threadInfo[CGTestContext::kQueueCount], *ti = threadInfo;
        if (multithread) {
            ti->context = contexts, ti->list = & list, ti->view = state.view, ti->idxs = idxs, ti->ctms = ctms, ti->clips = clips, ti->colors = colors, ti->widths = widths, ti->bounds = bounds, ti->outlineWidth = outlineWidth, ti->buffer = buffer;
            for (i = 1; i < CGTestContext::kQueueCount; i++)
                threadInfo[i] = threadInfo[0], threadInfo[i].context += i;
            izeds[0] = 0, izeds[divisions] = eiz;
            auto scene = & list.scenes[0];
            for (count = base = iz = 0, i = 1; i < divisions; i++) {
                for (target = total * i / divisions; count < target; iz++) {
                    if (iz - base == scene->count)
                        scene++, base = iz;
                    count += scene->paths[iz - base].ref->types.size();
                }
                izeds[i] = iz;
            }
            count = CGTestContext::kQueueCount;
            for (i = 0; i < count; i++)
                contexts[i].setGPU(state.device.ux, state.device.uy, pathsCount, izs[i], izs[i + 1]);
            RasterizerQueue::scheduleAndWait(queues, CGTestContext::kQueueCount, ThreadInfo::drawList, threadInfo, sizeof(ThreadInfo), count);
        } else {
            count = 1;
            contexts[0].setGPU(state.device.ux, state.device.uy, pathsCount, 0, eiz);
            contexts[0].drawList(list, state.view, idxs, ctms, colors, clips, widths, bounds, outlineWidth, buffer);
        }
        std::vector<Ra::Buffer::Entry> entries[count];
        size_t begins[count], size = Ra::writeContextsToBuffer(list, contexts, count, colors, ctms, clips, widths, bounds, eiz, begins, *buffer);
        if (count == 1)
            Ra::writeContextToBuffer(list, contexts, idxs, begins[0], entries[0], *buffer);
        else {
            for (i = 0; i < count; i++)
                threadInfo[i].begin = begins[i], threadInfo[i].entries = & entries[i];
            RasterizerQueue::scheduleAndWait(queues, CGTestContext::kQueueCount, ThreadInfo::writeContexts, threadInfo, sizeof(ThreadInfo), count);
        }
        for (int i = 0; i < count; i++)
            for (auto entry : entries[i])
                *(buffer->entries.alloc(1)) = entry;
        size_t end = buffer->entries.end == 0 ? 0 : buffer->entries.base[buffer->entries.end - 1].end;
        assert(size >= end);
    }
    static void drawTestScene(CGTestContext& testScene, Ra::SceneList& list, RasterizerState& state, Ra::Buffer *buffer) {
        Ra::SceneList visibles;
        list.writeVisibles(state.view, state.device, visibles);
        size_t pathsCount = visibles.pathsCount;
        if (pathsCount == 0)
            return;
        assert(sizeof(uint32_t) == sizeof(Ra::Colorant));
        uint32_t *idxs = (uint32_t *)malloc(pathsCount * sizeof(uint32_t));
        Ra::Transform *ctms = (Ra::Transform *)malloc(pathsCount * sizeof(state.view));
        Ra::Colorant *colors = (Ra::Colorant *)malloc(pathsCount * sizeof(Ra::Colorant));
        Ra::Transform *clips = (Ra::Transform *)malloc(pathsCount * sizeof(state.view));
        float *widths = (float *)malloc(pathsCount * sizeof(float));
        Ra::Bounds *bounds = (Ra::Bounds *)malloc(pathsCount * sizeof(Ra::Bounds));
        
        Ra::Scene *scene = & visibles.scenes[0];
        for (size_t i = 0, iz = 0; i < visibles.scenes.size(); i++, iz += scene->count, scene++)
            testScene.converter.convert(scene->colorHash(), & scene->colors[0].src0, scene->count, colors + iz);
        if (state.outlineWidth) {
            Ra::Colorant black(0, 0, 0, 255);
            memset_pattern4(colors, & black, pathsCount * sizeof(Ra::Colorant));
        }
        if (state.index != INT_MAX)
            colors[state.index].src0 = 0, colors[state.index].src1 = 0, colors[state.index].src2 = 255, colors[state.index].src3 = 255;
        
        renderScenes(visibles, state, pathsCount, idxs, ctms, colors, clips, widths, bounds, state.outlineWidth, & testScene.contexts[0], buffer, true, testScene.queues);
        free(idxs), free(ctms), free(colors), free(clips), free(widths), free(bounds);
    }
};

typedef RasterizerCG RaCG;
