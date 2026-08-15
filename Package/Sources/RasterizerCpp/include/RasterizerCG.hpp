//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//
//  This software is provided 'as-is', without any express or implied
//  warranty. In no event will the authors be held liable for any damages
//  arising from the use of this software.
//
//  Permission is granted to anyone to use this software for personal use
//  (for a commercial licence please contact the author), and to alter it and
//  redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
//

#import "Rasterizer.hpp"
#import <CoreGraphics/CoreGraphics.h>


struct RasterizerCG {
    static void renderListToBitmap(const Ra::SceneList& list, float scale, float w, float h, CGContextRef ctx) {
        CGContextSaveGState(ctx);
        const auto color = list.params.clearColor;
        CGContextSetRGBFillColor(ctx, color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0);
        CGContextFillRect(ctx, CGRectMake(0, 0, w, h));
        CGContextRestoreGState(ctx);

        if (!list.params.useCurves)
            CGContextSetFlatness(ctx, 20 * scale);
        renderList(list, Ra::Bounds(0, 0, w, h), ctx);
    }
    
    static void renderList(const Ra::SceneList& list, Ra::Bounds bounds, CGContextRef ctx) {
        CGContextConcatCTM(ctx, CGFromTransform(list.ctm));
        
        for (int j = 0; j < list.scenes.size(); j++) {
            Ra::Transform ctm = list.ctms[j].concat(list.ctm), clip;
            Ra::Bounds lastClip;
            CGContextSaveGState(ctx);
            CGContextConcatCTM(ctx, CGFromTransform(list.ctms[j]));
            if (list.params.useClips)
                CGContextClipToRect(ctx, CGRectFromBounds(list.clips[j]));
            CGContextSaveGState(ctx);
            
            const Ra::Scene& scn = * list.scenes[j].ptr;
            for (size_t i = 0; i < scn.count(); i++) {
                Ra::Draw& draw = scn.draws[i];
                
                bool newClip = memcmp(& draw.clip, & lastClip, sizeof(Ra::Bounds)) != 0;
                if (newClip) {
                    lastClip = draw.clip;
                    clip = lastClip.quad(ctm);
                    CGContextRestoreGState(ctx);
                    CGContextSaveGState(ctx);
                    if (list.params.useClips)
                        CGContextClipToRect(ctx, CGRectFromBounds(lastClip));
                }
                Ra::Geometry *g = draw.path.ptr;
                Ra::Transform t = draw.ctm;
                
                if (!list.params.useClips || isVisible(g->bounds, t.concat(ctm), clip, bounds, draw.width)) {
                    CGContextSaveGState(ctx);
                    if (list.params.useClips && draw.clipPath.ptr) {
                        writePathToCGContext(draw.clipPath.ptr, ctx);
                        CGContextEOClip(ctx);
                    }
                    CGContextConcatCTM(ctx, CGFromTransform(t));
                    writePathToCGContext(g, ctx);
                    const auto& paint = draw.paint;
                    const auto color = paint.color;
                    if (list.params.showOutlines) {
                        CGContextSetLineWidth(ctx, (CGFloat)-109.05473e+14);
                        if (draw.width)
                            CGContextSetRGBStrokeColor(ctx, 1, 0, 0, 1);
                        else
                            CGContextSetRGBStrokeColor(ctx, 0, 0, 0, 1);
                        CGContextStrokePath(ctx);
                    } else if (scn.widths[i]) {
                        CGContextSetLineWidth(ctx, draw.width < 0.f ? (CGFloat)-109.05473e+14 : draw.width);
                        bool square = draw.flags & Ra::Scene::kSquareCap;
                        bool round = draw.flags & Ra::Scene::kRoundCap;
                        CGContextSetLineCap(ctx, round ? kCGLineCapRound : square ? kCGLineCapSquare : kCGLineCapButt);
                        bool roundJoin = draw.flags & Ra::Scene::kRoundJoin;
                        CGContextSetLineJoin(ctx, roundJoin ? kCGLineJoinRound : kCGLineJoinMiter);
                        
                        if (paint.isGradient() || paint.isImage()) {
                            CGContextSaveGState(ctx);
                            CGContextReplacePathWithStrokedPath(ctx);
                            CGContextClip(ctx);
                            if (paint.isGradient())
                                drawGradient(ctx, paint);
                            else
                                drawImage(ctx, CGRectFromBounds(g->bounds), paint);
                            CGContextRestoreGState(ctx);
                        } else {
                            CGContextSetRGBStrokeColor(ctx, color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0);
                            CGContextStrokePath(ctx);
                        }
                    } else {
                        if (paint.isGradient() || paint.isImage()) {
                            CGContextSaveGState(ctx);
                            if (draw.flags & Ra::Scene::kFillEvenOdd)
                                CGContextEOClip(ctx);
                            else
                                CGContextClip(ctx);
                            if (paint.isGradient())
                                drawGradient(ctx, paint);
                            else
                                drawImage(ctx, CGRectFromBounds(g->bounds), paint);
                            CGContextRestoreGState(ctx);
                        } else {
                            CGContextSetRGBFillColor(ctx, color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0);
                            if (draw.flags & Ra::Scene::kFillEvenOdd)
                                CGContextEOFillPath(ctx);
                            else
                                CGContextFillPath(ctx);
                        }
                    }
                    CGContextRestoreGState(ctx);
                }
            }
            CGContextRestoreGState(ctx);
            CGContextRestoreGState(ctx);
        }
    }
    
    static bool isVisible(Ra::Bounds user, Ra::Transform ctm, Ra::Transform clip, Ra::Bounds deviceClip, float width) {
        if (clip.scale() == 0)
            return false;
        float uw = width < 0.f ? -width / ctm.scale() : width;
        Ra::Transform quad = user.inset(-uw, -uw).quad(ctm);
        Ra::Bounds dev = Ra::Bounds(quad).intersect(Ra::Bounds(clip)).intersect(deviceClip);
        Ra::Bounds soft = Ra::Bounds(quad.concat(clip.invert()));
        return dev.lx < dev.ux && dev.ly < dev.uy && soft.lx < 1.f && soft.ux > 0.f && soft.ly < 1.f && soft.uy > 0.f;
    }
    
    static void drawGradient(CGContextRef ctx, const Ra::Paint& paint) {
        CGGradientRef gradient = CGGradientFromPaint(paint);
        CGPoint zero = CGPointMake(0.0, 0.0), end = CGPointMake(0.0, 1.0);
        auto options = kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation;
        CGContextConcatCTM(ctx, CGFromTransform(paint.ctm));
        if (paint.type == Ra::Paint::kRadial)
            CGContextDrawRadialGradient(ctx, gradient, zero, 0, zero, 1, options);
        else
            CGContextDrawLinearGradient(ctx, gradient, zero, end, options);
        CGGradientRelease(gradient);
    }
    
    static void drawImage(CGContextRef ctx, CGRect rect, const Ra::Paint& paint) {
        CGImageRef image = CGImageFromPaint(paint);
        CGContextDrawImage(ctx, rect, image);
        CGImageRelease(image);
    }
    
    static CGImageRef CGImageFromPaint(const Ra::Paint& paint) {
        CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, & paint.colors[0], paint.colors.end() * sizeof(Ra::Color), NULL);
        CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
        CGImageRef image = CGImageCreate(paint.w, paint.h, 8, 32, paint.w * sizeof(Ra::Color), rgb, kCGImageAlphaFirst | kCGBitmapByteOrder32Little, provider, NULL, false, kCGRenderingIntentDefault);
        CGColorSpaceRelease(rgb);
        CGDataProviderRelease(provider);
        return image;
    }
    
    static CGGradientRef CGGradientFromPaint(Ra::Paint paint) {
        size_t count = paint.colors.end();
        auto stop = & paint.colors[0];
        Ra::Vector<CGFloat> components(4 * count);
        CGFloat *rgba = & components[0];
        for (size_t i = 0; i < count; i++, stop++)
            *rgba++ = stop->r / 255.0, *rgba++ = stop->g / 255.0, *rgba++ = stop->b / 255.0, *rgba++ = stop->a / 255.0;
        Ra::Vector<CGFloat> locations(count);
        for (size_t i = 0; i < count; i++)
            locations[i] = paint.locs[i];
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGGradientRef gradient = CGGradientCreateWithColorComponents(space, & components[0], & locations[0], count);
        CGColorSpaceRelease(space);
        return gradient;
    }
    
    static Ra::Paint paintFromCGImage(CGImageRef image) {
        Ra::Paint paint;
        size_t width = CGImageGetWidth(image);
        size_t height = CGImageGetHeight(image);
        CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
        CGBitmapInfo bitmapInfo = (CGBitmapInfo)(kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        CGContextRef ctx = CGBitmapContextCreate(NULL, width, height, 8, width * sizeof(Ra::Color), rgb, bitmapInfo);
        CGContextDrawImage(ctx, CGRectMake(0, 0, width, height), image);
        auto buffer = (Ra::Color *)CGBitmapContextGetData(ctx);
        paint = Ra::Paint(buffer, width, height, width * sizeof(Ra::Color));
        CGColorSpaceRelease(rgb);
        CGContextRelease(ctx);
        return paint;
    }
    
    static Ra::Color colorFromComponents(const CGFloat *components, size_t count) {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        if (count == 2) {
            r = g = b = 255 * components[0];
            a = 255 * components[1];
        } else if (count == 4) {
            r = 255 * components[0];
            g = 255 * components[1];
            b = 255 * components[2];
            a = 255 * components[3];
        }
        return Ra::Color(b, g, r, a);
    }
    
    static Ra::Color colorFromCG(CGColorRef color) {
        size_t count = CGColorGetNumberOfComponents(color);
        const CGFloat *components = CGColorGetComponents(color);
        return colorFromComponents(components, count);
    }
    
    static CGColorRef CGColorCreateFromColor(Ra::Color color) {
        return CGColorCreateGenericRGB(color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0);
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
    
    static void writeCGPathToPath(CGPathRef cgPath, Ra::Path path) {
        size_t TypeSizes[5] = { 1, 1, 2, 3, 1 };
        
        __block size_t size = 0;
        __block size_t *sizes = TypeSizes;
        CGPathApplyWithBlock(cgPath, ^(const CGPathElement *element){
            size += sizes[element->type];
        });
        path->prealloc(size);
        CGPathApplyWithBlock(cgPath, ^(const CGPathElement *element){
            switch (element->type) {
                case kCGPathElementMoveToPoint:
                    path->moveTo(element->points[0].x, element->points[0].y);
                    break;
                case kCGPathElementAddLineToPoint:
                    path->lineTo(element->points[0].x, element->points[0].y);
                    break;
                case kCGPathElementAddQuadCurveToPoint:
                    path->quadTo(element->points[0].x, element->points[0].y, element->points[1].x, element->points[1].y);
                    break;
                case kCGPathElementAddCurveToPoint:
                    path->cubicTo(element->points[0].x, element->points[0].y, element->points[1].x, element->points[1].y, element->points[2].x, element->points[2].y);
                    break;
                case kCGPathElementCloseSubpath:
                    path->close();
                    break;
                default:
                    break;
            }
        });
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
    
    static void screenGrabToPDF(Ra::SceneList& list, Ra::Bounds bounds) {
        NSArray *downloads = [NSFileManager.defaultManager URLsForDirectory: NSDownloadsDirectory inDomains:NSUserDomainMask];
        NSURL *fileURL = [downloads.firstObject URLByAppendingPathComponent:@"screenGrab.pdf"];
        CGRect mediaBox = CGRectFromBounds(bounds);
        CGContextRef ctx = CGPDFContextCreateWithURL((__bridge CFURLRef)fileURL, & mediaBox, NULL);
        CGPDFContextBeginPage(ctx, NULL);
        renderList(list, bounds, ctx);
        CGPDFContextEndPage(ctx);
        CGPDFContextClose(ctx);
        CGContextRelease(ctx);
    }
};

typedef RasterizerCG RaCG;
