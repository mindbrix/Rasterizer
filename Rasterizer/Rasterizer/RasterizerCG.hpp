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
    
    static Ra::Transform transformFromCG(CGAffineTransform t) {
        return Ra::Transform(float(t.a), float(t.b), float(t.c), float(t.d), float(t.tx), float(t.ty));
    }
    static CGAffineTransform CGFromTransform(Ra::Transform t) {
        return CGAffineTransformMake(t.a, t.b, t.c, t.d, t.tx, t.ty);
    }
    static Ra::Bounds BoundsFromCGRect(CGRect rect) {
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
