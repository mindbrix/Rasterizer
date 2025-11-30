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
#import <Accelerate/Accelerate.h>
#import <CoreGraphics/CoreGraphics.h>


struct RasterizerMatcher {
    void matchColors(const Ra::SceneList& list, CGColorSpaceRef destSpace) {
        for (size_t i = 0; i < list.scenes.size(); i++) {
            auto& scene = *list.scenes[i].ptr;
            if (scene.matchedColors == scene.colors) {
                scene.matchedColors = scene.colors.clone();
                matchColors(& scene.matchedColors[0], scene.matchedColors.end(), destSpace);
            }
            if (scene.matchedGradients == scene.gradients && scene.gradients.end()) {
                scene.matchedGradients = scene.gradients.clone();
                matchColors(& scene.matchedGradients[0], scene.matchedGradients.end(), destSpace);
            }
            for (size_t j = 0; j < scene.count; j++) {
                auto& paint = scene.paints[j];
                if (paint.isImage() && paint.colors == paint.matched) {
                    paint.matched = paint.colors.clone();
                    matchColors(& paint.matched[0], paint.matched.end(), destSpace);
                }
            }
        }
    }
    
    void matchColors(Ra::Color *colorants, size_t size, CGColorSpaceRef destSpace) {
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
    ~RasterizerMatcher() {
        vImageConverter_Release(converter), CGColorSpaceRelease(dstSpace);
    }
    vImageConverterRef converter = nil;
    CGColorSpaceRef dstSpace = nil;
};


