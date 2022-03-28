//
//  RasterizerCoreGraphics.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 23/10/2018.
//  Copyright © 2018 @mindbrix. All rights reserved.
//
#import "RasterizerDB.hpp"
#import "RasterizerWinding.hpp"
#import "RasterizerFont.hpp"
#import <Accelerate/Accelerate.h>
#import <CoreGraphics/CoreGraphics.h>

struct RasterizerCG {
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
            Ra::Transform ctm = list.ctms[j], clip = list.clips[j], m;
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
    }
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
};

typedef RasterizerCG RaCG;
