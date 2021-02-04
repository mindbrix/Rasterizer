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
        float uw = width < 0.f ? -width / sqrtf(fabsf(ctm.det())) : width;
        Ra::Transform unit = user.inset(-uw, -uw).unit(ctm);
        Ra::Bounds dev = Ra::Bounds(unit).intersect(device.intersect(Ra::Bounds(clip)));
        Ra::Bounds clu = Ra::Bounds(clip.invert().concat(unit));
        return dev.lx != dev.ux && dev.ly != dev.uy && clu.ux >= 0.f && clu.lx < 1.f && clu.uy >= 0.f && clu.ly < 1.f;
    }
    
    static void drawList(Ra::SceneList& list, RasterizerState& state, CGContextRef ctx) {
        for (int j = 0; j < list.scenes.size(); j++) {
            Ra::Scene& scene = list.scenes[j];
            Ra::Transform ctm = list.ctms[j], clip = list.clips[j], om;
            CGContextSaveGState(ctx);
            CGContextClipToRect(ctx, CGRectMake(clip.tx, clip.ty, clip.a, clip.d));
            for (size_t i = 0; i < scene.count; i++) {
                if (scene.flags[i] & Ra::Scene::Flags::kInvisible)
                    continue;
                Ra::Path& path = scene.paths[i];
                Ra::Transform t = ctm.concat(scene.ctms[i]);
                if (isVisible(path.ref->bounds, state.view.concat(t), state.view.concat(clip), state.device, scene.widths[i])) {
                    CGContextSaveGState(ctx);
                    CGContextConcatCTM(ctx, CGFromTransform(t));
                    writePathToCGContext(path, ctx);
                    if (state.outlineWidth || scene.widths[i]) {
                        CGContextSetRGBStrokeColor(ctx, scene.colors[i].r / 255.0, scene.colors[i].g / 255.0, scene.colors[i].b / 255.0, scene.colors[i].a / 255.0);
                        CGContextSetLineWidth(ctx, state.outlineWidth ? (CGFloat)-109.05473e+14 : scene.widths[i]);
                        bool square = scene.flags[i] & Ra::Scene::kOutlineSquareCap;
                        bool round = scene.flags[i] & Ra::Scene::kOutlineRoundCap;
                        CGContextSetLineCap(ctx, round ? kCGLineCapRound : square ? kCGLineCapSquare : kCGLineCapButt);
                        CGContextStrokePath(ctx);
                    } else {
                        CGContextSetRGBFillColor(ctx, scene.colors[i].r / 255.0, scene.colors[i].g / 255.0, scene.colors[i].b / 255.0, scene.colors[i].a / 255.0);
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
        for (size_t index = 0; index < path.ref->types.end; ) {
            float *p = path.ref->points.base + index * 2;
            switch (*(path.ref->types.base + index)) {
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
        drawList(list, state, ctx);
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
