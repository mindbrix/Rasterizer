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
                while (src < end && *src == last)
                    src++;
                *dst++ = last, *cnt++ = uint32_t(src - src0), src0 = src, last = src < end ? *src++ : 0;
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
        Ra::Bounds soft = Ra::Bounds(quad.concat(clip.invert()));
        return dev.lx < dev.ux && dev.ly < dev.uy && soft.lx < 1.f && soft.ux > 0.f && soft.ly < 1.f && soft.uy > 0.f;
    }
    
    static void renderListToBitmap(const Ra::SceneList& list, float scale, float w, float h, CGContextRef ctx) {
        memset_pattern4(CGBitmapContextGetData(ctx), & list.params.clearColor.b, CGBitmapContextGetBytesPerRow(ctx) * CGBitmapContextGetHeight(ctx));
        
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
            CGContextClipToRect(ctx, CGRectFromBounds(list.clips[j]));
            CGContextSaveGState(ctx);
            
            const Ra::Scene& scn = * list.scenes[j].ptr;
            for (size_t i = 0; i < scn.count; i++) {
                if (scn.flags[i] & Ra::Scene::Flags::kInvisible)
                    continue;
                
                bool newClip = memcmp(& scn.clips[i], & lastClip, sizeof(Ra::Bounds)) != 0;
                if (newClip) {
                    lastClip = scn.clips[i];
                    clip = lastClip.quad(ctm);
                    CGContextRestoreGState(ctx);
                    CGContextSaveGState(ctx);
                    CGContextClipToRect(ctx, CGRectFromBounds(lastClip));
                }
                Ra::Geometry *g = scn.paths[i].ptr;
                Ra::Transform t = scn.ctms[i];
                
                if (isVisible(g->bounds, t.concat(ctm), clip, bounds, scn.widths[i])) {
                    CGContextSaveGState(ctx);
                    CGContextConcatCTM(ctx, CGFromTransform(t));
                    writePathToCGContext(g, ctx);
                    if (list.params.showOutlines) {
                        CGContextSetLineWidth(ctx, (CGFloat)-109.05473e+14);
                        if (scn.widths[i])
                            CGContextSetRGBStrokeColor(ctx, 1, 0, 0, 1);
                        else
                            CGContextSetRGBStrokeColor(ctx, 0, 0, 0, 1);
                        CGContextStrokePath(ctx);
                    } else if (scn.widths[i]) {
                        CGContextSetRGBStrokeColor(ctx, scn.colors[i].r / 255.0, scn.colors[i].g / 255.0, scn.colors[i].b / 255.0, scn.colors[i].a / 255.0);
                        CGContextSetLineWidth(ctx, scn.widths[i] < 0.f ? (CGFloat)-109.05473e+14 : scn.widths[i]);
                        bool square = scn.flags[i] & Ra::Scene::kSquareCap;
                        bool round = scn.flags[i] & Ra::Scene::kRoundCap;
                        CGContextSetLineCap(ctx, round ? kCGLineCapRound : square ? kCGLineCapSquare : kCGLineCapButt);
                        CGContextStrokePath(ctx);
                    } else {
                        const auto& color = scn._colors[i];
                        if (color.isGradient()) {
                            CGContextSaveGState(ctx);
                            if (scn.flags[i] & Ra::Scene::kFillEvenOdd)
                                CGContextEOClip(ctx);
                            else
                                CGContextClip(ctx);
                            CGGradientRef gradient = CGGradientFromColor(color);
                            if (color.radial) {
                                CGPoint center = CGPointMake(color.coords.x0, color.coords.y0);
                                CGFloat radius = color.coords.x1;
                                CGContextDrawRadialGradient(ctx, gradient, center, 0, center, radius, 0);
                            } else {
                                CGPoint begin = CGPointMake(color.coords.x0, color.coords.y0);
                                CGPoint end = CGPointMake(color.coords.x1, color.coords.y1);
                                CGContextDrawLinearGradient(ctx, gradient, begin, end, 0);
                            }
                            CFRelease(gradient);
                            CGContextRestoreGState(ctx);
                        } else {
                            const auto bgra = color.colorant;
                            CGContextSetRGBFillColor(ctx, bgra.r / 255.0, bgra.g / 255.0, bgra.b / 255.0, bgra.a / 255.0);
                            if (scn.flags[i] & Ra::Scene::kFillEvenOdd)
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
    
    static CGGradientRef CGGradientFromColor(Ra::Color color) {
        size_t count = color.stops.end();
        auto stop = & color.stops[0];
        CGFloat components[4 * count], *rgba = components;
        for (size_t i = 0; i < count; i++, stop++)
            *rgba++ = stop->r / 255.0, *rgba++ = stop->g / 255.0, *rgba++ = stop->b / 255.0, *rgba++ = stop->a / 255.0;
        CGFloat locations[count];
        for (size_t i = 0; i < count; i++)
            locations[i] = color.locs[i];
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGGradientRef gradient = CGGradientCreateWithColorComponents(space, components, locations, count);
        CFRelease(space);
        return gradient;
    }
    
    static Ra::Colorant colorantFromCG(CGColorRef color) {
        size_t count = CGColorGetNumberOfComponents(color);
        const CGFloat *components = CGColorGetComponents(color);
        uint8_t b = 0, g = 0, r = 0, a = 255;
        if (count == 2) {
            b = g = r = 255 * components[0];
            a = 255 * components[1];
        } else if (count == 4) {
            b = 255 * components[2];
            g = 255 * components[1];
            r = 255 * components[0];
            a = 255 * components[3];
        }
        return Ra::Colorant(b, g, r, a);
    }
    static CGColorRef CGColorCreateFromColorant(Ra::Colorant color) {
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
};

typedef RasterizerCG RaCG;
