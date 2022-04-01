//
//  RasterizerCG.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 23/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "RasterizerState.hpp"
#import <Accelerate/Accelerate.h>
#import <CoreGraphics/CoreGraphics.h>

struct RasterizerCG {
    struct Converter {
        void matchColors(Ra::Colorant *colors, size_t size, CGColorSpaceRef destSpace) {
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
            uint32_t last = 0, count = 0, cols[size], counts[size], *cnt = counts, *src = (uint32_t *)colors, *dst = cols;
            for (int i = 0; i < size; i++, src++) {
                if (*src != last) {
                    if (count)
                        *cnt++ = count;
                    count = 1, last = *dst++ = *src;
                } else
                    count++;
            }
            if (count)
                *cnt++ = count;
            
            size_t total = cnt - counts;
            vImage_Buffer srcBuffer;  vImageBuffer_Init(& srcBuffer, 1, total, 32, 0);
            vImage_Buffer dstBuffer;  vImageBuffer_Init(& dstBuffer, 1, total, 32, 0);
            memcpy(srcBuffer.data, cols, total * sizeof(*colors));
            vImageConvert_AnyToAny(converter, & srcBuffer, & dstBuffer, NULL, kvImageDoNotTile);
            cnt = counts, src = (uint32_t *)dstBuffer.data, dst = (uint32_t *)colors;
            for (int i = 0; i < total; i++, src++, dst += *cnt, cnt++)
                memset_pattern4(dst, src, *cnt * sizeof(*dst));
            free(dstBuffer.data), free(srcBuffer.data);
        }
        ~Converter() {
            vImageConverter_Release(converter), CGColorSpaceRelease(dstSpace);
        }
        vImageConverterRef converter = nil;
        CGColorSpaceRef dstSpace = nil;
    };
    
    static bool isVisible(Ra::Bounds user, Ra::Transform ctm, Ra::Transform clip, Ra::Bounds device, float width) {
        if (clip.scale() == 0)
            return false;
        float uw = width < 0.f ? -width / ctm.scale() : width;
        Ra::Transform unit = user.inset(-uw, -uw).unit(ctm), inv = clip.invert();
        Ra::Bounds clipBounds = Ra::Bounds(clip), unitBounds = Ra::Bounds(unit);
        Ra::Bounds dev = unitBounds.intersect(device).intersect(clipBounds);
        Ra::Bounds clu = Ra::Bounds(inv.concat(unit));
        return dev.lx < dev.ux && dev.ly < dev.uy && clu.lx < 1.f && clu.ux > 0.f && clu.ly < 1.f && clu.uy > 0.f;
    }
    
    static void drawList(Ra::SceneList& list, RasterizerState& state, Ra::TransferFunction transferFunction, CGContextRef ctx) {
        uint32_t ip, lastip = ~0;
        for (int j = 0; j < list.scenes.size(); j++) {
            Ra::Scene& scn = list.scenes[j];
            if (transferFunction)
                (*transferFunction)(0, scn.count, j, scn.bnds->base,
                     & scn.ctms->src[0], scn.ctms->base, & scn.colors->src[0], scn.colors->base,
                     & scn.widths->src[0], scn.widths->base, & scn.flags->src[0], scn.flags->base, & state);
            Ra::Transform ctm = list.ctms[j], clip, m;
            CGContextSaveGState(ctx);
            CGContextConcatCTM(ctx, CGFromTransform(ctm));
            CGContextSaveGState(ctx);
            
            clip = state.view.concat(Ra::Transform(1e12f, 0.f, 0.f, 1e12f, -5e11f, -5e11f));
            
            for (size_t i = 0; i < scn.count; i++) {
                if (scn.flags->base[i] & Ra::Scene::Flags::kInvisible)
                    continue;
                
                Ra::Path& path = scn.paths->base[i];
                Ra::Transform t = scn.ctms->base[i];
                m = state.view.concat(ctm).concat(t);
                ip = scn.clipCache->ips.base[i];
                if (ip != lastip) {
                    lastip = ip;
                    CGContextRestoreGState(ctx);
                    CGContextSaveGState(ctx);
                    Ra::Bounds *pclip = ip ? scn.clipCache->entryAt(i) : nullptr;
                    if (pclip)
                        CGContextClipToRect(ctx, CGRectFromBounds(*pclip));
                    clip = ip && pclip ? pclip->unit(state.view.concat(ctm)) : Ra::Transform(1e12f, 0.f, 0.f, 1e12f, -5e11f, -5e11f);
                }
                
                if (isVisible(path->bounds, m, clip, state.device, scn.widths->base[i])) {
                    CGContextSaveGState(ctx);
                    CGContextConcatCTM(ctx, CGFromTransform(t));
                    writePathToCGContext(path, ctx);
                    if (state.outlineWidth || scn.widths->base[i]) {
                        CGContextSetRGBStrokeColor(ctx, scn.colors->base[i].r / 255.0, scn.colors->base[i].g / 255.0, scn.colors->base[i].b / 255.0, scn.colors->base[i].a / 255.0);
                        CGContextSetLineWidth(ctx, state.outlineWidth || scn.widths->base[i] < 0.f ? (CGFloat)-109.05473e+14 : scn.widths->base[i]);
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
    static void writePathToCGContext(Ra::Path path, CGContextRef ctx) {
        for (size_t index = 0; index < path->types.end; ) {
            float *p = path->points.base + index * 2;
            switch (*(path->types.base + index)) {
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
    static void screenGrabToPDF(Ra::SceneList& list, RasterizerState& state, CGRect bounds) {
        NSArray *downloads = [NSFileManager.defaultManager URLsForDirectory: NSDownloadsDirectory inDomains:NSUserDomainMask];
        NSURL *fileURL = [downloads.firstObject URLByAppendingPathComponent:@"test.pdf"];
        CGRect mediaBox = bounds;
        CGContextRef ctx = CGPDFContextCreateWithURL((__bridge CFURLRef)fileURL, & mediaBox, NULL);
        CGPDFContextBeginPage(ctx, NULL);
        state.update(1.0, bounds.size.width, bounds.size.height);
        CGContextConcatCTM(ctx, CGFromTransform(state.ctm));
        drawList(list, state, NULL, ctx);
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
    
    static void createBGRATexture(Ra::Image *img, Ra::Image *tex, CGColorSpaceRef dstSpace) {
        tex->init(nullptr, 4 * img->width * img->height, img->width, img->height), tex->hash = img->hash;
        NSData *data = [NSData dataWithBytes:img->memory->addr length:img->memory->size];
        CGImageSourceRef cgImageSrc = CGImageSourceCreateWithData((CFDataRef)data, NULL);
        if (CGImageSourceGetCount(cgImageSrc)) {
            CGImageRef cgImage = CGImageSourceCreateImageAtIndex(cgImageSrc, 0, NULL);
            vImage_CGImageFormat srcFormat;  bzero(& srcFormat, sizeof(srcFormat));
                srcFormat.bitsPerComponent = uint32_t(CGImageGetBitsPerComponent(cgImage));
                srcFormat.bitsPerPixel = uint32_t(CGImageGetBitsPerPixel(cgImage));
                srcFormat.colorSpace = CGImageGetColorSpace(cgImage);
                srcFormat.bitmapInfo = CGImageGetBitmapInfo(cgImage);
                srcFormat.renderingIntent = CGImageGetRenderingIntent(cgImage);
            vImage_CGImageFormat dstFormat;  bzero(& dstFormat, sizeof(dstFormat));
                dstFormat.bitsPerComponent = 8;
                dstFormat.bitsPerPixel = 32;
                dstFormat.colorSpace = dstSpace;
                dstFormat.bitmapInfo = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little;
                dstFormat.renderingIntent = kCGRenderingIntentDefault;
            vImage_Error error = kvImageNoError;
            vImageConverterRef converter = vImageConverter_CreateWithCGImageFormat(& srcFormat, & dstFormat, NULL, kvImageNoFlags, & error);
            vImage_Buffer srcBuffer;  vImageBuffer_InitWithCGImage(& srcBuffer, & srcFormat, NULL, cgImage, 0);
            vImage_Buffer dstBuffer;  vImageBuffer_Init(& dstBuffer, srcBuffer.height, srcBuffer.width, dstFormat.bitsPerPixel, 0);
            vImageConvert_AnyToAny(converter, & srcBuffer, & dstBuffer, NULL, kvImageDoNotTile);
            auto src = (uint8_t *)dstBuffer.data, dst = tex->memory->addr;
            for (int row = 0; row < tex->height; row++, src += dstBuffer.rowBytes, dst += 4 * tex->width)
                memcpy(dst, src, 4 * tex->width);
            CFRelease(cgImage), free(dstBuffer.data), free(srcBuffer.data), vImageConverter_Release(converter);
        }
        CFRelease(cgImageSrc);
    }
    static std::vector<Ra::Image> makeBGRATextures(Ra::Image *images, size_t count, CGColorSpaceRef dstSpace) {
        std::vector<Ra::Image> textures(count);
        Ra::Image *img = images, *tex = textures.data();
        for (int i = 0; i < count; i++, img++, tex++)
            createBGRATexture(img, tex, dstSpace);
        return textures;
    }
};

typedef RasterizerCG RaCG;
