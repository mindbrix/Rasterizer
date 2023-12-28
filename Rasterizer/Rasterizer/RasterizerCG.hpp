//
//  RasterizerCG.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 23/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"
#import <Accelerate/Accelerate.h>
#import <CoreGraphics/CoreGraphics.h>

struct RasterizerCG {
    struct Converter {
        void matchColors(Ra::Colorant *colorants, size_t size, CGColorSpaceRef destSpace) {
            if (colorants == nullptr || size == 0 || destSpace == nil)
                return;
            if (dstSpace != destSpace) {
                vImageConverter_Release(converter), CGColorSpaceRelease(dstSpace), dstSpace = CGColorSpaceRetain(destSpace);
                vImage_CGImageFormat srcFormat;  bzero(& srcFormat, sizeof(srcFormat));
                vImage_CGImageFormat dstFormat;  bzero(& dstFormat, sizeof(dstFormat));
                srcFormat.bitsPerComponent = dstFormat.bitsPerComponent = 8;
                srcFormat.bitsPerPixel = dstFormat.bitsPerPixel = 32;
                srcFormat.renderingIntent = dstFormat.renderingIntent = kCGRenderingIntentDefault;
                srcFormat.colorSpace = CGColorSpaceCreateDeviceRGB(), dstFormat.colorSpace = dstSpace;
                srcFormat.bitmapInfo = kCGImageAlphaFirst | kCGBitmapByteOrder32Little;
                dstFormat.bitmapInfo = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little;
                converter = vImageConverter_CreateWithCGImageFormat(& srcFormat, & dstFormat, NULL, kvImageNoFlags, NULL);
            }
            size_t colorSize = sizeof(uint32_t);
            auto colors = (uint32_t *)malloc(size * colorSize), counts = (uint32_t *)malloc(size * colorSize);
            uint32_t *cnt = counts, *src0 = (uint32_t *)colorants, *src = src0 + 1, *dst = colors, *end = src0 + size, last = *src0;
            do {
                while (*src == last && src < end)
                    src++;
                *dst++ = last, *cnt++ = uint32_t(src - src0), src0 = src, last = *src++;
            } while (src < end);
            
            size_t total = cnt - counts;
            vImage_Buffer srcBuffer;  vImageBuffer_Init(& srcBuffer, 1, total, 32, 0);
            vImage_Buffer dstBuffer;  vImageBuffer_Init(& dstBuffer, 1, total, 32, 0);
            memcpy(srcBuffer.data, colors, total * colorSize);
            vImageConvert_AnyToAny(converter, & srcBuffer, & dstBuffer, NULL, kvImageDoNotTile);
            cnt = counts, src = (uint32_t *)dstBuffer.data, dst = (uint32_t *)colorants;
            for (int i = 0; i < total; i++, src++, dst += *cnt, cnt++)
                memset_pattern4(dst, src, *cnt * colorSize);
            free(dstBuffer.data), free(srcBuffer.data), free(colors), free(counts);
        }
        ~Converter() {
            vImageConverter_Release(converter), CGColorSpaceRelease(dstSpace);
        }
        vImageConverterRef converter = nil;
        CGColorSpaceRef dstSpace = nil;
    };
    
    static bool isVisible(Ra::Bounds user, Ra::Transform ctm, Ra::Transform clip, Ra::Bounds deviceClip, float width) {
        if (clip.scale() == 0)
            return false;
        float uw = width < 0.f ? -width / ctm.scale() : width;
        Ra::Transform quad = user.inset(-uw, -uw).quad(ctm);
        Ra::Bounds dev = Ra::Bounds(quad).intersect(Ra::Bounds(clip)).intersect(deviceClip);
        Ra::Bounds soft = Ra::Bounds(clip.invert().concat(quad));
        return dev.lx < dev.ux && dev.ly < dev.uy && soft.lx < 1.f && soft.ux > 0.f && soft.ly < 1.f && soft.uy > 0.f;
    }
    
    static void drawList(Ra::SceneList& list, Ra::Transform view, Ra::Bounds device, Ra::Bounds deviceClip, float outlineWidth, CGContextRef ctx) {
        uint32_t ip, lastip = ~0;
        for (int j = 0; j < list.scenes.size(); j++) {
            Ra::Scene& scn = list.scenes[j];
            Ra::Transform ctm = list.ctms[j], clip, m;
            CGContextSaveGState(ctx);
            CGContextConcatCTM(ctx, CGFromTransform(ctm));
            CGContextSaveGState(ctx);
            
            clip = view.concat(Ra::Transform(1e12f, 0.f, 0.f, 1e12f, -5e11f, -5e11f));
            
            for (size_t i = 0; i < scn.count; i++) {
                if (scn.flags->base[i] & Ra::Scene::Flags::kInvisible)
                    continue;
                
                Ra::Geometry *g = scn.paths->base[i].ptr;
                Ra::Transform t = scn.ctms->base[i];
                m = view.concat(ctm).concat(t);
                ip = scn.clipCache->ips.base[i];
                if (ip != lastip) {
                    lastip = ip;
                    CGContextRestoreGState(ctx);
                    CGContextSaveGState(ctx);
                    Ra::Bounds *pclip = ip ? scn.clipCache->entryAt(i) : nullptr;
                    if (pclip)
                        CGContextClipToRect(ctx, CGRectFromBounds(*pclip));
                    clip = ip && pclip ? pclip->quad(view.concat(ctm)) : Ra::Transform(1e12f, 0.f, 0.f, 1e12f, -5e11f, -5e11f);
                }
                
                if (isVisible(g->bounds, m, clip, deviceClip, scn.widths->base[i])) {
                    CGContextSaveGState(ctx);
                    CGContextConcatCTM(ctx, CGFromTransform(t));
                    writePathToCGContext(g, ctx);
                    if (outlineWidth || scn.widths->base[i]) {
                        CGContextSetRGBStrokeColor(ctx, scn.colors->base[i].r / 255.0, scn.colors->base[i].g / 255.0, scn.colors->base[i].b / 255.0, scn.colors->base[i].a / 255.0);
                        CGContextSetLineWidth(ctx, outlineWidth || scn.widths->base[i] < 0.f ? (CGFloat)-109.05473e+14 : scn.widths->base[i]);
                        bool square = scn.flags->base[i] & Ra::Scene::kSquareCap;
                        bool round = scn.flags->base[i] & Ra::Scene::kRoundCap;
                        CGContextSetLineCap(ctx, round ? kCGLineCapRound : square ? kCGLineCapSquare : kCGLineCapButt);
                        CGContextStrokePath(ctx);
                    } else {
                        CGContextSetRGBFillColor(ctx, scn.colors->base[i].r / 255.0, scn.colors->base[i].g / 255.0, scn.colors->base[i].b / 255.0, scn.colors->base[i].a / 255.0);
                        if (scn.flags->base[i] & Ra::Scene::kFillEvenOdd)
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
    
    static Ra::Transform transformFromCG(CGAffineTransform t) {
        return Ra::Transform(float(t.a), float(t.b), float(t.c), float(t.d), float(t.tx), float(t.ty));
    }
    static CGAffineTransform CGFromTransform(Ra::Transform t) {
        return CGAffineTransformMake(t.a, t.b, t.c, t.d, t.tx, t.ty);
    }
    static Ra::Bounds BoundsFromCGRect(CGRect rect) {
        return Ra::Bounds(float(CGRectGetMinX(rect)), float(CGRectGetMinY(rect)), float(CGRectGetMaxX(rect)), float(CGRectGetMaxY(rect)));
    }
    static CGRect CGRectFromBounds(Ra::Bounds bounds) {
        return CGRectMake(bounds.lx, bounds.ly, bounds.ux - bounds.lx, bounds.uy - bounds.ly);
    }
    static void writePathToCGContext(Ra::Geometry *g, CGContextRef ctx) {
        for (size_t index = 0; index < g->types.end; ) {
            float *p = g->points.base + index * 2;
            switch (*(g->types.base + index)) {
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
    static void screenGrabToPDF(Ra::SceneList& list, Ra::Transform ctm, CGRect bounds) {
        NSArray *downloads = [NSFileManager.defaultManager URLsForDirectory: NSDownloadsDirectory inDomains:NSUserDomainMask];
        NSURL *fileURL = [downloads.firstObject URLByAppendingPathComponent:@"screenGrab.pdf"];
        CGRect mediaBox = bounds;
        CGContextRef ctx = CGPDFContextCreateWithURL((__bridge CFURLRef)fileURL, & mediaBox, NULL);
        CGPDFContextBeginPage(ctx, NULL);
        CGContextConcatCTM(ctx, CGFromTransform(ctm));
        drawList(list, ctm, BoundsFromCGRect(bounds), BoundsFromCGRect(bounds), 0.f, ctx);
        CGPDFContextEndPage(ctx);
        CGPDFContextClose(ctx);
        CGContextRelease(ctx);
    }
    
    static NSURL *fontURL(NSString *fontName) {
        CTFontDescriptorRef fontRef = CTFontDescriptorCreateWithNameAndSize((__bridge CFStringRef)fontName, 1);
        NSURL *URL = (__bridge_transfer NSURL *)CTFontDescriptorCopyAttribute(fontRef, kCTFontURLAttribute);
        CFRelease(fontRef);
        return URL;
    }
};

typedef RasterizerCG RaCG;
