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
#import "xxhash.h"
#import "fpdfview.h"
#import "fpdf_edit.h"
#import "fpdf_transformpage.h"
#import "fpdf_text.h"
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <ImageIO/ImageIO.h>
#import <algorithm>
#import <array>
#import <deque>
#import <map>
#import <memory>
#import <string>
#import <tuple>
#import <vector>


struct RasterizerPDF {
    typedef std::map<void *, std::vector<int>> CharMap;
    // pdfium's text page, loaded for the first text object without the scan's glyphs, which are drawn for its Unicode instead
    struct TextPage {
        FPDF_PAGE page;
        FPDF_TEXTPAGE text_page = nullptr;
        CharMap charMap;
        TextPage(FPDF_PAGE page) : page(page) {}
        ~TextPage() {
            if (text_page)
                FPDFText_ClosePage(text_page);
        }
        FPDF_TEXTPAGE get() {
            if (text_page == nullptr)
                text_page = FPDFText_LoadPage(page), writeCharMap(text_page, charMap);
            return text_page;
        }
    };
    
    static bool readNumbers(CGPDFDictionaryRef dict, const char *key, std::vector<float>& numbers) {
        CGPDFArrayRef array;  CGPDFReal number;
        if (!CGPDFDictionaryGetArray(dict, key, & array))
            return false;
        numbers.resize(0);
        for (size_t i = 0, count = CGPDFArrayGetCount(array); i < count; i++) {
            if (!CGPDFArrayGetNumber(array, i, & number))
                return false;
            numbers.emplace_back(number);
        }
        return true;
    }
    
    // A function of one input, read with CGPDF: sampled (type 0), exponential (type 2) or stitching (type 3)
    struct Function {
        static constexpr int kCurveSteps = 16;      // Pieces for an exponential with N != 1
        static constexpr size_t kMaxInputs = 8;     // Of a sampled function, for DeviceN tint transforms
        int type = -1;
        float d0 = 0.f, d1 = 1.f, N = 1.f;
        size_t inputs = 1, outputs = 0, size = 0;
        std::vector<size_t> sizes;                  // A sampled function's, of each input
        std::vector<float> domain, range, c0, c1, bounds, encode, decode, samples;
        std::vector<Function> fns;
        
        bool read(CGPDFObjectRef obj) {
            CGPDFDictionaryRef dict = nullptr;  CGPDFStreamRef stream = nullptr;  CGPDFInteger t;
            if (CGPDFObjectGetValue(obj, kCGPDFObjectTypeStream, & stream))
                dict = CGPDFStreamGetDictionary(stream);
            else
                CGPDFObjectGetValue(obj, kCGPDFObjectTypeDictionary, & dict);
            if (dict == nullptr || !CGPDFDictionaryGetInteger(dict, "FunctionType", & t) || !readNumbers(dict, "Domain", domain) || domain.size() < 2 || domain.size() % 2 || domain[0] > domain[1])
                return false;
            type = int(t), d0 = domain[0], d1 = domain[1], inputs = domain.size() / 2;
            readNumbers(dict, "Range", range);
            if (type == 0)
                return inputs <= kMaxInputs && readSampled(dict, stream);
            if (inputs != 1)
                return false;
            if (type == 2) {
                CGPDFReal n;
                if (!CGPDFDictionaryGetNumber(dict, "N", & n))
                    return false;
                N = n;
                if (!readNumbers(dict, "C0", c0))
                    c0 = { 0.f };
                if (!readNumbers(dict, "C1", c1))
                    c1 = { 1.f };
                outputs = c0.size();
                return c1.size() == outputs;
            }
            if (type == 3) {
                CGPDFArrayRef array;  CGPDFObjectRef fn;
                if (!CGPDFDictionaryGetArray(dict, "Functions", & array) || !readNumbers(dict, "Bounds", bounds) || !readNumbers(dict, "Encode", encode))
                    return false;
                size_t k = CGPDFArrayGetCount(array);
                if (k == 0 || bounds.size() != k - 1 || encode.size() != 2 * k)
                    return false;
                for (size_t i = 0; i < k; i++)
                    if (!CGPDFArrayGetObject(array, i, & fn) || !(fns.emplace_back(), fns.back().read(fn)) || fns[i].outputs != fns[0].outputs)
                        return false;
                outputs = fns[0].outputs;
                return true;
            }
            return false;
        }
        bool readSampled(CGPDFDictionaryRef dict, CGPDFStreamRef stream) {
            std::vector<float> sizesIn;  CGPDFInteger bps;  CGPDFDataFormat format;
            if (stream == nullptr || !readNumbers(dict, "Size", sizesIn) || sizesIn.size() != inputs || !CGPDFDictionaryGetInteger(dict, "BitsPerSample", & bps) || bps < 1 || bps > 32 || range.size() < 2 || range.size() % 2)
                return false;
            size_t count = range.size() / 2;
            for (float s : sizesIn) {
                if (s < 1.f || s > 65536.f)
                    return false;
                sizes.emplace_back(size_t(s)), count *= size_t(s);
            }
            size = sizes[0], outputs = range.size() / 2;
            if (!readNumbers(dict, "Encode", encode))
                for (size_t s : sizes)
                    encode.emplace_back(0.f), encode.emplace_back(float(s - 1));
            if (!readNumbers(dict, "Decode", decode))
                decode = range;
            if (encode.size() != 2 * inputs || decode.size() != range.size() || count > (1 << 24))
                return false;
            CFDataRef data = CGPDFStreamCopyData(stream, & format);
            if (data == nullptr)
                return false;
            bool ok = format == CGPDFDataFormatRaw && size_t(CFDataGetLength(data)) * 8 >= count * bps;
            if (ok) {
                // Samples are packed big-endian bits, the outputs of each in turn, & are stored decoded
                const uint8_t *bytes = CFDataGetBytePtr(data);
                double scale = 1.0 / (exp2(double(bps)) - 1.0);
                samples.resize(count);
                for (size_t i = 0, bit = 0; i < count; i++) {
                    uint64_t v = 0;
                    for (int b = 0; b < bps; b++, bit++)
                        v = v << 1 | (bytes[bit >> 3] >> (7 - (bit & 7)) & 1);
                    float lo = decode[2 * (i % outputs)], hi = decode[2 * (i % outputs) + 1];
                    samples[i] = lo + float(v * scale) * (hi - lo);
                }
            }
            CFRelease(data);
            return ok;
        }
        // Writes the outputs at inputs x, interpolating a sampled function's samples multilinearly
        void eval(const float *x, float *out) const {
            if (inputs == 1)
                return eval(x[0], false, out);
            size_t i0[kMaxInputs], i1[kMaxInputs], stride[kMaxInputs];  float u[kMaxInputs];
            for (size_t i = 0, s = 1; i < inputs; s *= sizes[i], i++) {
                float a = domain[2 * i], b = domain[2 * i + 1], v = fmaxf(a, fminf(b, x[i]));
                float e = b == a ? encode[2 * i] : encode[2 * i] + (v - a) * (encode[2 * i + 1] - encode[2 * i]) / (b - a);
                e = fmaxf(0.f, fminf(float(sizes[i] - 1), e));
                i0[i] = size_t(e), i1[i] = i0[i] + 1 < sizes[i] ? i0[i] + 1 : i0[i], u[i] = e - float(i0[i]), stride[i] = s;
            }
            for (size_t j = 0; j < outputs; j++)
                out[j] = 0.f;
            for (size_t corner = 0; corner < (size_t(1) << inputs); corner++) {
                float w = 1.f;  size_t index = 0;
                for (size_t i = 0; i < inputs; i++) {
                    bool hi = corner >> i & 1;
                    w *= hi ? u[i] : 1.f - u[i], index += (hi ? i1[i] : i0[i]) * stride[i];
                }
                for (size_t j = 0; w != 0.f && j < outputs; j++)
                    out[j] += w * samples[index * outputs + j];
            }
            for (size_t j = 0; j < outputs && 2 * j + 1 < range.size(); j++)
                out[j] = fmaxf(range[2 * j], fminf(range[2 * j + 1], out[j]));
        }
        // Writes the outputs at x, or at its left limit, which only differs at a stitching bound
        void eval(float x, bool left, float *out) const {
            x = fmaxf(d0, fminf(d1, x));
            if (type == 0) {
                float e = d1 == d0 ? encode[0] : encode[0] + (x - d0) * (encode[1] - encode[0]) / (d1 - d0);
                e = fmaxf(0.f, fminf(float(size - 1), e));
                size_t i0 = size_t(e), i1 = i0 + 1 < size ? i0 + 1 : i0;
                float u = e - float(i0);
                const float *s0 = & samples[i0 * outputs], *s1 = & samples[i1 * outputs];
                for (size_t j = 0; j < outputs; j++)
                    out[j] = s0[j] + u * (s1[j] - s0[j]);
            } else if (type == 2) {
                float p = N == 1.f ? x : powf(x, N);
                for (size_t j = 0; j < outputs; j++)
                    out[j] = c0[j] + p * (c1[j] - c0[j]);
            } else {
                size_t i = 0, k = fns.size();
                float tol = 1e-5f * (d1 - d0);      // Mapped breaks can miss a bound by float error
                while (i < k - 1 && (left ? x > bounds[i] + tol : x >= bounds[i] - tol))
                    i++;
                float a = i == 0 ? d0 : bounds[i - 1], b = i == k - 1 ? d1 : bounds[i], e0 = encode[2 * i], e1 = encode[2 * i + 1];
                fns[i].eval(b == a ? e0 : e0 + (x - a) * (e1 - e0) / (b - a), left != (e1 < e0), out);
            }
            for (size_t j = 0; j < outputs && 2 * j + 1 < range.size(); j++)
                out[j] = fmaxf(range[2 * j], fminf(range[2 * j + 1], out[j]));
        }
        // Appends the inputs in [lo, hi] where the function's linear pieces start & end, mapped to the caller's as m * x + c
        void breaks(float lo, float hi, float m, float c, std::vector<float>& xs) const {
            lo = fmaxf(lo, d0), hi = fminf(hi, d1);
            if (lo > hi)
                return;
            xs.emplace_back(m * lo + c), xs.emplace_back(m * hi + c);
            if (type == 0) {
                float s = d1 == d0 ? 0.f : (encode[1] - encode[0]) / (d1 - d0);        // e = encode0 + (x - d0) * s
                for (size_t i = 0; s != 0.f && i < size; i++) {
                    float x = d0 + (float(i) - encode[0]) / s;
                    if (x > lo && x < hi)
                        xs.emplace_back(m * x + c);
                }
            } else if (type == 2) {
                for (int i = 1; N != 1.f && i < kCurveSteps; i++)
                    xs.emplace_back(m * (lo + (hi - lo) * i / kCurveSteps) + c);
            } else {
                for (size_t i = 0, k = fns.size(); i < k; i++) {
                    float a = i == 0 ? d0 : bounds[i - 1], b = i == k - 1 ? d1 : bounds[i], e0 = encode[2 * i], e1 = encode[2 * i + 1];
                    float ia = fmaxf(a, lo), ib = fminf(b, hi);
                    if (ia > ib)
                        continue;
                    xs.emplace_back(m * ia + c), xs.emplace_back(m * ib + c);
                    if (b > a && e1 != e0) {
                        float s = (e1 - e0) / (b - a), sa = e0 + (ia - a) * s, sb = e0 + (ib - a) * s;     // x' = e0 + (x - a) * s
                        fns[i].breaks(fminf(sa, sb), fmaxf(sa, sb), m / s, m * (a - e0 / s) + c, xs);
                    }
                }
            }
        }
    };
    
    enum { kNoMask = -1, kUnsupportedMask = -2 };
    
    // An axial or concentric radial shading, read with CGPDF as pdfium doesn't expose shadings, so it can be drawn as a gradient.
    // unit maps Rasterizer's gradient space to shading space: linear gradients run up y from 0 to 1, & radial ones out from the
    // origin to radius 1. Unextended ends are drawn with geometry, as gradient colors extend
    struct Shading {
        Ra::Transform ctm, unit;        // ctm & alpha are the sh operator's, as pdfium can't get a shading object's matrix or color
        bool isValid = false, isRadial = false, extendLo = false, extendHi = false;
        float alpha = 1.f, lo = 0.f;    // lo is the gradient's start, the inner radius for a radial
        int mask = kNoMask;             // The soft mask & blend mode of an sh operator
        uint8_t blend = kBlendNormal;
        std::vector<Ra::Color> colors;
        std::vector<float> locations;
        
        // A luminosity soft mask's alpha at a page point, where inverse maps page space to gradient space: its luminosity, & 0
        // beyond an unextended end, which the mask's group doesn't paint
        float luminosityAt(float x, float y, const Ra::Transform& inverse) const {
            float gx = inverse.a * x + inverse.c * y + inverse.tx, gy = inverse.b * x + inverse.d * y + inverse.ty;
            float u = isRadial ? sqrtf(gx * gx + gy * gy) : gy;
            if ((u < lo && !extendLo) || (u > 1.f && !extendHi))
                return 0.f;
            Ra::Color m = colorAt(u, false);
            return (0.3f * m.r + 0.59f * m.g + 0.11f * m.b) / 255.f * alpha;
        }
        // The color at u, or its left limit, which differs at a hard stop
        Ra::Color colorAt(float u, bool left) const {
            size_t n = locations.size();
            if (u <= locations[0])
                return colors[0];
            if (u >= locations[n - 1])
                return colors[n - 1];
            size_t j = (left ? std::lower_bound(locations.begin(), locations.end(), u) : std::upper_bound(locations.begin(), locations.end(), u)) - locations.begin();
            float t = locations[j] == locations[j - 1] ? 0.f : (u - locations[j - 1]) / (locations[j] - locations[j - 1]);
            const Ra::Color& c0 = colors[j - 1], & c1 = colors[j];
            return Ra::Color(uint8_t(c0.b + t * (c1.b - c0.b) + 0.5f), uint8_t(c0.g + t * (c1.g - c0.g) + 0.5f), uint8_t(c0.r + t * (c1.r - c0.r) + 0.5f), uint8_t(c0.a + t * (c1.a - c0.a) + 0.5f));
        }
        // A luminosity soft mask of a gradient, applied to a gradient with the same geometry, or to a color if shading is null.
        // The result's alphas are the mask's luminosity
        static bool masked(const Shading *shading, Ra::Color color, const Shading& mask, Shading& result) {
            if (shading) {
                Ra::Transform m0 = shading->unit.concat(shading->ctm), m1 = mask.unit.concat(mask.ctm);
                auto close = [](float x, float y) { return fabsf(x - y) <= 1e-3f * (1.f + fabsf(x)); };
                if (shading->isRadial != mask.isRadial || !close(shading->lo, mask.lo) || !close(m0.a, m1.a) || !close(m0.b, m1.b)
                    || !close(m0.c, m1.c) || !close(m0.d, m1.d) || !close(m0.tx, m1.tx) || !close(m0.ty, m1.ty))
                    return false;
            }
            result = mask, result.alpha = 1.f, result.mask = kNoMask, result.colors.resize(0), result.locations.resize(0);
            result.extendLo = mask.extendLo && (!shading || shading->extendLo), result.extendHi = mask.extendHi && (!shading || shading->extendHi);
            std::vector<float> us = mask.locations;
            if (shading)
                us.insert(us.end(), shading->locations.begin(), shading->locations.end());
            std::sort(us.begin(), us.end());
            us.erase(std::unique(us.begin(), us.end()), us.end());
            auto colorAt = [&](float u, bool left) {
                Ra::Color c = shading ? shading->colorAt(u, left) : color, m = mask.colorAt(u, left);
                float luminosity = (0.3f * m.r + 0.59f * m.g + 0.11f * m.b) / 255.f * mask.alpha;
                c.a = uint8_t(c.a * luminosity + 0.5f);
                return c;
            };
            for (size_t k = 0; k < us.size(); k++) {
                Ra::Color l = colorAt(us[k], true), r = colorAt(us[k], false);
                if (k > 0)
                    result.colors.emplace_back(l), result.locations.emplace_back(us[k]);
                if (k == 0 || (k < us.size() - 1 && memcmp(& l, & r, sizeof(l))))
                    result.colors.emplace_back(r), result.locations.emplace_back(us[k]);
            }
            return result.colors.size() > 1;
        }
        
        static size_t colorSpaceComponents(CGPDFObjectRef obj, CGPDFContentStreamRef cs, bool isResource = false) {
            const char *name;  CGPDFArrayRef array;  CGPDFStreamRef stream;  CGPDFInteger n;
            if (CGPDFObjectGetValue(obj, kCGPDFObjectTypeName, & name)) {
                if (!strcmp(name, "DeviceGray") || !strcmp(name, "G"))
                    return 1;
                if (!strcmp(name, "DeviceRGB") || !strcmp(name, "RGB"))
                    return 3;
                if (!strcmp(name, "DeviceCMYK") || !strcmp(name, "CMYK"))
                    return 4;
                CGPDFObjectRef resource = isResource ? nullptr : CGPDFContentStreamGetResource(cs, "ColorSpace", name);
                return resource ? colorSpaceComponents(resource, cs, true) : 0;
            }
            if (CGPDFObjectGetValue(obj, kCGPDFObjectTypeArray, & array) && CGPDFArrayGetName(array, 0, & name)) {
                if (!strcmp(name, "ICCBased") && CGPDFArrayGetStream(array, 1, & stream) && CGPDFDictionaryGetInteger(CGPDFStreamGetDictionary(stream), "N", & n))
                    return n == 1 || n == 3 || n == 4 ? n : 0;
                if (!strcmp(name, "CalGray"))
                    return 1;
                if (!strcmp(name, "CalRGB"))
                    return 3;
            }
            return 0;
        }
        bool read(CGPDFDictionaryRef dict, CGPDFContentStreamRef cs) {
            CGPDFInteger type;  CGPDFArrayRef array;  CGPDFObjectRef obj;  CGPDFBoolean e0 = false, e1 = false;
            std::vector<float> coords, domain;  std::vector<Function> fns;
            if (!CGPDFDictionaryGetInteger(dict, "ShadingType", & type) || (type != 2 && type != 3) || CGPDFDictionaryGetArray(dict, "BBox", & array))
                return false;
            if (!readNumbers(dict, "Coords", coords) || coords.size() != (type == 2 ? 4 : 6))
                return false;
            if (!readNumbers(dict, "Domain", domain))
                domain = { 0.f, 1.f };
            if (domain.size() != 2 || domain[0] == domain[1])
                return false;
            if (CGPDFDictionaryGetArray(dict, "Extend", & array))
                CGPDFArrayGetBoolean(array, 0, & e0), CGPDFArrayGetBoolean(array, 1, & e1);
            size_t components = CGPDFDictionaryGetObject(dict, "ColorSpace", & obj) ? colorSpaceComponents(obj, cs) : 0;
            if (components == 0 || !CGPDFDictionaryGetObject(dict, "Function", & obj))
                return false;
            if (CGPDFObjectGetValue(obj, kCGPDFObjectTypeArray, & array)) {
                for (size_t i = 0; i < CGPDFArrayGetCount(array); i++)
                    if (!CGPDFArrayGetObject(array, i, & obj) || !(fns.emplace_back(), fns.back().read(obj)) || fns.back().outputs != 1)
                        return false;
                if (fns.size() != components)
                    return false;
            } else if (!(fns.emplace_back(), fns.back().read(obj)) || fns[0].outputs != components)
                return false;
                
            // Where the gradient is, as a unit transform & the stops' positions u = a + b * s for s in [0, 1] from (x0, y0, r0) to (x1, y1, r1)
            float a = 0.f, b = 1.f;
            if (type == 2) {
                float dx = coords[2] - coords[0], dy = coords[3] - coords[1];
                if (dx == 0.f && dy == 0.f)
                    return false;
                unit = Ra::Transform(-dy, dx, dx, dy, coords[0], coords[1]);
                extendLo = e0, extendHi = e1;
            } else {
                float r0 = coords[2], r1 = coords[5], r = fmaxf(r0, r1);
                if (r0 < 0.f || r1 < 0.f || r0 == r1 || fabsf(coords[3] - coords[0]) > 1e-3f * r || fabsf(coords[4] - coords[1]) > 1e-3f * r)
                    return false;       // Not concentric
                isRadial = true, unit = Ra::Transform(r, 0.f, 0.f, r, coords[0], coords[1]);
                a = r0 / r, b = (r1 - r0) / r, lo = fminf(r0, r1) / r;
                extendLo = r0 < r1 ? e0 : e1, extendHi = r0 < r1 ? e1 : e0;
            }
            
            // Stops where the functions' pieces start & end, with both limits at a stitching bound so its edge stays hard
            float t0 = domain[0], t1 = domain[1], tmin = fminf(t0, t1), tmax = fmaxf(t0, t1), eps = 1e-6f * (tmax - tmin);
            std::vector<float> ts = { tmin, tmax };
            for (auto& fn : fns)
                fn.breaks(tmin, tmax, 1.f, 0.f, ts);
            for (auto& t : ts)
                t = fmaxf(tmin, fminf(tmax, t));
            std::sort(ts.begin(), ts.end());
            ts.erase(std::unique(ts.begin(), ts.end(), [eps](float x, float y) { return y - x <= eps; }), ts.end());
            ts.back() = tmax;
            if (ts.size() < 2)
                return false;
            for (size_t k = 0; k < ts.size(); k++) {
                Ra::Color l = color(fns, components, ts[k], true), r = color(fns, components, ts[k], false);
                float u = a + b * (ts[k] - t0) / (t1 - t0);
                if (k > 0)
                    colors.emplace_back(l), locations.emplace_back(u);
                if (k == 0 || (k < ts.size() - 1 && memcmp(& l, & r, sizeof(l))))
                    colors.emplace_back(r), locations.emplace_back(u);
            }
            if (locations.front() > locations.back())
                std::reverse(colors.begin(), colors.end()), std::reverse(locations.begin(), locations.end());
            return true;
        }
        static Ra::Color color(const std::vector<Function>& fns, size_t components, float t, bool left) {
            float c[4] = { 0.f, 0.f, 0.f, 0.f }, *out = c;
            for (auto& fn : fns)
                fn.eval(t, left, out), out += fn.outputs;
            for (auto& v : c)
                v = fmaxf(0.f, fminf(1.f, v));
            float r = c[0], g = c[0], b = c[0];
            if (components == 3)
                g = c[1], b = c[2];
            else if (components == 4)
                r = (1.f - c[0]) * (1.f - c[3]), g = (1.f - c[1]) * (1.f - c[3]), b = (1.f - c[2]) * (1.f - c[3]);
            return Ra::Color(uint8_t(b * 255.f + 0.5f), uint8_t(g * 255.f + 0.5f), uint8_t(r * 255.f + 0.5f), 255);
        }
    };
    
    // The page's shadings, shading pattern fills, forms & soft masks, from its content stream's sh, path painting & Do operators
    // in order, recursing into forms, with their ctms. pdfium makes a shading, path or form object for each, in the same order,
    // so they're matched by index while the counts agree
    // A font's glyphs, as CoreText paths in text space, & widths, read with CGPDF, as pdfium can only find glyphs from Unicode, which
    // loses ligatures & contextual forms. Simple fonts map codes to glyphs by their Differences' names, else by Unicode from their
    // encoding. Type 0 fonts need an Identity CMap, & map CIDs to glyphs with their CIDToGIDMap. Type 3 fonts & other CMaps are invalid
    struct TextFont {
        TextFont(const TextFont&) = delete;
        TextFont(CGPDFDictionaryRef dict) {
            const char *subtype, *name;  CGPDFDictionaryRef font = dict, desc = nullptr, enc;  CGPDFArrayRef array;  CGPDFReal number;
            if (!CGPDFDictionaryGetName(dict, "Subtype", & subtype) || !strcmp(subtype, "Type3"))
                return;
            if ((isType0 = !strcmp(subtype, "Type0"))) {
                if (!CGPDFDictionaryGetName(dict, "Encoding", & name) || (strcmp(name, "Identity-H") && strcmp(name, "Identity-V"))
                    || !CGPDFDictionaryGetArray(dict, "DescendantFonts", & array) || !CGPDFArrayGetDictionary(array, 0, & font))
                    return;
                if (CGPDFDictionaryGetNumber(font, "DW", & number))
                    defaultWidth = number;
                if (CGPDFDictionaryGetArray(font, "W", & array))
                    readCIDWidths(array);
                CGPDFStreamRef map;
                if (CGPDFDictionaryGetStream(font, "CIDToGIDMap", & map)) {
                    CGPDFDataFormat format;  CFDataRef data = CGPDFStreamCopyData(map, & format);
                    if (data) {
                        const uint8_t *bytes = CFDataGetBytePtr(data);
                        for (CFIndex i = 0; i + 1 < CFDataGetLength(data); i += 2)
                            cidToGid.emplace_back(bytes[i] << 8 | bytes[i + 1]);
                        CFRelease(data), identityGid = false;
                    }
                }
            }
            CGPDFDictionaryGetDictionary(font, "FontDescriptor", & desc);
            CGPDFStreamRef file = nullptr;
            if (desc && (CGPDFDictionaryGetStream(desc, "FontFile3", & file) || CGPDFDictionaryGetStream(desc, "FontFile2", & file) || CGPDFDictionaryGetStream(desc, "FontFile", & file))) {
                CGPDFDataFormat format;  CFDataRef data = CGPDFStreamCopyData(file, & format);  const char *fileType;
                if (data && isType0 && identityGid && CGPDFDictionaryGetName(CGPDFStreamGetDictionary(file), "Subtype", & fileType) && !strcmp(fileType, "CIDFontType0C"))
                    readCFFCharset(CFDataGetBytePtr(data), size_t(CFDataGetLength(data)));     // CID-keyed, so CIDs aren't glyphs
                if (data) {
                    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
                    cgFont = CGFontCreateWithDataProvider(provider);
                    CGDataProviderRelease(provider), CFRelease(data);
                }
                if (cgFont)
                    ctFont = CTFontCreateWithGraphicsFont(cgFont, 1.0, nullptr, nullptr);
            }
            if (ctFont == nullptr && (CGPDFDictionaryGetName(font, "BaseFont", & name) || CGPDFDictionaryGetName(dict, "BaseFont", & name))) {
                CFStringRef system = systemFontName(name);
                ctFont = CTFontCreateWithName(system, 1.0, nullptr), CFRelease(system);
                cgFont = CTFontCopyGraphicsFont(ctFont, nullptr);
            }
            if (ctFont == nullptr || cgFont == nullptr)
                return;
            if (!isType0) {
                CGPDFInteger first = 0;
                CGPDFDictionaryGetInteger(dict, "FirstChar", & first), firstChar = int(first);
                if (CGPDFDictionaryGetArray(dict, "Widths", & array))
                    for (size_t i = 0; i < CGPDFArrayGetCount(array); i++)
                        widths.emplace_back(CGPDFArrayGetNumber(array, i, & number) ? float(number) : 0.f);
                if (desc && CGPDFDictionaryGetNumber(desc, "MissingWidth", & number))
                    missingWidth = number, hasMissingWidth = true;
                CGPDFInteger flags = 0;
                if (desc)
                    CGPDFDictionaryGetInteger(desc, "Flags", & flags);
                base = flags & 4 ? kSymbolic : kStandard;
                if (CGPDFDictionaryGetName(dict, "Encoding", & name))
                    base = encodingFor(name, base);
                else if (CGPDFDictionaryGetDictionary(dict, "Encoding", & enc)) {
                    if (CGPDFDictionaryGetName(enc, "BaseEncoding", & name))
                        base = encodingFor(name, base);
                    if (CGPDFDictionaryGetArray(enc, "Differences", & array))
                        for (size_t i = 0, code = 0; i < CGPDFArrayGetCount(array); i++) {
                            CGPDFInteger c;
                            if (CGPDFArrayGetInteger(array, i, & c))
                                code = size_t(c);
                            else if (CGPDFArrayGetName(array, i, & name) && code < 256)
                                names[code++] = name;
                        }
                }
            }
            isValid = true;
        }
        ~TextFont() {
            if (ctFont) CFRelease(ctFont);
            if (cgFont) CGFontRelease(cgFont);
        }
        // A code's glyph, or 0
        CGGlyph glyph(uint32_t code) {
            if (isType0)
                return CGGlyph(identityGid ? code : code < cidToGid.size() ? cidToGid[code] : 0);
            if (code > 255)
                return 0;
            if (!mapped[code]) {
                mapped[code] = true, glyphs[code] = 0;
                UniChar u = names[code].size() ? unicodeForName(names[code].c_str()) : unicodeForCode(code);
                const char *name = names[code].size() ? names[code].c_str() : glyphName(u);     // A font without a cmap only has names
                if (name) {
                    CFStringRef string = CFStringCreateWithCString(nullptr, name, kCFStringEncodingASCII);
                    glyphs[code] = string ? CGFontGetGlyphWithGlyphName(cgFont, string) : 0;
                    if (string) CFRelease(string);
                }
                if (glyphs[code] == 0 && base == kSymbolic) {
                    UniChar pua = UniChar(0xF000 + code);
                    CTFontGetGlyphsForCharacters(ctFont, & pua, & glyphs[code], 1);
                }
                if (glyphs[code] == 0 && u)
                    CTFontGetGlyphsForCharacters(ctFont, & u, & glyphs[code], 1);
            }
            return glyphs[code];
        }
        // A code's width, in thousandths of text space
        float width(uint32_t code, CGGlyph g) {
            if (isType0) {
                auto it = cidWidths.find(code);
                return it == cidWidths.end() ? defaultWidth : it->second;
            }
            if (int(code) >= firstChar && code - firstChar < widths.size())
                return widths[code - firstChar];
            if (hasMissingWidth)
                return missingWidth;
            CGSize advance = CGSizeZero;
            CTFontGetAdvancesForGlyphs(ctFont, kCTFontOrientationHorizontal, & g, & advance, 1);
            return float(advance.width * 1000.0);
        }
        // A glyph's outline in text space, where a unit is the font size
        Ra::Path path(CGGlyph g) {
            auto it = paths.find(g);
            if (it != paths.end())
                return it->second;
            Ra::Path path;  Ra::Path *p = & path;
            CGPathRef cgPath = CTFontCreatePathForGlyph(ctFont, g, nullptr);
            if (cgPath) {
                CGPathApplyWithBlock(cgPath, ^(const CGPathElement *e) {
                    const CGPoint *q = e->points;
                    switch (e->type) {
                        case kCGPathElementMoveToPoint:         (*p)->moveTo(q[0].x, q[0].y);  break;
                        case kCGPathElementAddLineToPoint:      (*p)->lineTo(q[0].x, q[0].y);  break;
                        case kCGPathElementAddQuadCurveToPoint: (*p)->quadTo(q[0].x, q[0].y, q[1].x, q[1].y);  break;
                        case kCGPathElementAddCurveToPoint:     (*p)->cubicTo(q[0].x, q[0].y, q[1].x, q[1].y, q[2].x, q[2].y);  break;
                        case kCGPathElementCloseSubpath:        (*p)->close();  break;
                    }
                });
                CGPathRelease(cgPath);
            }
            return paths.emplace(g, path).first->second;
        }
        
        enum Encoding { kStandard, kWinAnsi, kMacRoman, kSymbolic };
        static Encoding encodingFor(const char *name, Encoding base) {
            return !strcmp(name, "WinAnsiEncoding") ? kWinAnsi : !strcmp(name, "MacRomanEncoding") ? kMacRoman : !strcmp(name, "StandardEncoding") ? kStandard : base;
        }
        UniChar unicodeForCode(uint32_t code) const {
            uint8_t byte = code;
            if (base == kStandard && (code == 0x27 || code == 0x60))
                return code == 0x27 ? 0x2019 : 0x2018;
            CFStringRef s = CFStringCreateWithBytes(nullptr, & byte, 1, base == kMacRoman ? kCFStringEncodingMacRoman : base == kWinAnsi ? kCFStringEncodingWindowsLatin1 : kCFStringEncodingISOLatin1, false);
            UniChar u = s && CFStringGetLength(s) ? CFStringGetCharacterAtIndex(s, 0) : 0;
            if (s) CFRelease(s);
            return u;
        }
        // The standard glyph name of a Latin-1 or Windows-1252 character, or null
        static const char *glyphName(UniChar u) {
            static const char *ascii[] = { "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand", "quotesingle",
                "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period", "slash", "zero", "one", "two", "three", "four",
                "five", "six", "seven", "eight", "nine", "colon", "semicolon", "less", "equal", "greater", "question", "at" };
            static const char *ascii2[] = { "bracketleft", "backslash", "bracketright", "asciicircum", "underscore", "grave" };
            static const char *ascii3[] = { "braceleft", "bar", "braceright", "asciitilde" };
            static const char *latin1[] = { "space", "exclamdown", "cent", "sterling", "currency", "yen", "brokenbar", "section", "dieresis",
                "copyright", "ordfeminine", "guillemotleft", "logicalnot", "hyphen", "registered", "macron", "degree", "plusminus",
                "twosuperior", "threesuperior", "acute", "mu", "paragraph", "periodcentered", "cedilla", "onesuperior", "ordmasculine",
                "guillemotright", "onequarter", "onehalf", "threequarters", "questiondown", "Agrave", "Aacute", "Acircumflex", "Atilde",
                "Adieresis", "Aring", "AE", "Ccedilla", "Egrave", "Eacute", "Ecircumflex", "Edieresis", "Igrave", "Iacute", "Icircumflex",
                "Idieresis", "Eth", "Ntilde", "Ograve", "Oacute", "Ocircumflex", "Otilde", "Odieresis", "multiply", "Oslash", "Ugrave",
                "Uacute", "Ucircumflex", "Udieresis", "Yacute", "Thorn", "germandbls", "agrave", "aacute", "acircumflex", "atilde",
                "adieresis", "aring", "ae", "ccedilla", "egrave", "eacute", "ecircumflex", "edieresis", "igrave", "iacute", "icircumflex",
                "idieresis", "eth", "ntilde", "ograve", "oacute", "ocircumflex", "otilde", "odieresis", "divide", "oslash", "ugrave",
                "uacute", "ucircumflex", "udieresis", "yacute", "thorn", "ydieresis" };
            static const struct { UniChar u;  const char *name; } extras[] = { { 0x20AC, "Euro" }, { 0x201A, "quotesinglbase" },
                { 0x0192, "florin" }, { 0x201E, "quotedblbase" }, { 0x2026, "ellipsis" }, { 0x2020, "dagger" }, { 0x2021, "daggerdbl" },
                { 0x02C6, "circumflex" }, { 0x2030, "perthousand" }, { 0x0160, "Scaron" }, { 0x2039, "guilsinglleft" }, { 0x0152, "OE" },
                { 0x017D, "Zcaron" }, { 0x2018, "quoteleft" }, { 0x2019, "quoteright" }, { 0x201C, "quotedblleft" }, { 0x201D, "quotedblright" },
                { 0x2022, "bullet" }, { 0x2013, "endash" }, { 0x2014, "emdash" }, { 0x02DC, "tilde" }, { 0x2122, "trademark" },
                { 0x0161, "scaron" }, { 0x203A, "guilsinglright" }, { 0x0153, "oe" }, { 0x017E, "zcaron" }, { 0x0178, "Ydieresis" },
                { 0xFB01, "fi" }, { 0xFB02, "fl" } };
            static char letter[2];
            if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z'))
                return letter[0] = char(u), letter[1] = 0, letter;
            if (u >= 0x20 && u <= 0x40)
                return ascii[u - 0x20];
            if (u >= 0x5B && u <= 0x60)
                return ascii2[u - 0x5B];
            if (u >= 0x7B && u <= 0x7E)
                return ascii3[u - 0x7B];
            if (u >= 0xA0 && u <= 0xFF)
                return latin1[u - 0xA0];
            for (auto& e : extras)
                if (e.u == u)
                    return e.name;
            return nullptr;
        }
        // The Unicode of a glyph name: uniXXXX, uXXXX, or one ASCII character
        static UniChar unicodeForName(const char *name) {
            unsigned u = 0;
            if (strlen(name) == 1)
                return UniChar(name[0]);
            if ((sscanf(name, "uni%4x", & u) == 1 && strlen(name) == 7) || (sscanf(name, "u%x", & u) == 1 && strlen(name) <= 7))
                return u <= 0xFFFF ? UniChar(u) : 0;
            return 0;
        }
        // A font's PostScript name, without a subset prefix, as the standard fonts' system names
        static CFStringRef systemFontName(const char *base) {
            const char *plus = strchr(base, '+');
            std::string name = plus && plus - base == 6 ? plus + 1 : base;
            std::replace(name.begin(), name.end(), ',', '-');
            if (name == "ZapfDingbats")
                name = "ZapfDingbatsITC";
            return CFStringCreateWithCString(nullptr, name.c_str(), kCFStringEncodingASCII);
        }
        // A CID-keyed CFF font's charset, which lists its glyphs' CIDs, as cidToGid, if its Top DICT has ROS
        void readCFFCharset(const uint8_t *d, size_t n) {
            auto u16 = [&](size_t o) { return o + 2 <= n ? uint32_t(d[o] << 8 | d[o + 1]) : 0u; };
            auto offsetAt = [&](size_t o, int size) { uint32_t v = 0;  for (int i = 0; i < size && o + i < n; i++) v = v << 8 | d[o + i];  return v; };
            // An INDEX at o: its data's range for item i, & where it ends
            auto item = [&](size_t o, uint32_t i, size_t& begin, size_t& end) {
                uint32_t count = u16(o);  int size = o + 2 < n ? d[o + 2] : 0;
                if (count == 0) {
                    begin = end = o + 2;
                    return o + 2;
                }
                size_t offsets = o + 3, data = offsets + (count + 1) * size - 1;
                begin = data + offsetAt(offsets + i * size, size), end = data + offsetAt(offsets + (i + 1) * size, size);
                return data + offsetAt(offsets + count * size, size);
            };
            if (n < 4)
                return;
            size_t begin, end, topBegin, topEnd;
            size_t top = item(d[2], 0, begin, end);     // Past the Name INDEX
            item(top, 0, topBegin, topEnd);
            bool isCID = false;  uint32_t charset = 0, charStrings = 0;  std::vector<int32_t> operands;
            for (size_t i = topBegin; i < topEnd && topEnd <= n; ) {     // The Top DICT
                uint8_t b = d[i];
                if (b >= 32 && b <= 246)
                    operands.push_back(b - 139), i += 1;
                else if (b >= 247 && b <= 250 && i + 1 < n)
                    operands.push_back((b - 247) * 256 + d[i + 1] + 108), i += 2;
                else if (b >= 251 && b <= 254 && i + 1 < n)
                    operands.push_back(-(b - 251) * 256 - d[i + 1] - 108), i += 2;
                else if (b == 28)
                    operands.push_back(int16_t(u16(i + 1))), i += 3;
                else if (b == 29)
                    operands.push_back(int32_t(offsetAt(i + 1, 4))), i += 5;
                else if (b == 30) {     // A real, of nibbles until 0xF
                    for (i++; i < topEnd && (d[i] & 0xF) != 0xF && (d[i] >> 4) != 0xF; i++) {}
                    operands.push_back(0), i++;
                } else {
                    int op = b == 12 && i + 1 < n ? 1200 + d[i + 1] : b;
                    i += b == 12 ? 2 : 1;
                    if (op == 1230)
                        isCID = true;
                    else if (op == 15 && operands.size())
                        charset = uint32_t(operands.back());
                    else if (op == 17 && operands.size())
                        charStrings = uint32_t(operands.back());
                    operands.clear();
                }
            }
            if (!isCID || charset == 0 || charStrings == 0 || charStrings >= n)
                return;
            uint32_t glyphCount = u16(charStrings);
            std::vector<uint32_t> cids(1, 0);     // GID 0, .notdef, is CID 0
            size_t o = charset + 1;
            if (charset < n && d[charset] == 0)
                for (; cids.size() < glyphCount && o + 2 <= n; o += 2)
                    cids.push_back(u16(o));
            else if (charset < n && (d[charset] == 1 || d[charset] == 2))
                for (int big = d[charset] == 2; cids.size() < glyphCount && o + 3 + big <= n; o += 3 + big)
                    for (uint32_t first = u16(o), left = big ? u16(o + 2) : d[o + 2], c = 0; c <= left && cids.size() < glyphCount; c++)
                        cids.push_back(first + c);
            for (size_t g = 0; g < cids.size(); g++) {
                if (cids[g] >= cidToGid.size())
                    cidToGid.resize(cids[g] + 1, 0);
                cidToGid[cids[g]] = uint16_t(g);
            }
            identityGid = false;
        }
        void readCIDWidths(CGPDFArrayRef w) {
            CGPDFInteger first, last;  CGPDFArrayRef array;  CGPDFReal number;
            for (size_t i = 0, n = CGPDFArrayGetCount(w); i + 1 < n; ) {
                if (!CGPDFArrayGetInteger(w, i, & first))
                    return;
                if (CGPDFArrayGetArray(w, i + 1, & array)) {
                    for (size_t j = 0; j < CGPDFArrayGetCount(array); j++)
                        if (CGPDFArrayGetNumber(array, j, & number))
                            cidWidths[uint32_t(first + j)] = number;
                    i += 2;
                } else if (i + 2 < n && CGPDFArrayGetInteger(w, i + 1, & last) && CGPDFArrayGetNumber(w, i + 2, & number)) {
                    for (CGPDFInteger c = first; c <= last && c - first < 65536; c++)
                        cidWidths[uint32_t(c)] = number;
                    i += 3;
                } else
                    return;
            }
        }
        
        bool isValid = false, isType0 = false, identityGid = true, hasMissingWidth = false;
        CTFontRef ctFont = nullptr;  CGFontRef cgFont = nullptr;
        Encoding base = kStandard;
        int firstChar = 0;  std::vector<float> widths;  float missingWidth = 0.f, defaultWidth = 1000.f;
        std::map<uint32_t, float> cidWidths;  std::vector<uint16_t> cidToGid;
        std::array<std::string, 256> names;  std::array<CGGlyph, 256> glyphs = {};  std::array<bool, 256> mapped = {};
        std::map<CGGlyph, Ra::Path> paths;
    };
    
    // An image XObject or inline image, decoded with CGPDF, ImageIO & CoreGraphics, which converts its color space to RGB, as
    // straight alpha BGRA. An image mask's pixels are its alpha, painted with the fill color. Images too large for a texture
    // are drawn smaller
    struct PDFImage {
        static constexpr size_t kMaxSize = 16384;
        bool isValid = false, isMask = false;
        size_t width = 0, height = 0;
        std::vector<Ra::Color> pixels;
        
        PDFImage(CGPDFStreamRef stream, CGPDFContentStreamRef cs) {
            CGPDFDictionaryRef dict = CGPDFStreamGetDictionary(stream);
            CGPDFBoolean imageMask = false;
            CGPDFDictionaryGetBoolean(dict, "ImageMask", & imageMask) || CGPDFDictionaryGetBoolean(dict, "IM", & imageMask);
            Samples samples;
            CGImageRef image = createImage(stream, cs, imageMask, samples);
            if (image == nullptr)
                return;
            float scale = fminf(1.f, float(kMaxSize) / fmaxf(CGImageGetWidth(image), CGImageGetHeight(image)));
            width = fmaxf(1.f, roundf(CGImageGetWidth(image) * scale)), height = fmaxf(1.f, roundf(CGImageGetHeight(image) * scale));
            pixels.resize(width * height);
            isMask = imageMask, isValid = true;
            if (isMask) {
                std::vector<uint8_t> alpha = drawAlpha(image);
                for (size_t i = 0; i < pixels.size(); i++)
                    pixels[i] = Ra::Color(0, 0, 0, alpha[i]);
            } else {
                draw(image, pixels.data(), false);
                CGImageAlphaInfo info = CGImageGetAlphaInfo(image);
                if (info != kCGImageAlphaNone && info != kCGImageAlphaNoneSkipFirst && info != kCGImageAlphaNoneSkipLast)     // A JPX's own alpha
                    for (auto& p : pixels)
                        if (p.a != 0 && p.a != 255)
                            p = Ra::Color(p.b * 255 / p.a, p.g * 255 / p.a, p.r * 255 / p.a, p.a);
                CGPDFStreamRef mask;  CGPDFArrayRef key;
                Samples maskSamples;
                CGImageRef maskImage = nullptr;
                if (CGPDFDictionaryGetStream(dict, "SMask", & mask))
                    maskImage = createImage(mask, cs, false, maskSamples);
                else if (CGPDFDictionaryGetStream(dict, "Mask", & mask))
                    maskImage = createImage(mask, cs, true, maskSamples);
                if (maskImage) {
                    std::vector<uint8_t> alpha = drawAlpha(maskImage);
                    for (size_t i = 0; i < pixels.size(); i++)
                        pixels[i].a = alpha[i];
                    CGImageRelease(maskImage);
                } else if (CGPDFDictionaryGetArray(dict, "Mask", & key))
                    applyColorKey(key, samples);
            }
            CGImageRelease(image);
        }
        // A stream's raw samples, for a color key mask, if they weren't a JPEG's
        struct Samples {
            CFDataRef data = nullptr;
            size_t width = 0, height = 0, components = 0, bpc = 0, rowBytes = 0;
            ~Samples() { if (data) CFRelease(data); }
        };
        static CGPDFObjectRef getObject(CGPDFDictionaryRef dict, const char *key, const char *abbreviation) {
            CGPDFObjectRef obj = nullptr;
            CGPDFDictionaryGetObject(dict, key, & obj) || CGPDFDictionaryGetObject(dict, abbreviation, & obj);
            return obj;
        }
        static size_t getInteger(CGPDFDictionaryRef dict, const char *key, const char *abbreviation) {
            CGPDFObjectRef obj = getObject(dict, key, abbreviation);  CGPDFInteger value = 0;  CGPDFReal real;
            if (obj && !CGPDFObjectGetValue(obj, kCGPDFObjectTypeInteger, & value) && CGPDFObjectGetValue(obj, kCGPDFObjectTypeReal, & real))
                value = CGPDFInteger(real);
            return value > 0 ? size_t(value) : 0;
        }
        static bool hasFilter(CGPDFDictionaryRef dict, std::initializer_list<const char *> names) {
            CGPDFObjectRef obj = getObject(dict, "Filter", "F");  CGPDFArrayRef array;  const char *name;
            auto isOne = [&](const char *filter) { return std::any_of(names.begin(), names.end(), [&](const char *n) { return !strcmp(n, filter); }); };
            if (obj && CGPDFObjectGetValue(obj, kCGPDFObjectTypeName, & name))
                return isOne(name);
            for (size_t i = 0; obj && CGPDFObjectGetValue(obj, kCGPDFObjectTypeArray, & array) && i < CGPDFArrayGetCount(array); i++)
                if (CGPDFArrayGetName(array, i, & name) && isOne(name))
                    return true;
            return false;
        }
        static uint32_t sample(const uint8_t *row, size_t i, size_t bpc) {     // The ith sample of a row
            if (bpc == 8)
                return row[i];
            if (bpc == 16)
                return row[2 * i] << 8 | row[2 * i + 1];
            size_t bit = i * bpc;
            return (row[bit >> 3] >> (8 - bpc - (bit & 7))) & ((1 << bpc) - 1);
        }
        // The image of a stream, in its color space, with its decode array, or for a mask, gray, with 1 where it's painted
        static CGImageRef createImage(CGPDFStreamRef stream, CGPDFContentStreamRef cs, bool isMask, Samples& samples) {
            CGPDFDictionaryRef dict = CGPDFStreamGetDictionary(stream);
            CGPDFDataFormat format;
            CFDataRef data = CGPDFStreamCopyData(stream, & format);
            if (data == nullptr)
                return nullptr;
            CGImageRef image = nullptr;
            size_t width = getInteger(dict, "Width", "W"), height = getInteger(dict, "Height", "H"), bpc = isMask ? 1 : getInteger(dict, "BitsPerComponent", "BPC");
            std::vector<CGFloat> decode;  CGPDFArrayRef array;  CGPDFObjectRef obj;  CGPDFReal number;
            if ((obj = getObject(dict, "Decode", "D")) && CGPDFObjectGetValue(obj, kCGPDFObjectTypeArray, & array))
                for (size_t i = 0; i < CGPDFArrayGetCount(array) && CGPDFArrayGetNumber(array, i, & number); i++)
                    decode.emplace_back(number);
            Function tint;
            CGColorSpaceRef space = isMask ? CGColorSpaceCreateDeviceGray() : nullptr;
            if (!isMask && (obj = getObject(dict, "ColorSpace", "CS")))
                space = createColorSpace(obj, cs, & tint);
            size_t components = isMask ? 1 : tint.type >= 0 ? 1 : space ? CGColorSpaceGetNumberOfComponents(space) : 0;
            if (isMask)     // A mask's sample of 0 is painted, unless its decode array is [1 0]
                decode = decode.size() == 2 && decode[0] > decode[1] ? std::vector<CGFloat>{ 0, 1 } : std::vector<CGFloat>{ 1, 0 };
            if (format != CGPDFDataFormatRaw) {
                CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
                CGImageRef decoded = source ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
                if (source)
                    CFRelease(source);
                if (decoded && space && tint.type < 0 && CGImageGetColorSpace(decoded) && components == CGColorSpaceGetNumberOfComponents(CGImageGetColorSpace(decoded))) {
                    // Its stored samples in the PDF's color space, with its decode array, not ImageIO's, which inverts an Adobe
                    // CMYK JPEG's, as a PDF's DCTDecode doesn't. Photoshop's PDFs invert them with a decode array instead
                    size_t n = components, bpp = CGImageGetBitsPerPixel(decoded);
                    CFDataRef pixels = CGImageGetBitsPerComponent(decoded) == 8 && bpp == 8 * n && CGImageGetAlphaInfo(decoded) == kCGImageAlphaNone
                        ? CGDataProviderCopyData(CGImageGetDataProvider(decoded)) : nullptr;
                    if (pixels) {
                        CGDataProviderRef provider = CGDataProviderCreateWithCFData(pixels);
                        image = CGImageCreate(CGImageGetWidth(decoded), CGImageGetHeight(decoded), 8, bpp, CGImageGetBytesPerRow(decoded), space, CGBitmapInfo(kCGImageAlphaNone), provider, decode.size() == 2 * n ? decode.data() : nullptr, false, kCGRenderingIntentDefault);
                        CGDataProviderRelease(provider), CFRelease(pixels), CGImageRelease(decoded);
                    } else if ((image = CGImageCreateCopyWithColorSpace(decoded, space)))
                        CGImageRelease(decoded);
                    else
                        image = decoded;
                } else
                    image = decoded;
            } else if (width && height && space && (bpc == 1 || bpc == 2 || bpc == 4 || bpc == 8 || bpc == 16) && components) {
                size_t rowBytes = (width * components * bpc + 7) / 8;
                if (size_t(CFDataGetLength(data)) < rowBytes * height) {
                    // Short data is padded, as pdfium does, but CCITT & JBIG2 data CGPDF couldn't decode is pdfium's
                    if (hasFilter(dict, { "CCITTFaxDecode", "CCF", "JBIG2Decode" })) {
                        CGColorSpaceRelease(space), CFRelease(data);
                        return nullptr;
                    }
                    CFMutableDataRef padded = CFDataCreateMutableCopy(nullptr, rowBytes * height, data);
                    CFDataSetLength(padded, rowBytes * height), CFRelease(data), data = padded;
                }
                if (CGPDFDictionaryGetArray(dict, "Mask", & array)) {
                    samples.width = width, samples.height = height, samples.components = components, samples.bpc = bpc, samples.rowBytes = rowBytes;
                    samples.data = (CFDataRef)CFRetain(data);
                }
                CFDataRef pixels = tint.type >= 0 ? createTinted(data, width, height, bpc, rowBytes, decode, tint, CGColorSpaceGetNumberOfComponents(space)) : (CFDataRef)CFRetain(data);
                if (pixels) {
                    bool tinted = tint.type >= 0;
                    size_t n = tinted ? CGColorSpaceGetNumberOfComponents(space) : components, depth = tinted ? 8 : bpc;
                    CGDataProviderRef provider = CGDataProviderCreateWithCFData(pixels);
                    image = CGImageCreate(width, height, depth, depth * n, tinted ? width * n : rowBytes, space, CGBitmapInfo(kCGImageAlphaNone), provider,
                        !tinted && decode.size() == 2 * n ? decode.data() : nullptr, false, kCGRenderingIntentDefault);
                    CGDataProviderRelease(provider), CFRelease(pixels);
                }
            }
            CGColorSpaceRelease(space);
            CFRelease(data);
            return image;
        }
        // A Separation or DeviceN of one component's samples, as 8 bit samples of its alternate space, by its tint transform
        static CFDataRef createTinted(CFDataRef data, size_t width, size_t height, size_t bpc, size_t rowBytes, std::vector<CGFloat>& decode, Function& tint, size_t n) {
            if (tint.outputs != n)
                return nullptr;
            float d0 = decode.size() == 2 ? decode[0] : 0.f, d1 = decode.size() == 2 ? decode[1] : 1.f, max = float((1 << bpc) - 1);
            std::vector<uint8_t> lut(n << bpc);         // Of each sample value, as bpc is at most 16
            std::vector<float> out(n);
            for (uint32_t s = 0; s < (1u << bpc); s++) {
                tint.eval(d0 + s * (d1 - d0) / max, false, out.data());
                for (size_t j = 0; j < n; j++)
                    lut[s * n + j] = uint8_t(fmaxf(0.f, fminf(1.f, out[j])) * 255.f + 0.5f);
            }
            CFMutableDataRef tinted = CFDataCreateMutable(nullptr, width * height * n);
            CFDataSetLength(tinted, width * height * n);
            const uint8_t *src = CFDataGetBytePtr(data);  uint8_t *dst = CFDataGetMutableBytePtr(tinted);
            for (size_t y = 0; y < height; y++, src += rowBytes)
                for (size_t x = 0; x < width; x++, dst += n)
                    memcpy(dst, & lut[sample(src, x, bpc) * n], n);
            return tinted;
        }
        // The CGColorSpace of a PDF color space, or null if CoreGraphics can't draw it. A Separation, or a DeviceN of one
        // component, is its alternate space, & its tint transform is read into tint
        static CGColorSpaceRef createColorSpace(CGPDFObjectRef obj, CGPDFContentStreamRef cs, Function *tint, int depth = 0) {
            const char *name;  CGPDFArrayRef array, names;  CGPDFStreamRef stream;  CGPDFDictionaryRef dict;  CGPDFObjectRef object;
            if (depth > 4)
                return nullptr;
            if (CGPDFObjectGetValue(obj, kCGPDFObjectTypeName, & name)) {
                if (!strcmp(name, "DeviceGray") || !strcmp(name, "G") || !strcmp(name, "CalGray"))
                    return CGColorSpaceCreateDeviceGray();
                if (!strcmp(name, "DeviceRGB") || !strcmp(name, "RGB") || !strcmp(name, "CalRGB"))
                    return CGColorSpaceCreateDeviceRGB();
                if (!strcmp(name, "DeviceCMYK") || !strcmp(name, "CMYK"))
                    return CGColorSpaceCreateDeviceCMYK();
                CGPDFObjectRef resource = CGPDFContentStreamGetResource(cs, "ColorSpace", name);
                return resource ? createColorSpace(resource, cs, tint, depth + 1) : nullptr;
            }
            if (!CGPDFObjectGetValue(obj, kCGPDFObjectTypeArray, & array) || !CGPDFArrayGetName(array, 0, & name))
                return nullptr;
            if (!strcmp(name, "ICCBased") && CGPDFArrayGetStream(array, 1, & stream)) {
                CGPDFDataFormat format;
                CFDataRef data = CGPDFStreamCopyData(stream, & format);
                CGColorSpaceRef space = data ? CGColorSpaceCreateWithICCData(data) : nullptr;
                if (data)
                    CFRelease(data);
                if (space)
                    return space;
                dict = CGPDFStreamGetDictionary(stream);
                if (CGPDFDictionaryGetObject(dict, "Alternate", & object))
                    return createColorSpace(object, cs, nullptr, depth + 1);
                CGPDFInteger n = 0;
                CGPDFDictionaryGetInteger(dict, "N", & n);
                return n == 1 ? CGColorSpaceCreateDeviceGray() : n == 3 ? CGColorSpaceCreateDeviceRGB() : n == 4 ? CGColorSpaceCreateDeviceCMYK() : nullptr;
            }
            if ((!strcmp(name, "CalGray") || !strcmp(name, "CalRGB") || !strcmp(name, "Lab")) && CGPDFArrayGetDictionary(array, 1, & dict)) {
                std::vector<float> white, black, gamma, matrix, range;
                readNumbers(dict, "WhitePoint", white), readNumbers(dict, "BlackPoint", black), readNumbers(dict, "Gamma", gamma);
                readNumbers(dict, "Matrix", matrix), readNumbers(dict, "Range", range);
                CGFloat w[3] = { 0.9505, 1, 1.089 }, b[3] = { 0, 0, 0 }, g[3] = { 1, 1, 1 }, m[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, r[4] = { -100, 100, -100, 100 };
                for (size_t i = 0; i < 3 && white.size() == 3; i++)
                    w[i] = white[i];
                for (size_t i = 0; i < 3 && black.size() == 3; i++)
                    b[i] = black[i];
                if (!strcmp(name, "CalGray"))
                    return CGColorSpaceCreateCalibratedGray(w, b, gamma.size() == 1 ? gamma[0] : 1.f);
                if (!strcmp(name, "Lab")) {
                    for (size_t i = 0; i < 4 && range.size() == 4; i++)
                        r[i] = range[i];
                    return CGColorSpaceCreateLab(w, b, r);
                }
                for (size_t i = 0; i < 3 && gamma.size() == 3; i++)
                    g[i] = gamma[i];
                for (size_t i = 0; i < 9 && matrix.size() == 9; i++)
                    m[i] = matrix[i];
                return CGColorSpaceCreateCalibratedRGB(w, b, g, m);
            }
            if ((!strcmp(name, "Indexed") || !strcmp(name, "I")) && CGPDFArrayGetObject(array, 1, & object)) {
                CGColorSpaceRef base = createColorSpace(object, cs, nullptr, depth + 1);
                CGPDFInteger hival = -1;  CGPDFStringRef string;  CFDataRef data = nullptr;  CGPDFDataFormat format;
                if (base && CGPDFArrayGetInteger(array, 2, & hival) && hival >= 0 && hival < 256) {
                    if (CGPDFArrayGetString(array, 3, & string))
                        data = CFDataCreate(nullptr, CGPDFStringGetBytePtr(string), CGPDFStringGetLength(string));
                    else if (CGPDFArrayGetStream(array, 3, & stream))
                        data = CGPDFStreamCopyData(stream, & format);
                }
                size_t size = data ? (hival + 1) * CGColorSpaceGetNumberOfComponents(base) : 0;
                CGColorSpaceRef space = nullptr;
                if (data && size_t(CFDataGetLength(data)) >= size)
                    space = CGColorSpaceCreateIndexed(base, hival, CFDataGetBytePtr(data));
                if (data)
                    CFRelease(data);
                CGColorSpaceRelease(base);
                return space;
            }
            // A tint transform's alternate space must be of components in [0, 1]
            bool isSeparation = !strcmp(name, "Separation"), isDeviceN = !strcmp(name, "DeviceN");
            if (tint && (isSeparation || (isDeviceN && CGPDFArrayGetArray(array, 1, & names) && CGPDFArrayGetCount(names) == 1))
                && CGPDFArrayGetObject(array, 2, & object) && CGPDFArrayGetObject(array, 3, & obj) && tint->read(obj)) {
                CGColorSpaceRef space = createColorSpace(object, cs, nullptr, depth + 1);
                CGColorSpaceModel model = space ? CGColorSpaceGetModel(space) : kCGColorSpaceModelUnknown;
                if (model == kCGColorSpaceModelMonochrome || model == kCGColorSpaceModelRGB || model == kCGColorSpaceModelCMYK)
                    return space;
                CGColorSpaceRelease(space);
            }
            if (tint)
                tint->type = -1;
            return nullptr;
        }
        // Draws an image into pixels, width x height, in DeviceRGB, or DeviceGray for alpha, which is scaled to the image as
        // pdfium does, without interpolation
        void draw(CGImageRef image, void *pixels, bool isGray) const {
            CGColorSpaceRef space = isGray ? CGColorSpaceCreateDeviceGray() : CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate(pixels, width, height, 8, isGray ? width : width * sizeof(Ra::Color), space,
                isGray ? uint32_t(kCGImageAlphaNone) : uint32_t(kCGImageAlphaPremultipliedFirst) | kCGBitmapByteOrder32Little);
            if (ctx) {
                bool same = CGImageGetWidth(image) == width && CGImageGetHeight(image) == height;
                CGContextSetInterpolationQuality(ctx, same || isGray ? kCGInterpolationNone : kCGInterpolationDefault);
                CGContextDrawImage(ctx, CGRectMake(0, 0, width, height), image);
                CGContextRelease(ctx);
            }
            CGColorSpaceRelease(space);
        }
        std::vector<uint8_t> drawAlpha(CGImageRef image) const {
            std::vector<uint8_t> alpha(width * height);
            draw(image, alpha.data(), true);
            return alpha;
        }
        // Pixels whose samples are all in the color key's ranges are transparent
        void applyColorKey(CGPDFArrayRef key, Samples& samples) {
            size_t n = samples.components;  CGPDFInteger value;
            std::vector<uint32_t> ranges;
            for (size_t i = 0; i < CGPDFArrayGetCount(key) && CGPDFArrayGetInteger(key, i, & value); i++)
                ranges.emplace_back(uint32_t(value));
            if (samples.data == nullptr || ranges.size() != 2 * n)
                return;
            const uint8_t *base = CFDataGetBytePtr(samples.data);
            for (size_t y = 0; y < height; y++) {
                const uint8_t *row = base + (y * samples.height / height) * samples.rowBytes;
                for (size_t x = 0; x < width; x++) {
                    size_t sx = x * samples.width / width;  bool inRange = true;
                    for (size_t j = 0; j < n && inRange; j++) {
                        uint32_t s = sample(row, sx * n + j, samples.bpc);
                        inRange = s >= ranges[2 * j] && s <= ranges[2 * j + 1];
                    }
                    if (inRange)
                        pixels[y * width + x] = Ra::Color(0, 0, 0, 0);
                }
            }
        }
    };
    
    // A color space of fill & stroke colors, which converts them to RGB: device gray & RGB exactly, CalGray, CalRGB & Lab
    // as pdfium does, & CMYK & ICC spaces with CoreGraphics, as images' are, cached as it's slow. An Indexed space looks up
    // its base's colors, & a Separation or DeviceN uses its tint transform. A space the scan can't convert has no colors, so
    // pdfium's are used
    struct ColorSpace {
        enum Family { kGray, kRGB, kCalRGB, kLab, kOther, kIndexed, kTint, kPattern, kUnsupported };
        Family family = kUnsupported;
        size_t components = 1;
        float white[3] = { 0.9505f, 1.f, 1.089f }, gamma[3] = { 1.f, 1.f, 1.f }, matrix[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, range[4] = { -100, 100, -100, 100 };
        bool hasMatrix = false;
        CGColorSpaceRef space = nullptr;        // An other space's
        const ColorSpace *base = nullptr;       // An Indexed or tint transform's base, in its Shadings' cache
        std::vector<uint8_t> lookup;
        size_t hival = 0;
        Function tint;
        mutable std::map<std::vector<float>, Ra::Color> colors;
        
        ColorSpace(Family family = kUnsupported, size_t components = 1, CGColorSpaceRef space = nullptr) : family(family), components(components), space(space) {}
        ColorSpace(const ColorSpace&) = delete;
        ~ColorSpace() { CGColorSpaceRelease(space); }
        static uint8_t quantize(float v) { return uint8_t(fmaxf(0.f, fminf(1.f, v)) * 255.f + 0.5f); }
        static float encode(float c) {      // sRGB's transfer function
            c = fmaxf(0.f, fminf(1.f, c));
            return c <= 0.0031308f ? 12.92f * c : 1.055f * powf(c, 1.f / 2.4f) - 0.055f;
        }
        // XYZ to sRGB, as pdfium does: with sRGB's primaries & the white point for CalRGB, & pdfium's fixed matrix for Lab
        static Ra::Color rgbForXYZ(float X, float Y, float Z, const float *white) {
            float r, g, b;
            if (white == nullptr)
                r = 3.2410f * X - 1.5374f * Y - 0.4986f * Z, g = -0.9692f * X + 1.8760f * Y + 0.0416f * Z, b = 0.0556f * X - 0.2040f * Y + 1.0570f * Z;
            else {
                // M = P diag(S), where P's columns are the primaries' xyz, & P S = white, so RGB = M^-1 XYZ = S^-1 P^-1 XYZ
                const float P[9] = { 0.64f, 0.30f, 0.15f, 0.33f, 0.60f, 0.06f, 0.03f, 0.10f, 0.79f };
                float inv[9], det = P[0] * (P[4] * P[8] - P[5] * P[7]) - P[1] * (P[3] * P[8] - P[5] * P[6]) + P[2] * (P[3] * P[7] - P[4] * P[6]);
                inv[0] = (P[4] * P[8] - P[5] * P[7]) / det, inv[1] = (P[2] * P[7] - P[1] * P[8]) / det, inv[2] = (P[1] * P[5] - P[2] * P[4]) / det;
                inv[3] = (P[5] * P[6] - P[3] * P[8]) / det, inv[4] = (P[0] * P[8] - P[2] * P[6]) / det, inv[5] = (P[2] * P[3] - P[0] * P[5]) / det;
                inv[6] = (P[3] * P[7] - P[4] * P[6]) / det, inv[7] = (P[1] * P[6] - P[0] * P[7]) / det, inv[8] = (P[0] * P[4] - P[1] * P[3]) / det;
                auto apply = [&](float x, float y, float z, float *v) {
                    v[0] = inv[0] * x + inv[1] * y + inv[2] * z, v[1] = inv[3] * x + inv[4] * y + inv[5] * z, v[2] = inv[6] * x + inv[7] * y + inv[8] * z;
                };
                float S[3], v[3];
                apply(white[0], white[1], white[2], S), apply(X, Y, Z, v);
                r = S[0] == 0.f ? 0.f : v[0] / S[0], g = S[1] == 0.f ? 0.f : v[1] / S[1], b = S[2] == 0.f ? 0.f : v[2] / S[2];
            }
            return Ra::Color(quantize(encode(b)), quantize(encode(g)), quantize(encode(r)), 255);
        }
        // The color of components c, as RGB, if the space has one
        bool color(const float *c, size_t n, Ra::Color& out) const {
            if (n < components)
                return false;
            switch (family) {
                case kGray:
                    return out = Ra::Color(quantize(c[0]), quantize(c[0]), quantize(c[0]), 255), true;
                case kRGB:
                    return out = Ra::Color(quantize(c[2]), quantize(c[1]), quantize(c[0]), 255), true;
                case kCalRGB: {
                    float a = powf(fmaxf(0.f, c[0]), gamma[0]), b = powf(fmaxf(0.f, c[1]), gamma[1]), d = powf(fmaxf(0.f, c[2]), gamma[2]);
                    const float *m = matrix;
                    return out = hasMatrix ? rgbForXYZ(a * m[0] + b * m[3] + d * m[6], a * m[1] + b * m[4] + d * m[7], a * m[2] + b * m[5] + d * m[8], white) : rgbForXYZ(a, b, d, white), true;
                }
                case kLab: {
                    float L = fmaxf(0.f, fminf(100.f, c[0])), A = fmaxf(range[0], fminf(range[1], c[1])), B = fmaxf(range[2], fminf(range[3], c[2]));
                    float M = (L + 16.f) / 116.f, l = M + A / 500.f, n = M - B / 200.f;
                    auto f = [](float t) { return t < 0.2069f ? 0.12842f * (t - 0.1379f) : t * t * t; };
                    return out = rgbForXYZ(0.957f * f(l), f(M), 1.0889f * f(n), nullptr), true;
                }
                case kOther: {
                    std::vector<float> key(c, c + components);
                    auto it = colors.find(key);
                    if (it != colors.end())
                        return out = it->second, true;
                    std::vector<CGFloat> values(c, c + components);
                    values.emplace_back(1.0);
                    CGColorRef color = CGColorCreate(space, values.data());
                    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
                    CGColorRef matched = color ? CGColorCreateCopyByMatchingToColorSpace(rgb, kCGRenderingIntentDefault, color, nullptr) : nullptr;
                    bool converted = matched && CGColorGetNumberOfComponents(matched) >= 3;
                    if (converted) {
                        const CGFloat *v = CGColorGetComponents(matched);
                        out = colors[key] = Ra::Color(quantize(v[2]), quantize(v[1]), quantize(v[0]), 255);
                    }
                    CGColorRelease(matched), CGColorRelease(color), CGColorSpaceRelease(rgb);
                    return converted;
                }
                case kIndexed: {
                    size_t i = size_t(fmaxf(0.f, fminf(float(hival), roundf(c[0])))), m = base->components;
                    std::vector<float> values(m);
                    for (size_t j = 0; j < m && (i + 1) * m <= lookup.size(); j++)
                        values[j] = lookup[i * m + j] / 255.f;
                    return (i + 1) * m <= lookup.size() && base->color(values.data(), m, out);
                }
                case kTint: {
                    std::vector<float> values(tint.outputs);
                    tint.eval(c, values.data());
                    return base->color(values.data(), values.size(), out);
                }
                default:
                    return false;
            }
        }
    };
    
    struct Shadings {
        static constexpr size_t kMaxDepth = 32;
        enum { kStroke = 4 };       // A path's draw mode is its FPDF_FILLMODE, & kStroke if it's stroked, as pdfium's
        // The objects pdfium makes from each content stream, in order, each with its record in paths, texts, shadings or forms,
        // & its clip in clips, or -1 if it's unclipped
        enum Kind : uint8_t { kPathObject, kTextObject, kImageObject, kShadingObject, kFormObject, kNoObject };
        struct Object {
            uint8_t kind;
            uint32_t index;
            int clip;
            FPDF_PAGEOBJECT pdfium = nullptr, parent = nullptr;     // Its pdfium object & that's form, if they're matched
        };
        std::vector<std::vector<Object>> streams;   // The page's, then each form's, by form index + 1
        struct Colors {             // An object's fill & stroke colors, with their alphas, if the scan has them
            Ra::Color fill = Ra::Color(0, 0, 0, 255), stroke = Ra::Color(0, 0, 0, 255);
            bool hasFill = true, hasStroke = true;
        };
        // The scan's color spaces, by their object, & the device ones, as fill & stroke colors' spaces are pointers to them
        ColorSpace graySpace { ColorSpace::kGray, 1 }, rgbSpace { ColorSpace::kRGB, 3 };
        ColorSpace cmykSpace { ColorSpace::kOther, 4, CGColorSpaceCreateDeviceCMYK() }, patternSpace { ColorSpace::kPattern }, unsupportedSpace;
        std::map<CGPDFObjectRef, std::unique_ptr<ColorSpace>> colorSpaces;
        // An image object's image in images, or -1 if it couldn't be decoded, & its graphics state
        struct ImageObject {
            int image = -1;
            Ra::Transform ctm;      // The unit square to page space
            float alpha = 1.f;
            int mask = kNoMask;     // Its soft mask, in masks
            uint8_t blend = kBlendNormal;
            Colors colors;          // An image mask's
        };
        std::vector<ImageObject> imageObjects;
        std::deque<PDFImage> images;
        std::map<CGPDFStreamRef, int> imageIndices;         // An XObject's image, decoded once
        std::map<std::tuple<int, float, uint32_t, int, std::array<float, 6>>, Ra::Paint> imagePaints;
        static constexpr float kMaxMaskSize = 4096.f;       // Of a masked image enlarged for its mask
        bool readsImages = true;                            // Not for a soft mask's group
        // A path painting operator's path, in user space, & its paint, but for its colors
        struct PathObject {
            Ra::Path path = nullptr;
            Ra::Transform ctm;      // User space to page space
            int pattern = -1;       // The fill's shading pattern in patterns, or -1 for a color
            int mask = kNoMask;     // In masks
            float alpha = 1.f, width = 1.f, dashPhase = 0.f;
            int dash = -1;          // In dashes, or -1 for a solid stroke
            uint8_t blend = kBlendNormal, mode = 0, cap = 0, join = 0;
            Colors colors;
            Ra::Bounds bounds;      // Of its points, in its form's space, as pdfium's bounds of a fill. pdfium's form space is the
                                    // ctm at the Do, & its objects' matrices include the form matrix
        };
        std::deque<PathObject> paths;       // A deque, as there can be very many, which a vector would copy as it grows
        std::vector<std::vector<float>> dashes;
        // A clip state's paths, in page space, as pdfium's: the content stream's own, then those of its form's Do. Draws are only
        // clipped to the first path, & the bounds of all, so sorted puts the non-rect ones first
        struct Clip {
            std::vector<Ra::Path> paths, sorted;
            size_t own = 0;
            int inherited = -1;     // The clip of the paths that aren't the stream's own
            Ra::Bounds bounds = Ra::Bounds::huge();
        };
        std::vector<Clip> clips;
        std::map<size_t, Ra::Path> clipPaths;       // By hash, so draws with the same clip share its path, & so its clip mask
        std::vector<Shading> shadings, patterns, masks;
        struct Group {              // A transparency group form's soft mask, opacity & blend mode, which its contents start without
            int mask = kNoMask;
            float alpha = 1.f;
            uint8_t blend = kBlendNormal;
            int clip = -1;          // Its contents' clip at their start, in clips
            Ra::Transform ctm;      // The ctm at its Do, which is pdfium's space for its objects
        };
        // A text showing operator's glyphs, as pdfium's text objects, each with its text rendering matrix, from text space to page space
        struct Glyph { Ra::Path path;  Ra::Transform ctm; };
        struct TextRun {
            bool isValid = true;
            uint8_t mode = 0;           // Its text rendering mode
            Colors colors;
            std::vector<Glyph> glyphs;
            float unitsPerEm = 1.f, width = 0.f;    // User space units in text space's, & the stroke width
        };
        std::vector<TextRun> texts;
        std::map<CGPDFDictionaryRef, std::unique_ptr<TextFont>> fonts;
        TextFont *fontFor(CGPDFDictionaryRef dict) {
            auto it = fonts.find(dict);
            if (it == fonts.end())
                it = fonts.emplace(dict, std::unique_ptr<TextFont>(new TextFont(dict))).first;
            return it->second.get();
        }
        typedef std::pair<CGPDFDictionaryRef, std::array<float, 6>> MaskKey;     // A soft mask & the ctm it's set at
        std::map<MaskKey, int> maskIndices;
        std::map<CGPDFDictionaryRef, Shading> maskGroups;
        std::vector<Group> forms;
        
        struct State {
            Ra::Transform ctm, space;   // space is the content stream's default space, which pattern matrices map to
            float alpha = 1.f, strokeAlpha = 1.f;     // The fill & stroke alphas, ca & CA
            const ColorSpace *fillSpace = nullptr, *strokeSpace = nullptr;     // Null for DeviceGray
            Colors colors;              // Without their alphas
            bool isPatternSpace = false;
            int pattern = -1, mask = kNoMask, clip = -1;
            uint8_t blend = kBlendNormal;
            float lineWidth = 1.f, dashPhase = 0.f;     // The stroke state
            int dash = -1;
            uint8_t cap = 0, join = 0;
            TextFont *font = nullptr;   // The text state, which q & Q save & restore
            bool hasFont = false;       // pdfium's font for a Tf of a missing font is Helvetica, but it makes no text before a Tf
            uint8_t textMode = 0;       // The text rendering mode, Tr
            float fontSize = 0.f, charSpace = 0.f, wordSpace = 0.f, hScale = 1.f, leading = 0.f, rise = 0.f;
        };
        struct PathPoint {              // As pdfium's path points: a move, line or one of a cubic's three, which can close
            float x, y;
            uint8_t type;
            bool close;
        };
        struct Scan {
            State state;
            std::vector<State> stack;
            Shadings *shadings;
            CGPDFOperatorTableRef table;
            size_t depth = 0;
            std::vector<PathPoint> points;     // The current path, in user space
            float startX = 0.f, startY = 0.f, x = 0.f, y = 0.f;    // Its subpath's start & current point
            uint8_t clipMode = 0;              // The FPDF_FILLMODE of a pending W or W*, applied at the next painting operator
            int container = -1;                // The form index of the content stream, or -1 for the page's
            int entryClip = -1;                // The clip at the stream's form's Do, whose paths aren't the stream's own
            Ra::Transform baseInverse;         // Of the ctm at the stream's form's Do, so pdfium's space for its objects
            Ra::Transform tm, tlm;             // The text matrix & text line matrix
            // The glyphs of a text object's runs in clipping text rendering modes, in page space, which clip at ET, as pdfium's
            // do, if there are at most kMaxClipTexts, & the scan has their glyphs
            static constexpr size_t kMaxClipTexts = 1024;
            Ra::Path textClip = nullptr;
            size_t clipTexts = 0;
            bool hasClipGlyphs = true;
        };
        void read(const char *filename, size_t pageIndex) {
            CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8 *)filename, strlen(filename), false);
            CGPDFDocumentRef doc = url ? CGPDFDocumentCreateWithURL(url) : nullptr;
            CGPDFPageRef page = doc ? CGPDFDocumentGetPage(doc, pageIndex + 1) : nullptr;
            if (page) {
                CGPDFContentStreamRef cs = CGPDFContentStreamCreateWithPage(page);
                CGPDFOperatorTableRef table = createTable();
                scan(cs, table, State(), 0);
                CGPDFOperatorTableRelease(table);
                CGPDFContentStreamRelease(cs);
            }
            CGPDFDocumentRelease(doc);
            if (url)
                CFRelease(url);
        }
        void scan(CGPDFContentStreamRef cs, CGPDFOperatorTableRef table, State state, size_t depth, int container = -1, Ra::Transform base = Ra::Transform(), int entryClip = -1) {
            Scan scan;  scan.shadings = this, scan.table = table, scan.state = state, scan.depth = depth, scan.container = container, scan.baseInverse = base.invert(), scan.entryClip = entryClip;
            if (streams.size() < size_t(container + 2))
                streams.resize(container + 2);
            CGPDFScannerRef scanner = CGPDFScannerCreate(cs, table, & scan);
            CGPDFScannerScan(scanner);
            CGPDFScannerRelease(scanner);
        }
        static void addObject(Scan& scan, uint8_t kind, size_t index = 0) {
            scan.shadings->streams[scan.container + 1].emplace_back(Object{ kind, uint32_t(index), scan.state.clip });
        }
        // An image object, whose image is decoded once if it's an XObject's
        static void addImage(Scan& scan, CGPDFStreamRef stream, CGPDFContentStreamRef cs, bool isXObject) {
            Shadings& s = *scan.shadings;
            ImageObject object;
            object.ctm = scan.state.ctm, object.alpha = scan.state.alpha, object.mask = scan.state.mask, object.blend = scan.state.blend;
            object.colors = objectColors(scan.state);
            if (s.readsImages && stream) {
                auto it = isXObject ? s.imageIndices.find(stream) : s.imageIndices.end();
                if (it != s.imageIndices.end())
                    object.image = it->second;
                else {
                    s.images.emplace_back(stream, cs);
                    if (s.images.back().isValid)
                        object.image = int(s.images.size() - 1);
                    else
                        s.images.pop_back();
                    if (isXObject)
                        s.imageIndices[stream] = object.image;
                }
            }
            addObject(scan, kImageObject, s.imageObjects.size());
            s.imageObjects.emplace_back(object);
        }
        // An image's paint, with opacity, an image mask's color, & a soft mask, in masks, for the image drawn at ctm, cached as a
        // paint copies its pixels
        Ra::Paint imagePaint(int index, float opacity, Ra::Color color, int mask, Ra::Transform ctm) {
            const PDFImage& image = images[index];
            std::array<float, 6> m = {};
            if (mask >= 0)
                m = { ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx, ctm.ty };
            auto key = std::make_tuple(index, opacity, image.isMask ? uint32_t(color.r << 16 | color.g << 8 | color.b) : 0u, mask, m);
            auto it = imagePaints.find(key);
            if (it != imagePaints.end())
                return it->second;
            std::vector<Ra::Color> pixels = image.pixels;
            size_t width = image.width, height = image.height;
            if (image.isMask)
                for (auto& p : pixels)
                    p = Ra::Color(color.b, color.g, color.r, p.a);
            if (mask >= 0) {
                // The mask is sampled at the pixels' centers, so a small image is enlarged to 2 pixels per unit, as a mask's
                // alpha is smooth
                size_t w = std::max(width, size_t(fminf(kMaxMaskSize, ceilf(2.f * hypotf(ctm.a, ctm.b))))), h = std::max(height, size_t(fminf(kMaxMaskSize, ceilf(2.f * hypotf(ctm.c, ctm.d)))));
                if (w != width || h != height) {
                    std::vector<Ra::Color> enlarged(w * h);
                    for (size_t y = 0; y < h; y++)
                        for (size_t x = 0; x < w; x++)
                            enlarged[y * w + x] = pixels[(y * height / h) * width + x * width / w];
                    pixels.swap(enlarged), width = w, height = h;
                }
                applyMask(pixels.data(), width, height, width * sizeof(Ra::Color), ctm, masks[mask]);
            }
            premultiply(pixels.data(), width, height, width * sizeof(Ra::Color), opacity);
            return imagePaints[key] = Ra::Paint(pixels.data(), width, height, width * sizeof(Ra::Color));
        }
        // The clip of parent & path, in page space. pdfium drops the stream's last path if it's a rect that contains a new path,
        // but not text, & a clip to nothing, from a path of one point or no area, clips everything out
        int addClip(int parent, Ra::Path path, bool isEntry, bool isText = false) {
            Clip clip;
            if (parent >= 0)
                clip = clips[parent];
            size_t own = isEntry ? 0 : clip.own;
            clip.inherited = isEntry ? parent : clip.inherited;
            path = clipPaths.emplace(path->hash(), path).first->second;
            if (!isText && own > 0 && clip.paths[own - 1]->isRect() && clip.paths[own - 1]->bounds.contains(path->bounds))
                clip.paths.erase(clip.paths.begin() + --own);
            clip.paths.insert(clip.paths.begin() + own, path), clip.own = own + 1;
            clip.bounds = Ra::Bounds::huge(), clip.sorted.resize(0);
            for (auto& p : clip.paths)
                if (p->isValid())
                    clip.bounds = clip.bounds.intersect(p->bounds), clip.sorted.emplace_back(p);
                else
                    clip.bounds = clip.bounds.intersect(Ra::Bounds(0.f, 0.f, 0.f, 0.f));
            std::stable_partition(clip.sorted.begin(), clip.sorted.end(), [](Ra::Path& p) { return !p->isRect(); });
            clips.emplace_back(std::move(clip));
            return int(clips.size() - 1);
        }
        static CGPDFOperatorTableRef createTable() {
            CGPDFOperatorTableRef table = CGPDFOperatorTableCreate();
            CGPDFOperatorTableSetCallback(table, "q", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;
                scan.stack.emplace_back(scan.state);
            });
            CGPDFOperatorTableSetCallback(table, "Q", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;
                if (scan.stack.size())
                    scan.state = scan.stack.back(), scan.stack.pop_back();
            });
            CGPDFOperatorTableSetCallback(table, "cm", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  CGPDFReal m[6];
                for (int i = 5; i >= 0; i--)
                    if (!CGPDFScannerPopNumber(scanner, & m[i]))
                        return;
                scan.state.ctm = Ra::Transform(m[0], m[1], m[2], m[3], m[4], m[5]).concat(scan.state.ctm);
            });
            CGPDFOperatorTableSetCallback(table, "gs", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  const char *name;  CGPDFDictionaryRef dict, smask;  CGPDFReal ca;
                if (!CGPDFScannerPopName(scanner, & name))
                    return;
                CGPDFContentStreamRef cs = CGPDFScannerGetContentStream(scanner);
                CGPDFObjectRef obj = CGPDFContentStreamGetResource(cs, "ExtGState", name);
                if (obj == nullptr || !CGPDFObjectGetValue(obj, kCGPDFObjectTypeDictionary, & dict))
                    return;
                if (CGPDFDictionaryGetNumber(dict, "ca", & ca))
                    scan.state.alpha = fmaxf(0.f, fminf(1.f, ca));
                if (CGPDFDictionaryGetNumber(dict, "CA", & ca))
                    scan.state.strokeAlpha = fmaxf(0.f, fminf(1.f, ca));
                CGPDFReal lw;  CGPDFInteger lc, lj;  CGPDFArrayRef d, dash;
                if (CGPDFDictionaryGetNumber(dict, "LW", & lw))
                    scan.state.lineWidth = lw;
                if (CGPDFDictionaryGetInteger(dict, "LC", & lc))
                    scan.state.cap = uint8_t(lc);
                if (CGPDFDictionaryGetInteger(dict, "LJ", & lj))
                    scan.state.join = uint8_t(lj);
                if (CGPDFDictionaryGetArray(dict, "D", & d) && CGPDFArrayGetArray(d, 0, & dash) && CGPDFArrayGetNumber(d, 1, & lw))
                    setDash(scan, dash, lw);
                CGPDFObjectRef bm;
                if (CGPDFDictionaryGetObject(dict, "BM", & bm))
                    scan.state.blend = readBlendMode(bm);
                if (CGPDFDictionaryGetName(dict, "SMask", & name))
                    scan.state.mask = kNoMask;
                else if (CGPDFDictionaryGetDictionary(dict, "SMask", & smask))
                    scan.state.mask = scan.shadings->readMask(smask, cs, scan.state.ctm, scan.depth);
            });
            CGPDFOperatorTableSetCallback(table, "sh", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  const char *name;  CGPDFDictionaryRef dict;
                if (!CGPDFScannerPopName(scanner, & name))
                    return;
                CGPDFContentStreamRef cs = CGPDFScannerGetContentStream(scanner);
                CGPDFObjectRef obj = CGPDFContentStreamGetResource(cs, "Shading", name);
                if (obj == nullptr)     // pdfium makes no object for it
                    return;
                addObject(scan, kShadingObject, scan.shadings->shadings.size());
                scan.shadings->shadings.emplace_back();
                Shading& shading = scan.shadings->shadings.back();
                shading.ctm = scan.state.ctm, shading.alpha = scan.state.alpha, shading.mask = scan.state.mask, shading.blend = scan.state.blend;
                shading.isValid = obj && CGPDFObjectGetValue(obj, kCGPDFObjectTypeDictionary, & dict) && shading.read(dict, cs);
            });
            CGPDFOperatorTableSetCallback(table, "Do", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  const char *name;  CGPDFStreamRef stream;  CGPDFDictionaryRef dict, resources = nullptr;
                std::vector<float> m;
                if (!CGPDFScannerPopName(scanner, & name))
                    return;
                CGPDFContentStreamRef cs = CGPDFScannerGetContentStream(scanner);
                CGPDFObjectRef obj = CGPDFContentStreamGetResource(cs, "XObject", name);
                if (obj == nullptr || !CGPDFObjectGetValue(obj, kCGPDFObjectTypeStream, & stream)
                    || !CGPDFDictionaryGetName(dict = CGPDFStreamGetDictionary(stream), "Subtype", & name))
                    return;
                if (!strcmp(name, "Image"))
                    return addImage(scan, stream, cs, true);
                if (strcmp(name, "Form"))
                    return;
                // A transparency group's soft mask, opacity & blend mode apply to the group, so its contents start without them. Other
                // forms' contents inherit them, as pdfium's objects in them do
                CGPDFDictionaryRef groupDict;
                bool isGroup = CGPDFDictionaryGetDictionary(dict, "Group", & groupDict) && CGPDFDictionaryGetName(groupDict, "S", & name) && !strcmp(name, "Transparency");
                Group group;
                group.ctm = scan.state.ctm;
                if (isGroup)
                    group.mask = scan.state.mask, group.alpha = scan.state.alpha, group.blend = scan.state.blend;
                int container = int(scan.shadings->forms.size());
                addObject(scan, kFormObject, container);
                // Its contents are clipped to its BBox, in form space, as pdfium's are
                State state = scan.state;
                if (readNumbers(dict, "Matrix", m) && m.size() == 6)
                    state.ctm = Ra::Transform(m[0], m[1], m[2], m[3], m[4], m[5]).concat(state.ctm);
                if (readNumbers(dict, "BBox", m) && m.size() == 4) {
                    Ra::Path bbox;  bbox->addBounds(Ra::Bounds(fminf(m[0], m[2]), fminf(m[1], m[3]), fmaxf(m[0], m[2]), fmaxf(m[1], m[3])));
                    state.clip = scan.shadings->addClip(state.clip, transformedPath(bbox, state.ctm), true);
                }
                group.clip = state.clip;
                scan.shadings->forms.emplace_back(group);
                if (scan.depth < kMaxDepth) {
                    if (isGroup)
                        state.alpha = 1.f, state.mask = kNoMask, state.blend = kBlendNormal;
                    state.space = state.ctm;
                    CGPDFDictionaryGetDictionary(dict, "Resources", & resources);
                    CGPDFContentStreamRef form = CGPDFContentStreamCreateWithStream(stream, resources, cs);
                    scan.shadings->scan(form, scan.table, state, scan.depth + 1, container, scan.state.ctm, scan.state.clip);
                    CGPDFContentStreamRelease(form);
                }
            });
            CGPDFOperatorTableSetCallback(table, "EI", [](CGPDFScannerRef scanner, void *info) {      // An inline image
                CGPDFStreamRef stream = nullptr;
                CGPDFScannerPopStream(scanner, & stream);
                addImage(*(Scan *)info, stream, CGPDFScannerGetContentStream(scanner), false);
            });
            addPathCallbacks(table);
            addTextCallbacks(table);
            return table;
        }
        // Tracks the text state, & records each text showing operator's glyphs as a text run
        static void addTextCallbacks(CGPDFOperatorTableRef table) {
            CGPDFOperatorTableSetCallback(table, "BT", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;
                scan.tm = scan.tlm = Ra::Transform();
                scan.textClip = nullptr, scan.clipTexts = 0, scan.hasClipGlyphs = true;
            });
            CGPDFOperatorTableSetCallback(table, "ET", [](CGPDFScannerRef scanner, void *info) {     // pdfium clips if the mode still clips
                Scan& scan = *(Scan *)info;  State& st = scan.state;
                if (scan.clipTexts && st.textMode >= 4 && st.textMode <= 7 && scan.clipTexts <= Scan::kMaxClipTexts && scan.hasClipGlyphs)
                    st.clip = scan.shadings->addClip(st.clip, scan.textClip.ptr ? scan.textClip : Ra::Path(), st.clip == scan.entryClip, true);
                scan.textClip = nullptr, scan.clipTexts = 0, scan.hasClipGlyphs = true;
            });
            CGPDFOperatorTableSetCallback(table, "Tr", [](CGPDFScannerRef scanner, void *info) {
                CGPDFInteger mode;
                if (CGPDFScannerPopInteger(scanner, & mode) && mode >= 0 && mode <= 7)
                    ((Scan *)info)->state.textMode = uint8_t(mode);
            });
            CGPDFOperatorTableSetCallback(table, "Tf", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  CGPDFReal size;  const char *name;  CGPDFDictionaryRef dict;
                if (!CGPDFScannerPopNumber(scanner, & size) || !CGPDFScannerPopName(scanner, & name))
                    return;
                CGPDFObjectRef obj = CGPDFContentStreamGetResource(CGPDFScannerGetContentStream(scanner), "Font", name);
                scan.state.fontSize = size, scan.state.hasFont = true;
                scan.state.font = obj && CGPDFObjectGetValue(obj, kCGPDFObjectTypeDictionary, & dict) ? scan.shadings->fontFor(dict) : nullptr;
            });
            CGPDFOperatorTableSetCallback(table, "Tc", [](CGPDFScannerRef scanner, void *info) { popNumber(scanner, ((Scan *)info)->state.charSpace); });
            CGPDFOperatorTableSetCallback(table, "Tw", [](CGPDFScannerRef scanner, void *info) { popNumber(scanner, ((Scan *)info)->state.wordSpace); });
            CGPDFOperatorTableSetCallback(table, "TL", [](CGPDFScannerRef scanner, void *info) { popNumber(scanner, ((Scan *)info)->state.leading); });
            CGPDFOperatorTableSetCallback(table, "Ts", [](CGPDFScannerRef scanner, void *info) { popNumber(scanner, ((Scan *)info)->state.rise); });
            CGPDFOperatorTableSetCallback(table, "Tz", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  float scale;
                if (popNumber(scanner, scale))
                    scan.state.hScale = scale / 100.f;
            });
            CGPDFOperatorTableSetCallback(table, "Td", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  CGPDFReal tx, ty;
                if (CGPDFScannerPopNumber(scanner, & ty) && CGPDFScannerPopNumber(scanner, & tx))
                    moveLine(scan, tx, ty);
            });
            CGPDFOperatorTableSetCallback(table, "TD", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  CGPDFReal tx, ty;
                if (CGPDFScannerPopNumber(scanner, & ty) && CGPDFScannerPopNumber(scanner, & tx))
                    scan.state.leading = -ty, moveLine(scan, tx, ty);
            });
            CGPDFOperatorTableSetCallback(table, "Tm", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  CGPDFReal m[6];
                for (int i = 5; i >= 0; i--)
                    if (!CGPDFScannerPopNumber(scanner, & m[i]))
                        return;
                scan.tm = scan.tlm = Ra::Transform(m[0], m[1], m[2], m[3], m[4], m[5]);
            });
            CGPDFOperatorTableSetCallback(table, "T*", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;
                moveLine(scan, 0.f, -scan.state.leading);
            });
            CGPDFOperatorTableSetCallback(table, "Tj", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  CGPDFStringRef string;
                if (CGPDFScannerPopString(scanner, & string))
                    showText(scan, & string, nullptr);
            });
            CGPDFOperatorTableSetCallback(table, "'", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  CGPDFStringRef string;
                if (CGPDFScannerPopString(scanner, & string))
                    moveLine(scan, 0.f, -scan.state.leading), showText(scan, & string, nullptr);
            });
            CGPDFOperatorTableSetCallback(table, "\"", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  CGPDFStringRef string;  CGPDFReal ac, aw;
                if (CGPDFScannerPopString(scanner, & string) && CGPDFScannerPopNumber(scanner, & ac) && CGPDFScannerPopNumber(scanner, & aw))
                    scan.state.wordSpace = aw, scan.state.charSpace = ac, moveLine(scan, 0.f, -scan.state.leading), showText(scan, & string, nullptr);
            });
            CGPDFOperatorTableSetCallback(table, "TJ", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  CGPDFArrayRef array;
                if (CGPDFScannerPopArray(scanner, & array))
                    showText(scan, nullptr, array);
            });
        }
        static bool popNumber(CGPDFScannerRef scanner, float& value) {
            CGPDFReal number;
            return CGPDFScannerPopNumber(scanner, & number) ? (value = number, true) : false;
        }
        static void moveLine(Scan& scan, float tx, float ty) {
            scan.tm = scan.tlm = Ra::Transform(1.f, 0.f, 0.f, 1.f, tx, ty).concat(scan.tlm);
        }
        // Records a text run of a string, or a TJ array of strings & adjustments, advancing the text matrix. pdfium makes no text
        // object before a Tf, or for an empty Tj string, or a TJ array without strings
        static void showText(Scan& scan, CGPDFStringRef *string, CGPDFArrayRef array) {
            Shadings& s = *scan.shadings;  State& st = scan.state;  CGPDFStringRef str;
            bool hasStrings = string && CGPDFStringGetLength(*string) > 0;
            for (size_t i = 0; array && !hasStrings && i < CGPDFArrayGetCount(array); i++)
                hasStrings = CGPDFArrayGetString(array, i, & str);
            if (array && !hasStrings)       // But it applies the adjustments
                for (size_t i = 0; i < CGPDFArrayGetCount(array); i++) {
                    CGPDFReal adjust;
                    if (CGPDFArrayGetNumber(array, i, & adjust))
                        scan.tm = Ra::Transform(1.f, 0.f, 0.f, 1.f, -adjust / 1000.f * st.fontSize * st.hScale, 0.f).concat(scan.tm);
                }
            if (!st.hasFont || !hasStrings)
                return;
            addObject(scan, kTextObject, s.texts.size());
            s.texts.emplace_back();
            TextRun& run = s.texts.back();
            run.mode = st.textMode, run.colors = objectColors(st);
            bool clips = st.textMode >= 4 && st.textMode <= 7;
            scan.clipTexts += clips;
            TextFont *font = st.font;
            if (font == nullptr || !font->isValid) {
                run.isValid = false, scan.hasClipGlyphs = scan.hasClipGlyphs && !clips;
                return;
            }
            Ra::Transform size(st.fontSize * st.hScale, 0.f, 0.f, st.fontSize, 0.f, st.rise);
            run.unitsPerEm = sqrtf(fabsf(size.concat(scan.tm).det())), run.width = st.lineWidth;
            auto show = [&](CGPDFStringRef str) {
                const uint8_t *bytes = CGPDFStringGetBytePtr(str);  size_t length = CGPDFStringGetLength(str), step = font->isType0 ? 2 : 1;
                for (size_t i = 0; i + step <= length; i += step) {
                    uint32_t code = step == 2 ? bytes[i] << 8 | bytes[i + 1] : bytes[i];
                    CGGlyph g = font->glyph(code);
                    Ra::Path path = font->path(g);
                    if (path->types.end) {
                        run.glyphs.push_back({ path, size.concat(scan.tm).concat(st.ctm) });
                        if (clips)
                            scan.textClip = transformedPath(path, run.glyphs.back().ctm, scan.textClip.ptr ? scan.textClip : Ra::Path());
                    }
                    float tx = (font->width(code, g) / 1000.f * st.fontSize + st.charSpace + (step == 1 && code == 32 ? st.wordSpace : 0.f)) * st.hScale;
                    scan.tm = Ra::Transform(1.f, 0.f, 0.f, 1.f, tx, 0.f).concat(scan.tm);
                }
            };
            if (string)
                show(*string);
            else
                for (size_t i = 0; i < CGPDFArrayGetCount(array); i++) {
                    CGPDFReal adjust;
                    if (CGPDFArrayGetString(array, i, & str))
                        show(str);
                    else if (CGPDFArrayGetNumber(array, i, & adjust))
                        scan.tm = Ra::Transform(1.f, 0.f, 0.f, 1.f, -adjust / 1000.f * st.fontSize * st.hScale, 0.f).concat(scan.tm);
                }
        }
        // Tracks the fill color space & pattern, & records a path fill for each path painting operator
        static void addPathCallbacks(CGPDFOperatorTableRef table) {
            // Fill colors, & their pattern, & stroke colors
            CGPDFOperatorTableSetCallback(table, "cs", [](CGPDFScannerRef scanner, void *info) { setColorSpace(scanner, *(Scan *)info, false); });
            CGPDFOperatorTableSetCallback(table, "CS", [](CGPDFScannerRef scanner, void *info) { setColorSpace(scanner, *(Scan *)info, true); });
            for (const char *op : { "sc", "scn" })
                CGPDFOperatorTableSetCallback(table, op, [](CGPDFScannerRef scanner, void *info) { setColor(scanner, *(Scan *)info, false); });
            for (const char *op : { "SC", "SCN" })
                CGPDFOperatorTableSetCallback(table, op, [](CGPDFScannerRef scanner, void *info) { setColor(scanner, *(Scan *)info, true); });
            CGPDFOperatorTableSetCallback(table, "g", [](CGPDFScannerRef scanner, void *info) { setDeviceColor(scanner, *(Scan *)info, false, nullptr); });
            CGPDFOperatorTableSetCallback(table, "G", [](CGPDFScannerRef scanner, void *info) { setDeviceColor(scanner, *(Scan *)info, true, nullptr); });
            CGPDFOperatorTableSetCallback(table, "rg", [](CGPDFScannerRef scanner, void *info) { setDeviceColor(scanner, *(Scan *)info, false, & ((Scan *)info)->shadings->rgbSpace); });
            CGPDFOperatorTableSetCallback(table, "RG", [](CGPDFScannerRef scanner, void *info) { setDeviceColor(scanner, *(Scan *)info, true, & ((Scan *)info)->shadings->rgbSpace); });
            CGPDFOperatorTableSetCallback(table, "k", [](CGPDFScannerRef scanner, void *info) { setDeviceColor(scanner, *(Scan *)info, false, & ((Scan *)info)->shadings->cmykSpace); });
            CGPDFOperatorTableSetCallback(table, "K", [](CGPDFScannerRef scanner, void *info) { setDeviceColor(scanner, *(Scan *)info, true, & ((Scan *)info)->shadings->cmykSpace); });
            CGPDFOperatorTableSetCallback(table, "m", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  float p[2];
                if (popNumbers(scanner, p, 2))
                    addPoint(scan, p[0], p[1], Ra::Geometry::kMove);
            });
            CGPDFOperatorTableSetCallback(table, "l", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  float p[2];
                if (popNumbers(scanner, p, 2))
                    addPoint(scan, p[0], p[1], Ra::Geometry::kLine);
            });
            CGPDFOperatorTableSetCallback(table, "c", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  float p[6];
                if (popNumbers(scanner, p, 6))
                    for (int i = 0; i < 6; i += 2)
                        addPoint(scan, p[i], p[i + 1], Ra::Geometry::kCubic);
            });
            CGPDFOperatorTableSetCallback(table, "v", [](CGPDFScannerRef scanner, void *info) {      // The first control point is the current point
                Scan& scan = *(Scan *)info;  float p[4];
                if (popNumbers(scanner, p, 4))
                    addPoint(scan, scan.x, scan.y, Ra::Geometry::kCubic), addPoint(scan, p[0], p[1], Ra::Geometry::kCubic), addPoint(scan, p[2], p[3], Ra::Geometry::kCubic);
            });
            CGPDFOperatorTableSetCallback(table, "y", [](CGPDFScannerRef scanner, void *info) {      // The second is the end point
                Scan& scan = *(Scan *)info;  float p[4];
                if (popNumbers(scanner, p, 4))
                    addPoint(scan, p[0], p[1], Ra::Geometry::kCubic), addPoint(scan, p[2], p[3], Ra::Geometry::kCubic), addPoint(scan, p[2], p[3], Ra::Geometry::kCubic);
            });
            CGPDFOperatorTableSetCallback(table, "re", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  float p[4];
                if (popNumbers(scanner, p, 4)) {
                    float x = p[0], y = p[1], w = p[2], h = p[3];
                    addPoint(scan, x, y, Ra::Geometry::kMove), addPoint(scan, x + w, y, Ra::Geometry::kLine);
                    addPoint(scan, x + w, y + h, Ra::Geometry::kLine), addPoint(scan, x, y + h, Ra::Geometry::kLine), closePath(scan, x, y);
                }
            });
            CGPDFOperatorTableSetCallback(table, "h", [](CGPDFScannerRef scanner, void *info) { closeSubpath(*(Scan *)info); });
            CGPDFOperatorTableSetCallback(table, "W", [](CGPDFScannerRef scanner, void *info) { ((Scan *)info)->clipMode = FPDF_FILLMODE_WINDING; });
            CGPDFOperatorTableSetCallback(table, "W*", [](CGPDFScannerRef scanner, void *info) { ((Scan *)info)->clipMode = FPDF_FILLMODE_ALTERNATE; });
            CGPDFOperatorTableSetCallback(table, "n", [](CGPDFScannerRef scanner, void *info) { addPath(*(Scan *)info, FPDF_FILLMODE_NONE); });
            CGPDFOperatorTableSetCallback(table, "f", [](CGPDFScannerRef scanner, void *info) { addPath(*(Scan *)info, FPDF_FILLMODE_WINDING); });
            CGPDFOperatorTableSetCallback(table, "F", [](CGPDFScannerRef scanner, void *info) { addPath(*(Scan *)info, FPDF_FILLMODE_WINDING); });
            CGPDFOperatorTableSetCallback(table, "f*", [](CGPDFScannerRef scanner, void *info) { addPath(*(Scan *)info, FPDF_FILLMODE_ALTERNATE); });
            CGPDFOperatorTableSetCallback(table, "B", [](CGPDFScannerRef scanner, void *info) { addPath(*(Scan *)info, FPDF_FILLMODE_WINDING | kStroke); });
            CGPDFOperatorTableSetCallback(table, "B*", [](CGPDFScannerRef scanner, void *info) { addPath(*(Scan *)info, FPDF_FILLMODE_ALTERNATE | kStroke); });
            CGPDFOperatorTableSetCallback(table, "S", [](CGPDFScannerRef scanner, void *info) { addPath(*(Scan *)info, kStroke); });
            CGPDFOperatorTableSetCallback(table, "b", [](CGPDFScannerRef scanner, void *info) { closeSubpath(*(Scan *)info), addPath(*(Scan *)info, FPDF_FILLMODE_WINDING | kStroke); });
            CGPDFOperatorTableSetCallback(table, "b*", [](CGPDFScannerRef scanner, void *info) { closeSubpath(*(Scan *)info), addPath(*(Scan *)info, FPDF_FILLMODE_ALTERNATE | kStroke); });
            CGPDFOperatorTableSetCallback(table, "s", [](CGPDFScannerRef scanner, void *info) { closeSubpath(*(Scan *)info), addPath(*(Scan *)info, kStroke); });
            
            CGPDFOperatorTableSetCallback(table, "w", [](CGPDFScannerRef scanner, void *info) { popNumber(scanner, ((Scan *)info)->state.lineWidth); });
            CGPDFOperatorTableSetCallback(table, "J", [](CGPDFScannerRef scanner, void *info) {
                CGPDFInteger cap;
                if (CGPDFScannerPopInteger(scanner, & cap))
                    ((Scan *)info)->state.cap = uint8_t(cap);
            });
            CGPDFOperatorTableSetCallback(table, "j", [](CGPDFScannerRef scanner, void *info) {
                CGPDFInteger join;
                if (CGPDFScannerPopInteger(scanner, & join))
                    ((Scan *)info)->state.join = uint8_t(join);
            });
            CGPDFOperatorTableSetCallback(table, "d", [](CGPDFScannerRef scanner, void *info) {
                CGPDFReal phase;  CGPDFArrayRef array;
                if (CGPDFScannerPopNumber(scanner, & phase) && CGPDFScannerPopArray(scanner, & array))
                    setDash(*(Scan *)info, array, phase);
            });
        }
        static bool popNumbers(CGPDFScannerRef scanner, float *numbers, int count) {
            CGPDFReal number;
            for (int i = count - 1; i >= 0; i--)
                if (CGPDFScannerPopNumber(scanner, & number))
                    numbers[i] = number;
                else
                    return false;
            return true;
        }
        // The color space of a name, of a device space or a resource, or of a resource's object, cached by its object
        const ColorSpace *colorSpace(const char *name, CGPDFContentStreamRef cs, int depth = 0) {
            if (!strcmp(name, "DeviceGray") || !strcmp(name, "G"))
                return & graySpace;
            if (!strcmp(name, "DeviceRGB") || !strcmp(name, "RGB"))
                return & rgbSpace;
            if (!strcmp(name, "DeviceCMYK") || !strcmp(name, "CMYK"))
                return & cmykSpace;
            if (!strcmp(name, "Pattern"))
                return & patternSpace;
            CGPDFObjectRef obj = depth < 4 ? CGPDFContentStreamGetResource(cs, "ColorSpace", name) : nullptr;
            return obj ? colorSpace(obj, cs, depth + 1) : & unsupportedSpace;
        }
        const ColorSpace *colorSpace(CGPDFObjectRef obj, CGPDFContentStreamRef cs, int depth) {
            const char *name;  CGPDFArrayRef array, names = nullptr;  CGPDFObjectRef object;  CGPDFStringRef string;  CGPDFStreamRef stream;
            CGPDFInteger hival;  CGPDFDataFormat format;
            if (CGPDFObjectGetValue(obj, kCGPDFObjectTypeName, & name))
                return colorSpace(name, cs, depth);
            auto it = colorSpaces.find(obj);
            if (it != colorSpaces.end())
                return it->second.get();
            ColorSpace *space = new ColorSpace();
            colorSpaces.emplace(obj, std::unique_ptr<ColorSpace>(space));
            if (depth > 4 || !CGPDFObjectGetValue(obj, kCGPDFObjectTypeArray, & array) || !CGPDFArrayGetName(array, 0, & name))
                return space;
            CGPDFDictionaryRef dict;
            if (!strcmp(name, "ICCBased")) {
                if ((space->space = PDFImage::createColorSpace(obj, cs, nullptr)))
                    space->family = ColorSpace::kOther, space->components = CGColorSpaceGetNumberOfComponents(space->space);
            } else if (!strcmp(name, "CalGray"))
                space->family = ColorSpace::kGray;      // pdfium ignores its gamma
            else if ((!strcmp(name, "CalRGB") || !strcmp(name, "Lab")) && CGPDFArrayGetDictionary(array, 1, & dict)) {
                std::vector<float> white, gamma, matrix, range;
                bool isLab = !strcmp(name, "Lab");
                space->family = isLab ? ColorSpace::kLab : ColorSpace::kCalRGB, space->components = 3;
                if (readNumbers(dict, "WhitePoint", white) && white.size() == 3)
                    std::copy(white.begin(), white.end(), space->white);
                if (readNumbers(dict, "Gamma", gamma) && gamma.size() == 3)
                    std::copy(gamma.begin(), gamma.end(), space->gamma);
                if (readNumbers(dict, "Matrix", matrix) && matrix.size() == 9)
                    std::copy(matrix.begin(), matrix.end(), space->matrix), space->hasMatrix = true;
                if (readNumbers(dict, "Range", range) && range.size() == 4)
                    std::copy(range.begin(), range.end(), space->range);
            } else if ((!strcmp(name, "Indexed") || !strcmp(name, "I")) && CGPDFArrayGetObject(array, 1, & object) && CGPDFArrayGetInteger(array, 2, & hival) && hival >= 0 && hival < 256) {
                const ColorSpace *base = colorSpace(object, cs, depth + 1);
                CFDataRef data = nullptr;
                if (CGPDFArrayGetString(array, 3, & string))
                    space->lookup.assign(CGPDFStringGetBytePtr(string), CGPDFStringGetBytePtr(string) + CGPDFStringGetLength(string));
                else if (CGPDFArrayGetStream(array, 3, & stream) && (data = CGPDFStreamCopyData(stream, & format)))
                    space->lookup.assign(CFDataGetBytePtr(data), CFDataGetBytePtr(data) + CFDataGetLength(data)), CFRelease(data);
                if (base->family <= ColorSpace::kOther)
                    space->family = ColorSpace::kIndexed, space->base = base, space->hival = size_t(hival);
            } else if ((!strcmp(name, "Separation") || (!strcmp(name, "DeviceN") && CGPDFArrayGetArray(array, 1, & names)))
                && CGPDFArrayGetObject(array, 2, & object) && CGPDFArrayGetObject(array, 3, & obj) && space->tint.read(obj)) {
                const ColorSpace *base = colorSpace(object, cs, depth + 1);
                size_t n = !strcmp(name, "Separation") ? 1 : CGPDFArrayGetCount(names);
                if (base->family <= ColorSpace::kOther && space->tint.outputs == base->components && space->tint.inputs == n)
                    space->family = ColorSpace::kTint, space->base = base, space->components = n;
            } else if (!strcmp(name, "Pattern"))
                return & patternSpace;
            return space;
        }
        // pdfium keeps the color until sc or scn, & doesn't set the space's initial color
        static void setColorSpace(CGPDFScannerRef scanner, Scan& scan, bool isStroke) {
            State& st = scan.state;  const char *name;
            if (!isStroke)
                st.isPatternSpace = false, st.pattern = -1;
            if (!CGPDFScannerPopName(scanner, & name))
                return;
            const ColorSpace *space = scan.shadings->colorSpace(name, CGPDFScannerGetContentStream(scanner));
            (isStroke ? st.strokeSpace : st.fillSpace) = space;
            if (!isStroke)
                st.isPatternSpace = space->family == ColorSpace::kPattern;
        }
        // sc, scn, SC or SCN: components, & for scn, a pattern's name
        static void setColor(CGPDFScannerRef scanner, Scan& scan, bool isStroke) {
            State& st = scan.state;  CGPDFObjectRef obj;  CGPDFReal number;  const char *name = nullptr;
            std::vector<float> components;
            while (components.size() < 32 && CGPDFScannerPopObject(scanner, & obj))
                if (CGPDFObjectGetValue(obj, kCGPDFObjectTypeReal, & number))
                    components.emplace_back(number);
                else if (!CGPDFObjectGetValue(obj, kCGPDFObjectTypeName, & name))
                    break;
            std::reverse(components.begin(), components.end());
            if (!isStroke && st.isPatternSpace && name) {
                CGPDFContentStreamRef cs = CGPDFScannerGetContentStream(scanner);
                CGPDFObjectRef pattern = CGPDFContentStreamGetResource(cs, "Pattern", name);
                std::vector<Shading>& patterns = scan.shadings->patterns;
                patterns.emplace_back();
                Shading& shading = patterns.back();
                shading.isValid = pattern && readPattern(pattern, cs, shading);
                shading.ctm = shading.ctm.concat(st.space);
                st.pattern = int(patterns.size() - 1);
            }
            setColor(scan, isStroke, components.data(), components.size());
        }
        static void setDeviceColor(CGPDFScannerRef scanner, Scan& scan, bool isStroke, const ColorSpace *space) {
            float c[4];
            size_t n = space ? space->components : 1;
            if (!isStroke)
                scan.state.isPatternSpace = false, scan.state.pattern = -1;
            if (popNumbers(scanner, c, int(n)))
                (isStroke ? scan.state.strokeSpace : scan.state.fillSpace) = space, setColor(scan, isStroke, c, n);
        }
        static void setColor(Scan& scan, bool isStroke, const float *c, size_t n) {
            Colors& colors = scan.state.colors;
            const ColorSpace *space = isStroke ? scan.state.strokeSpace : scan.state.fillSpace;
            space = space ? space : & scan.shadings->graySpace;
            (isStroke ? colors.hasStroke : colors.hasFill) = space->color(c, n, isStroke ? colors.stroke : colors.fill);
        }
        // The fill & stroke colors, with their alphas
        static Colors objectColors(const State& st) {
            Colors colors = st.colors;
            colors.fill.a = ColorSpace::quantize(st.alpha), colors.stroke.a = ColorSpace::quantize(st.strokeAlpha);
            return colors;
        }
        static void setDash(Scan& scan, CGPDFArrayRef array, float phase) {
            std::vector<float> dash;  CGPDFReal number;
            for (size_t i = 0; i < CGPDFArrayGetCount(array); i++)
                if (CGPDFArrayGetNumber(array, i, & number))
                    dash.emplace_back(number);
            std::vector<std::vector<float>>& dashes = scan.shadings->dashes;
            scan.state.dashPhase = phase, scan.state.dash = dash.empty() ? -1 : int(dashes.size());
            if (dash.size())
                dashes.emplace_back(dash);
        }
        // Adds a point as pdfium does: a move after a move replaces it, & a line or curve needs a current point
        static void addPoint(Scan& scan, float x, float y, uint8_t type) {
            std::vector<PathPoint>& points = scan.points;
            bool afterMove = points.size() && points.back().type == Ra::Geometry::kMove && !points.back().close;
            if (type == Ra::Geometry::kMove && afterMove && scan.x == x && scan.y == y)
                return;
            scan.x = x, scan.y = y;
            if (type == Ra::Geometry::kMove) {
                scan.startX = x, scan.startY = y;
                if (afterMove) {
                    points.back().x = x, points.back().y = y;
                    return;
                }
            } else if (points.empty())
                return;
            points.emplace_back(PathPoint{ x, y, type, false });
        }
        static void closePath(Scan& scan, float x, float y) {      // With a line to x, y
            scan.x = x, scan.y = y;
            if (scan.points.size())
                scan.points.emplace_back(PathPoint{ x, y, Ra::Geometry::kLine, true });
        }
        static void closeSubpath(Scan& scan) {
            if (scan.points.empty())
                return;
            if (scan.startX != scan.x || scan.startY != scan.y)
                closePath(scan, scan.startX, scan.startY);
            else
                scan.points.back().close = true;
        }
        // A painting operator makes a path object of a path of more than one point, & for a pending W or W*, clips to it
        static void addPath(Scan& scan, uint8_t mode) {
            Shadings& s = *scan.shadings;  State& st = scan.state;  std::vector<PathPoint>& points = scan.points;
            uint8_t clipMode = scan.clipMode;  scan.clipMode = 0;
            if (points.empty())
                return;
            bool isEntry = st.clip == scan.entryClip;
            if (points.size() == 1) {
                if (clipMode)
                    st.clip = s.addClip(st.clip, Ra::Path(), isEntry);
                return points.clear();
            }
            if (points.back().type == Ra::Geometry::kMove && !points.back().close)
                points.pop_back();
            Ra::Path path;  Ra::Bounds bounds;  float cubic[6];  int n = 0;
            path->prealloc(points.size());
            for (PathPoint& p : points) {
                bounds.extend(p.x, p.y);
                if (p.type == Ra::Geometry::kMove)
                    path->moveTo(p.x, p.y);
                else if (p.type == Ra::Geometry::kLine)
                    path->lineTo(p.x, p.y);
                else {
                    cubic[n++] = p.x, cubic[n++] = p.y;
                    if (n == 6)
                        path->cubicTo(cubic[0], cubic[1], cubic[2], cubic[3], cubic[4], cubic[5]), n = 0;
                }
                if (p.close)
                    path->close();
            }
            if (mode != FPDF_FILLMODE_NONE) {
                PathObject object;
                object.path = path, object.ctm = st.ctm, object.mode = mode;
                object.pattern = (mode & 3) && st.isPatternSpace ? st.pattern : -1, object.mask = st.mask, object.alpha = st.alpha, object.blend = st.blend;
                object.width = st.lineWidth, object.cap = st.cap, object.join = st.join, object.dash = st.dash, object.dashPhase = st.dashPhase;
                object.colors = objectColors(st);
                object.bounds = Ra::Bounds(bounds.quad(st.ctm.concat(scan.baseInverse)));
                addObject(scan, kPathObject, s.paths.size());
                s.paths.emplace_back(object);
            }
            if (clipMode)
                st.clip = s.addClip(st.clip, transformedPath(path, st.ctm), isEntry);
            points.clear();
        }
        // A /BM name, or the first known in an array of them
        static uint8_t readBlendMode(CGPDFObjectRef obj) {
            static const char *names[] = { "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "ColorDodge", "ColorBurn",
                "SoftLight", "HardLight", "Difference", "Exclusion", "Hue", "Saturation", "Color", "Luminosity" };
            const char *name;  CGPDFArrayRef array;
            if (CGPDFObjectGetValue(obj, kCGPDFObjectTypeName, & name)) {
                for (uint8_t i = 0; i < kBlendModeCount; i++)
                    if (!strcmp(name, names[i]))
                        return i;
            } else if (CGPDFObjectGetValue(obj, kCGPDFObjectTypeArray, & array))
                for (size_t j = 0; j < CGPDFArrayGetCount(array); j++)
                    if (CGPDFArrayGetName(array, j, & name))
                        for (uint8_t i = 0; i < kBlendModeCount; i++)
                            if (!strcmp(name, names[i]))
                                return i;
            return kBlendNormal;    // Including Compatible
        }
        // A shading pattern's shading, whose ctm is the pattern matrix, which maps to its content stream's default space
        static bool readPattern(CGPDFObjectRef obj, CGPDFContentStreamRef cs, Shading& shading) {
            CGPDFDictionaryRef dict, shadingDict, gs;  CGPDFInteger type;  std::vector<float> m;
            if (!CGPDFObjectGetValue(obj, kCGPDFObjectTypeDictionary, & dict) || !CGPDFDictionaryGetInteger(dict, "PatternType", & type) || type != 2
                || !CGPDFDictionaryGetDictionary(dict, "Shading", & shadingDict) || CGPDFDictionaryGetDictionary(dict, "ExtGState", & gs))
                return false;
            if (readNumbers(dict, "Matrix", m) && m.size() == 6)
                shading.ctm = Ra::Transform(m[0], m[1], m[2], m[3], m[4], m[5]);
            return shading.read(shadingDict, cs);
        }
        // A luminosity soft mask whose group paints one shading, with a pattern fill or sh, is the alpha of a gradient, e.g. from
        // Cairo. Its group's space is the ctm when the mask is set. A group with its own resources is scanned once, in group space,
        // & each mask & ctm is one entry in masks, as content often sets the same mask before each object
        int readMask(CGPDFDictionaryRef smask, CGPDFContentStreamRef cs, Ra::Transform ctm, size_t depth) {
            const char *name;  CGPDFStreamRef group;  CGPDFObjectRef tr;  CGPDFDictionaryRef resources = nullptr;
            if (!CGPDFDictionaryGetName(smask, "S", & name) || strcmp(name, "Luminosity") || !CGPDFDictionaryGetStream(smask, "G", & group)
                || (CGPDFDictionaryGetObject(smask, "TR", & tr) && !(CGPDFObjectGetValue(tr, kCGPDFObjectTypeName, & name) && !strcmp(name, "Identity")))
                || depth >= kMaxDepth)
                return kUnsupportedMask;
            if (!CGPDFDictionaryGetDictionary(CGPDFStreamGetDictionary(group), "Resources", & resources))     // It inherits cs's
                return addMask(readMaskGroup(group, resources, cs, depth), ctm);
            MaskKey key = { smask, { ctm.a, ctm.b, ctm.c, ctm.d, ctm.tx, ctm.ty } };
            auto it = maskIndices.find(key);
            if (it != maskIndices.end())
                return it->second;
            auto cached = maskGroups.find(smask);
            if (cached == maskGroups.end())
                cached = maskGroups.emplace(smask, readMaskGroup(group, resources, cs, depth)).first;
            return maskIndices[key] = addMask(cached->second, ctm);
        }
        // The mask's shading, in group space, at ctm
        int addMask(const Shading& shading, Ra::Transform ctm) {
            if (!shading.isValid)
                return kUnsupportedMask;
            masks.emplace_back(shading), masks.back().ctm = shading.ctm.concat(ctm);
            return int(masks.size() - 1);
        }
        // The shading a mask's group paints, in group space, with the group's alpha, or an invalid one
        Shading readMaskGroup(CGPDFStreamRef group, CGPDFDictionaryRef resources, CGPDFContentStreamRef cs, size_t depth) {
            std::vector<float> m;
            State state;
            if (readNumbers(CGPDFStreamGetDictionary(group), "Matrix", m) && m.size() == 6)
                state.ctm = Ra::Transform(m[0], m[1], m[2], m[3], m[4], m[5]);
            state.space = state.ctm;
            CGPDFContentStreamRef content = CGPDFContentStreamCreateWithStream(group, resources, cs);
            CGPDFOperatorTableRef table = createTable();
            Shadings contents;
            contents.readsImages = false;
            contents.scan(content, table, state, depth + 1);
            CGPDFOperatorTableRelease(table);
            CGPDFContentStreamRelease(content);
            
            const Shading *shading = nullptr;  float alpha = 1.f;
            if (contents.forms.empty() && contents.paths.size() == 1 && contents.shadings.empty() && contents.paths[0].pattern >= 0)
                shading = & contents.patterns[contents.paths[0].pattern], alpha = contents.paths[0].alpha;
            else if (contents.forms.empty() && contents.paths.empty() && contents.shadings.size() == 1)
                shading = & contents.shadings[0], alpha = shading->alpha;
            if (shading == nullptr || !shading->isValid)
                return Shading();
            Shading result = *shading;
            result.alpha = alpha;
            return result;
        }
        // pdfium's objects the scan doesn't have, but for paths, & their forms, by the form index & index of the scan's object
        // they precede, so they're drawn in their place by pdfium
        struct Orphan { FPDF_PAGEOBJECT object, parent; };
        std::map<std::pair<int, size_t>, std::vector<Orphan>> orphans;
        const Shading *pattern(const PathObject& object) const {
            return object.pattern >= 0 && patterns[object.pattern].isValid ? & patterns[object.pattern] : nullptr;
        }
        Clip& clip(int index) {
            static Clip none;
            return index < 0 ? none : clips[index];
        }
    };
    
    static int getPageCount(const char *filename) {
        Ra::SceneList list;
        FPDF_LIBRARY_CONFIG config;
        config.version = 3;
        config.m_pUserFontPaths = nullptr;
        config.m_pIsolate = nullptr;
        config.m_v8EmbedderSlot = 0;
        config.m_pPlatform = nullptr;
        FPDF_InitLibraryWithConfig(&config);
        
        FPDF_DOCUMENT doc = FPDF_LoadDocument(filename, NULL);
        int count = doc ? FPDF_GetPageCount(doc) : 0;
        FPDF_CloseDocument(doc);
        FPDF_DestroyLibrary();
        
        return count;
    }
    
    static Ra::Transform addPdfPageToScene(const char *filename, size_t pageIndex, Ra::SceneRef& scene) {
        Ra::Transform ctm;
        FPDF_LIBRARY_CONFIG config;
            config.version = 3;
            config.m_pUserFontPaths = nullptr;
            config.m_pIsolate = nullptr;
            config.m_v8EmbedderSlot = 0;
            config.m_pPlatform = nullptr;
        FPDF_InitLibraryWithConfig(&config);
        
        FPDF_DOCUMENT doc = FPDF_LoadDocument(filename, NULL);
        if (doc) {
            int count = FPDF_GetPageCount(doc);
            if (count > 0) {
                pageIndex = pageIndex > count - 1 ? count - 1 : pageIndex;
                FPDF_PAGE page = FPDF_LoadPage(doc, int(pageIndex));
                Shadings shadings;
                shadings.read(filename, pageIndex);
                
                ctm = transformForPage(page);
                if (0) {
                    Ra::Paint paint = paintFromPage(page);
                    if (paint.isImage()) {
                        Ra::Bounds bounds(0, 0, paint.bitmap->w, paint.bitmap->h);
                        Ra::Path path;  path->addBounds(bounds);
                        scene->addPath(path, Ra::Transform(), paint, 0, 0);
                    }
                } else
                    writePageToScene(doc, page, shadings, scene);
                
                FPDF_ClosePage(page);
            }
            FPDF_CloseDocument(doc);
        }
        FPDF_DestroyLibrary();
        return ctm;
    }
    
    static void writePageToScene(FPDF_DOCUMENT doc, FPDF_PAGE page, Shadings& shadings, Ra::SceneRef& scene) {
        TextPage textPage(page);
        matchObjects(page, nullptr, -1, shadings);
        writeStreamToScene(doc, page, -1, kNoMask, 1.f, kBlendNormal, textPage, shadings, scene);
    }
    
    // pdfium's objects are matched, depth first, with the scan's of the same kind in the same content stream, in order, within
    // a window, so an object only one has is skipped. Paths, which the scan & pdfium can disagree on, must also have the same
    // draw mode & bounds, which for a stroke contain the scan's. An unmatched form's contents are unmatched
    static constexpr size_t kMatchWindow = 16;
    static void matchObjects(FPDF_PAGE page, FPDF_PAGEOBJECT form, int container, Shadings& shadings) {
        static std::vector<Shadings::Object> none;
        std::vector<Shadings::Object>& objects = container >= -1 && size_t(container + 1) < shadings.streams.size() ? shadings.streams[container + 1] : none;
        size_t next = 0;
        int objectCount = form ? FPDFFormObj_CountObjects(form) : FPDFPage_CountObjects(page);
        for (int i = 0; i < objectCount; i++) {
            FPDF_PAGEOBJECT page_object = form ? FPDFFormObj_GetObject(form, i) : FPDFPage_GetObject(page, i);
            int type = FPDFPageObj_GetType(page_object), fillmode = 0;  FPDF_BOOL stroke = false;  float l = 0.f, b = 0.f, r = 0.f, t = 0.f;
            uint8_t kind = type == FPDF_PAGEOBJ_PATH ? Shadings::kPathObject : type == FPDF_PAGEOBJ_TEXT ? Shadings::kTextObject
                : type == FPDF_PAGEOBJ_IMAGE ? Shadings::kImageObject : type == FPDF_PAGEOBJ_SHADING ? Shadings::kShadingObject
                : type == FPDF_PAGEOBJ_FORM ? Shadings::kFormObject : Shadings::kNoObject;
            bool isPath = kind == Shadings::kPathObject;
            bool canMatch = !isPath || (FPDFPath_GetDrawMode(page_object, & fillmode, & stroke) && FPDFPageObj_GetBounds(page_object, & l, & b, & r, & t));
            Shadings::Object *match = nullptr;
            for (size_t k = next; canMatch && match == nullptr && k < objects.size() && k < next + kMatchWindow; k++) {
                Shadings::Object& object = objects[k];
                if (object.kind != kind)
                    continue;
                if (isPath) {
                    const Shadings::PathObject& path = shadings.paths[object.index];
                    if (path.mode != (fillmode | (stroke ? Shadings::kStroke : 0)) || !boundsMatch(path.bounds, Ra::Bounds(l, b, r, t), stroke))
                        continue;
                }
                match = & object, next = k + 1;
            }
            if (match)
                match->pdfium = page_object, match->parent = form;
            else if (container >= -1 && kind != Shadings::kPathObject && kind != Shadings::kNoObject)
                shadings.orphans[{ container, next }].push_back({ page_object, form });
            if (match && type == FPDF_PAGEOBJ_FORM)
                matchObjects(page, page_object, int(match->index), shadings);
        }
    }
    // Whether pdfium's bounds of a path are the scan's, or for a stroke, which pdfium's include, contain them
    static bool boundsMatch(Ra::Bounds scan, Ra::Bounds pdf, bool stroke) {
        float tol = 1e-3f * (1.f + fmaxf(fmaxf(fabsf(pdf.lx), fabsf(pdf.ux)), fmaxf(fabsf(pdf.ly), fabsf(pdf.uy))));
        if (stroke)
            return pdf.lx <= scan.lx + tol && pdf.ly <= scan.ly + tol && pdf.ux >= scan.ux - tol && pdf.uy >= scan.uy - tol;
        return fabsf(pdf.lx - scan.lx) <= tol && fabsf(pdf.ly - scan.ly) <= tol && fabsf(pdf.ux - scan.ux) <= tol && fabsf(pdf.uy - scan.uy) <= tol;
    }
    
    // Writes the scan's objects of the page's content stream, or a form's, which are in page space, & for a transparency group,
    // under its soft mask, opacity & blend mode. Under a mask only fills, as gradients with the mask's alpha, & images are
    // drawn. The opacity & blend mode are applied to each object, not the group, & an object's own blend mode replaces the
    // group's. pdfium draws its objects the scan doesn't have, & those it can't draw, in their place, clipped with the last
    // of the scan's objects' clip, or the form's at its start, & has colors the scan can't convert
    static void writeStreamToScene(FPDF_DOCUMENT doc, FPDF_PAGE page, int container, int mask, float opacity, uint8_t blend, TextPage& textPage, Shadings& shadings, Ra::SceneRef& scene) {
        static const std::vector<Shadings::Object> none;
        const std::vector<Shadings::Object>& objects = size_t(container + 1) < shadings.streams.size() ? shadings.streams[container + 1] : none;
        Ra::Transform formCTM = container < 0 ? Ra::Transform() : shadings.forms[container].ctm;
        int clip = container < 0 ? -1 : shadings.forms[container].clip;
        for (size_t k = 0; k <= objects.size(); k++) {
            auto it = shadings.orphans.find({ container, k });
            for (size_t i = 0; it != shadings.orphans.end() && i < it->second.size(); i++)
                writeOrphanToScene(doc, page, it->second[i], formCTM, clip, mask, opacity, blend, textPage, shadings, scene);
            if (k == objects.size())
                break;
            const Shadings::Object& object = objects[k];
            FPDF_PAGEOBJECT page_object = object.pdfium;
            clip = object.clip;
            int clipIndex = object.kind == Shadings::kShadingObject || object.kind == Shadings::kFormObject ? clip : objectClip(page_object, clip, formCTM, shadings);
            Shadings::Clip& objectClip = shadings.clip(clipIndex);
            Ra::Bounds *clipBounds = clipIndex < 0 ? nullptr : & objectClip.bounds;
            std::vector<Ra::Path>& clipPaths = objectClip.sorted;
            
            switch (object.kind) {
                case Shadings::kTextObject: {
                    const Shadings::TextRun& run = shadings.texts[object.index];
                    if (mask != kNoMask)
                        break;
                    if (run.isValid)
                        writeTextRunToScene(run, page_object, opacity, blend, clipBounds, clipPaths, scene);
                    else if (page_object)       // pdfium's glyphs
                        writeTextToScene(page_object, textPage, objectCTM(page_object, formCTM), opacity, blend, clipBounds, scene);
                    break;
                }
                case Shadings::kPathObject:
                    writePathToScene(page, object.parent, page_object, shadings.paths[object.index], formCTM, mask, opacity, blend, shadings, clipBounds, clipPaths, scene);
                    break;
                case Shadings::kImageObject:
                    writeImageToScene(doc, page, page_object, & shadings.imageObjects[object.index], page_object ? objectCTM(page_object, formCTM) : Ra::Transform(), mask, opacity, blend, shadings, clipBounds, clipPaths, scene);
                    break;
                case Shadings::kShadingObject: {
                    const Shading *shading = shadings.shadings[object.index].isValid ? & shadings.shadings[object.index] : nullptr;
                    writeShadingToScene(page, object.parent, page_object, shading, formCTM, mask, opacity, blend, shadings, clipBounds, clipPaths, scene);
                    break;
                }
                case Shadings::kFormObject: {
                    const Shadings::Group& group = shadings.forms[object.index];
                    int formMask = group.mask != kNoMask ? group.mask : mask;
                    float formOpacity = opacity * group.alpha;
                    uint8_t formBlend = group.blend != kBlendNormal ? group.blend : blend;
                    if (formMask != kUnsupportedMask && formOpacity != 0.f)      // Else it's skipped, & its contents
                        writeStreamToScene(doc, page, int(object.index), formMask, formOpacity, formBlend, textPage, shadings, scene);
                    break;
                }
                default:
                    break;
            }
        }
    }
    // An object of pdfium's the scan doesn't have, drawn by pdfium with the clip of the scan's object before it. A form's
    // objects are too, but for paths, & soft masks & opacities, which the scan has
    static void writeOrphanToScene(FPDF_DOCUMENT doc, FPDF_PAGE page, Shadings::Orphan orphan, Ra::Transform formCTM, int clip, int mask, float opacity, uint8_t blend, TextPage& textPage, Shadings& shadings, Ra::SceneRef& scene) {
        int type = FPDFPageObj_GetType(orphan.object), clipIndex = type == FPDF_PAGEOBJ_SHADING || type == FPDF_PAGEOBJ_FORM ? clip : objectClip(orphan.object, clip, formCTM, shadings);
        if (type == FPDF_PAGEOBJ_FORM) {
            Ra::Transform ctm = objectCTM(orphan.object, formCTM);
            for (int i = 0, count = FPDFFormObj_CountObjects(orphan.object); i < count; i++) {
                writeOrphanToScene(doc, page, { FPDFFormObj_GetObject(orphan.object, i), orphan.object }, ctm, clip, mask, opacity, blend, textPage, shadings, scene);
                if (FPDFFormObj_CountObjects(orphan.object) < count)      // A bitmap fallback took the object
                    i--, count--;
            }
            return;
        }
        Shadings::Clip& objectClip = shadings.clip(clipIndex);
        Ra::Bounds *clipBounds = clipIndex < 0 ? nullptr : & objectClip.bounds;
        if (type == FPDF_PAGEOBJ_TEXT && mask == kNoMask)
            writeTextToScene(orphan.object, textPage, objectCTM(orphan.object, formCTM), opacity, blend, clipBounds, scene);
        else if (type == FPDF_PAGEOBJ_IMAGE)
            writeImageToScene(doc, page, orphan.object, nullptr, objectCTM(orphan.object, formCTM), mask, opacity, blend, shadings, clipBounds, objectClip.sorted, scene);
        else if (type == FPDF_PAGEOBJ_SHADING)
            writeShadingToScene(page, orphan.parent, orphan.object, nullptr, formCTM, mask, opacity, blend, shadings, clipBounds, objectClip.sorted, scene);
    }
    // pdfium's object's matrix, in page space
    static Ra::Transform objectCTM(FPDF_PAGEOBJECT page_object, Ra::Transform formCTM) {
        FS_MATRIX m;
        FPDFPageObj_GetMatrix(page_object, & m);
        return Ra::Transform(m.a, m.b, m.c, m.d, m.e, m.f).concat(formCTM);
    }
    // An object's clip: pdfium drops it if it's one rect of the object's stream's that contains the object, as pdfium's
    // bounds say, but for a shading or form
    static int objectClip(FPDF_PAGEOBJECT page_object, int clip, Ra::Transform formCTM, Shadings& shadings) {
        float l, b, r, t;
        if (clip < 0 || page_object == nullptr)
            return clip;
        Shadings::Clip& c = shadings.clips[clip];
        if (c.own == 1 && c.paths[0]->isRect() && FPDFPageObj_GetBounds(page_object, & l, & b, & r, & t)
            && c.paths[0]->bounds.contains(Ra::Bounds(Ra::Bounds(l, b, r, t).quad(formCTM))))
            return c.inherited;
        return clip;
    }
    
    static void writeCharMap(FPDF_TEXTPAGE text_page, CharMap& charMap) {
        int charCount = FPDFText_CountChars(text_page);
        for (int i = 0; i < charCount; i++) {
            FPDF_PAGEOBJECT textObject = FPDFText_GetTextObject(text_page, i);
            unsigned int code = FPDFText_GetUnicode(text_page, i);
            if (code > 32) {
                void *key = (void *)textObject;
                auto it = charMap.find(key);
                if (it == charMap.end())
                    charMap.emplace(key, std::vector<int>({ i }));
                else
                    it->second.emplace_back(i);
            }
        }
    }
    
    static inline Ra::Transform transformForPage(FPDF_PAGE page) {
        float left = 0.f, bottom = 0.f, right = 0.f, top = 0.f, tx = 0.f, ty = 0.f, sine, cosine;
        FPDFPage_GetMediaBox(page, & left, & bottom, & right, & top);
        int rot = FPDFPage_GetRotation(page);
        __sincosf(-rot * 0.5f * M_PI, & sine, & cosine);
        tx = rot == 2 ? right - left : rot == 3 ? top - bottom : tx;
        ty = rot == 1 ? right - left : rot == 2 ? top - bottom : ty;
        Ra::Transform originCTM(1.f, 0.f, 0.f, 1.f, -left, -bottom);
        Ra::Transform pageCTM(cosine, sine, -sine, cosine, tx, ty);
        return pageCTM.concat(originCTM);
    }
    
    // Writes a text object's glyphs from the scan, filled and or stroked as its text rendering mode says, & not if it's invisible
    // or only clips
    static void writeTextRunToScene(const Shadings::TextRun& run, FPDF_PAGEOBJECT page_object, float opacity, uint8_t blend, Ra::Bounds *clipBounds, std::vector<Ra::Path>& clipPaths, Ra::SceneRef& scene) {
        int mode = run.mode;
        bool fills = mode == FPDF_TEXTRENDERMODE_FILL || mode == FPDF_TEXTRENDERMODE_FILL_STROKE || mode == FPDF_TEXTRENDERMODE_FILL_CLIP || mode == FPDF_TEXTRENDERMODE_FILL_STROKE_CLIP;
        bool strokes = mode == FPDF_TEXTRENDERMODE_STROKE || mode == FPDF_TEXTRENDERMODE_FILL_STROKE || mode == FPDF_TEXTRENDERMODE_STROKE_CLIP || mode == FPDF_TEXTRENDERMODE_FILL_STROKE_CLIP;
        Ra::Path *clipPath = clipPaths.size() == 0 || clipPaths[0]->isRect() ? nullptr : & clipPaths[0];
        Ra::Color fill, stroke;
        fills = fills && objectColor(run.colors, page_object, false, opacity, fill), strokes = strokes && objectColor(run.colors, page_object, true, opacity, stroke);
        float width = run.width == 0.f ? -1.f : run.width / run.unitsPerEm;     // In text space, which glyphs' paths are in
        for (auto& glyph : run.glyphs) {
            if (fills)
                scene->addPath(glyph.path, glyph.ctm, fill, 0.f, 0, clipBounds, clipPath, blend);
            if (strokes)
                scene->addPath(glyph.path, glyph.ctm, stroke, width, 0, clipBounds, clipPath, blend);
        }
    }
    
    static void writeTextToScene(FPDF_PAGEOBJECT page_object, TextPage& textPage, Ra::Transform textCTM, float opacity, uint8_t blend, Ra::Bounds *clipBounds, Ra::SceneRef& scene) {
        FPDF_TEXTPAGE text_page = textPage.get();
        CharMap& charMap = textPage.charMap;
        auto it = charMap.find((void *)page_object);
        if (it != charMap.end()) {
            double left = 0, bottom = 0, right = 0, top = 0;
            float hairline = -1.f;
            unsigned int R = 0, G = 0, B = 0, A = 255;
            FPDFPageObj_GetFillColor(page_object, & R, & G, & B, & A);
            Ra::Color red(0, 0, 255, 255), textColor(B, G, R, A * opacity + 0.5f);
            Ra::Path rect;  rect->addBounds(Ra::Bounds(0, 0, 1, 1)), rect->close();
            FPDF_FONT font = FPDFTextObj_GetFont(page_object);
            for(auto i : it->second) {
                unsigned int code = FPDFText_GetUnicode(text_page, i);
                FPDFText_GetCharBox(text_page, i, & left, & right, & bottom, & top);
                FPDF_GLYPHPATH pdfPath = FPDFFont_GetGlyphPath(font, code, 1);
                Ra::Path p = PathWriter().createPathFromGlyphPath(pdfPath);
                if (p->isValid()) {
                    Ra::Bounds b = Ra::Bounds(p->bounds.quad(textCTM));
                    Ra::Transform ctm = textCTM.concat(Ra::Bounds(left, bottom, right, top).fitTransform(b));
                    scene->addPath(p, ctm, textColor, 0.f, 0, nullptr, nullptr, blend);
                } else
                    scene->addPath(rect, Ra::Transform(right - left, 0, 0, top - bottom, left, bottom), red, hairline, 0);
            }
        }
    }
     
    // The scan's fill or stroke color, or else pdfium's, with opacity, if either has it
    static bool objectColor(const Shadings::Colors& colors, FPDF_PAGEOBJECT page_object, bool isStroke, float opacity, Ra::Color& color) {
        unsigned int R, G, B, A;
        if (isStroke ? colors.hasStroke : colors.hasFill)
            color = isStroke ? colors.stroke : colors.fill;
        else if (page_object && (isStroke ? FPDFPageObj_GetStrokeColor : FPDFPageObj_GetFillColor)(page_object, & R, & G, & B, & A))
            color = Ra::Color(B, G, R, A);
        else
            return false;
        color.a = color.a * opacity + 0.5f;
        return true;
    }
    // Writes the scan's path
    static void writePathToScene(FPDF_PAGE page, FPDF_PAGEOBJECT form, FPDF_PAGEOBJECT pageObject, const Shadings::PathObject& object, Ra::Transform formCTM, int mask, float opacity, uint8_t blend, Shadings& shadings, Ra::Bounds* clipBounds, std::vector<Ra::Path>& clipPaths, Ra::SceneRef& scene) {
        int fillmode = object.mode & ~Shadings::kStroke;
        bool stroke = object.mode & Shadings::kStroke;
        const Shading *pattern = shadings.pattern(object);
        bool isPattern = object.pattern >= 0;
        float alpha = object.alpha * opacity;
        mask = object.mask != kNoMask ? object.mask : mask;
        blend = object.blend != kBlendNormal ? object.blend : blend;
        Ra::Path *clipPath = clipPaths.size() == 0 || clipPaths[0]->isRect() ? nullptr : & clipPaths[0];
        
        if (mask != kUnsupportedMask) {
            Ra::Path path = object.path;
            Ra::Transform ctm = object.ctm;
            Ra::Color color(0, 0, 0, 255);
            if (fillmode != FPDF_FILLMODE_NONE && (objectColor(object.colors, pageObject, false, opacity, color) || isPattern)) {
                Ra::Path fill = path;
                Ra::Path *fillClipPath = clipPath;
                Ra::Transform fillCTM = ctm;
                if (fill->isRect() && ctm.det() != 0.f) {
                    // A rect fill that covers a non-rect clip paints the clip itself, even-odd, as clip masks are, & still clipped to
                    // the first one if it's another. Clip paths are in page space
                    Ra::Transform inv = ctm.invert();
                    for (auto& clip : clipPaths)
                        if (!clip->isRect() && clip->isValid() && path->bounds.contains(Ra::Bounds(clip->bounds.quad(inv)))) {
                            fill = clip, fillClipPath = & clip == & clipPaths[0] ? nullptr : clipPath, fillCTM = Ra::Transform();
                            break;
                        }
                }
                uint8_t flags = fillmode == FPDF_FILLMODE_ALTERNATE || fill.ptr != path.ptr ? Ra::Draw::kFillEvenOdd : 0;
                bool isGradient = fill->isValid() && fillCTM.det() != 0.f;
                Shading masked;
                if (mask >= 0) {
                    if (isGradient && (pattern || !isPattern) && Shading::masked(pattern, color, shadings.masks[mask], masked))
                        writeGradientToScene(masked, pattern ? alpha : 1.f, blend, fill, fillCTM, flags, clipBounds, fillClipPath, scene);
                } else if (pattern && isGradient)
                    writeGradientToScene(*pattern, alpha, blend, fill, fillCTM, flags, clipBounds, fillClipPath, scene);
                else if (isPattern && !stroke && pageObject) {
                    // An unsupported pattern fill is drawn as a bitmap of its page bounds, which takes the object from the page
                    Ra::Bounds bounds;  Ra::Path rect;
                    auto paint = paintFromPageObject(page, form, pageObject, formCTM, opacity, bounds);
                    rect->addBounds(bounds);
                    if (paint.isValid())
                        scene->addPath(rect, Ra::Transform(), paint, 0.f, 0, clipBounds, nullptr, blend);
                    return;
                } else if (fill->isValid() && !isPattern)
                    scene->addPath(fill, fillCTM, color, 0.f, flags, clipBounds, fillClipPath, blend);
            }
            if (stroke && mask == kNoMask && objectColor(object.colors, pageObject, true, opacity, color)) {
                uint8_t flags = 0;
                float width = object.width == 0.f ? -1.f : object.width;
                flags |= object.cap == FPDF_LINECAP_ROUND ? Ra::Draw::kRoundCap : 0;
                flags |= object.cap == FPDF_LINECAP_PROJECTING_SQUARE ? Ra::Draw::kSquareCap : 0;
                flags |= object.join == FPDF_LINEJOIN_ROUND ? Ra::Draw::kRoundJoin : 0;
                if (object.dash >= 0) {
                    std::vector<float>& lengths = shadings.dashes[object.dash];
                    path = Ra::Dasher::CreateDashedPath(path, object.dashPhase, lengths.data(), lengths.size());
                }
                if (path->isValid())
                    scene->addPath(path, ctm, color, width, flags, clipBounds, clipPath, blend);
            }
        }
    }
    
    // Writes the scan's image, clipped to the clip, or for one it couldn't decode, pdfium's bitmap, which has the image's
    // own opacity. An image mask is painted with pdfium's fill color, which has the fill alpha. The image's soft mask, or
    // else the group's, scales its alpha, & an unsupported one hides it
    static void writeImageToScene(FPDF_DOCUMENT doc, FPDF_PAGE page, FPDF_PAGEOBJECT page_object, const Shadings::ImageObject *object, Ra::Transform ctm, int mask, float opacity, uint8_t blend, Shadings& shadings, Ra::Bounds* clipBounds, std::vector<Ra::Path>& clipPaths, Ra::SceneRef& scene) {
        Ra::Paint image;
        mask = object && object->mask != kNoMask ? object->mask : mask;
        if (mask == kUnsupportedMask)
            return;
        if (object && object->image >= 0) {
            Ra::Color color;
            bool isMask = shadings.images[object->image].isMask;
            if (isMask && !objectColor(object->colors, page_object, false, 1.f, color))
                return;
            ctm = object->ctm, blend = object->blend != kBlendNormal ? object->blend : blend;
            image = shadings.imagePaint(object->image, opacity * (isMask ? color.a / 255.f : object->alpha), Ra::Color(color.b, color.g, color.r, 255), mask, ctm);
        } else if (page_object) {
            FPDF_BITMAP bitmap = FPDFImageObj_GetRenderedBitmap(doc, page, page_object);
            if (bitmap && mask >= 0 && FPDFBitmap_GetFormat(bitmap) == FPDFBitmap_BGRA)
                applyMask((Ra::Color *)FPDFBitmap_GetBuffer(bitmap), FPDFBitmap_GetWidth(bitmap), FPDFBitmap_GetHeight(bitmap), FPDFBitmap_GetStride(bitmap), ctm, shadings.masks[mask]);
            image = paintFromBitmap(bitmap, opacity);
            FPDFBitmap_Destroy(bitmap);
        }
        if (!image.isImage())
            return;
        Ra::Path *clipPath = clipPaths.size() == 0 || clipPaths[0]->isRect() ? nullptr : & clipPaths[0];
        Ra::Path unitRectPath;  unitRectPath->addBounds(Ra::Bounds(0, 0, 1, 1));
        scene->addPath(unitRectPath, ctm, image, 0, 0, clipBounds, clipPath, blend);
    }
    
    static void writeShadingToScene(FPDF_PAGE page, FPDF_PAGEOBJECT form, FPDF_PAGEOBJECT page_object, const Shading *shading, Ra::Transform formCTM, int mask, float opacity, uint8_t blend, Shadings& shadings, Ra::Bounds* clipBounds, std::vector<Ra::Path>& clipPaths, Ra::SceneRef& scene) {
        mask = shading && shading->mask != kNoMask ? shading->mask : mask;
        blend = shading && shading->blend != kBlendNormal ? shading->blend : blend;
        Shading masked;
        if (clipPaths.size() == 0 || mask == kUnsupportedMask)
            return;
        if (shading && clipPaths[0]->isValid() && shading->ctm.det() != 0.f) {
            if (mask == kNoMask)
                writeGradientToScene(*shading, shading->alpha * opacity, blend, clipPaths[0], Ra::Transform(), 0, clipBounds, nullptr, scene);
            else if (Shading::masked(shading, Ra::Color(), shadings.masks[mask], masked))
                writeGradientToScene(masked, shading->alpha * opacity, blend, clipPaths[0], Ra::Transform(), 0, clipBounds, nullptr, scene);
        } else if (mask == kNoMask && page_object) {
            Ra::Bounds bounds;  Ra::Path rect;
            auto paint = paintFromPageObject(page, form, page_object, formCTM, opacity, bounds);
            rect->addBounds(bounds);
            if (paint.isValid())
                scene->addPath(rect, Ra::Transform(), paint, 0, 0, clipBounds, nullptr, blend);
        }
    }
    
    // Fills path, in ctm space, with the gradient, clipped where an unextended end is inside it to the gradient's extent, & to clipPath
    static void writeGradientToScene(const Shading& shading, float alpha, uint8_t blend, Ra::Path& path, Ra::Transform ctm, uint8_t flags, Ra::Bounds* clipBounds, Ra::Path *clipPath, Ra::SceneRef& scene) {
        if (alpha == 0.f)
            return;
        std::vector<Ra::Color> colors = shading.colors;
        std::vector<float> locations = shading.locations;
        for (auto& color : colors)
            color.a = uint8_t(color.a * alpha + 0.5f);
        Ra::Transform unit = shading.unit.concat(shading.ctm);      // Gradient space to page space
        Ra::Bounds extent = Ra::Bounds(path->bounds.quad(ctm));
        if (clipBounds)
            extent = extent.intersect(*clipBounds);
        Ra::Bounds g = Ra::Bounds(extent.quad(unit.invert()));      // The fill's extent in gradient space
        bool clipLo, clipHi;
        if (shading.isRadial) {
            float nx = fmaxf(0.f, fmaxf(g.lx, -g.ux)), ny = fmaxf(0.f, fmaxf(g.ly, -g.uy));
            float fx = fmaxf(fabsf(g.lx), fabsf(g.ux)), fy = fmaxf(fabsf(g.ly), fabsf(g.uy));
            clipLo = !shading.extendLo && nx * nx + ny * ny < shading.lo * shading.lo;
            clipHi = !shading.extendHi && fx * fx + fy * fy > 1.f;
        } else
            clipLo = !shading.extendLo && g.ly < 0.f, clipHi = !shading.extendHi && g.uy > 1.f;
            
        if (!clipLo && !clipHi) {
            Ra::Paint paint(colors.data(), locations.data(), colors.size(), unit.concat(ctm.invert()), shading.isRadial);
            scene->addPath(path, ctm, paint, 0.f, flags, clipBounds, clipPath, blend);
            return;
        }
        Ra::Path region;  uint8_t regionFlags = 0;
        if (shading.isRadial) {
            if (clipHi)
                region->addEllipse(Ra::Bounds(-1.f, -1.f, 1.f, 1.f));
            else
                region->addBounds(g);
            if (clipLo)
                region->addEllipse(Ra::Bounds(-shading.lo, -shading.lo, shading.lo, shading.lo)), regionFlags = Ra::Draw::kFillEvenOdd;
        } else {
            Ra::Bounds band(g.lx, clipLo ? 0.f : g.ly, g.ux, clipHi ? 1.f : g.uy);
            if (band.ly >= band.uy)
                return;
            region->addBounds(band);
        }
        // An axis-aligned rect fill is the region within its bounds
        if (clipPath == nullptr && path->isRect() && ((ctm.b == 0.f && ctm.c == 0.f) || (ctm.a == 0.f && ctm.d == 0.f))) {
            Ra::Paint paint(colors.data(), locations.data(), colors.size(), Ra::Transform(), shading.isRadial);
            scene->addPath(region, unit, paint, 0.f, regionFlags, & extent, nullptr, blend);
            return;
        }
        // Otherwise the fill is clipped to the region, in page space, & to clipPath. Draws have one clip path, so for both, the
        // flattened clip path is clipped to the region's convex parts, its disc or band, & hole, combined even-odd as clip paths are
        Ra::Path clip;
        if (clipPath == nullptr)
            clip = transformedPath(region, unit);
        else {
            auto polygon = [&](float x, float y) {      // Gradient space to page space
                return std::vector<float>{ x * unit.a + y * unit.c + unit.tx, x * unit.b + y * unit.d + unit.ty };
            };
            auto disc = [&](float r) {
                float R = r * fmaxf(hypotf(unit.a, unit.b), hypotf(unit.c, unit.d));     // Its page radius, at most
                int n = R > kFlatness ? int(fminf(4096.f, ceilf(float(M_PI) / acosf(1.f - kFlatness / R)))) : 16;
                std::vector<float> xy;
                for (int i = 0; i < n; i++) {
                    auto p = polygon(r * cosf(2.f * float(M_PI) * i / n), r * sinf(2.f * float(M_PI) * i / n));
                    xy.insert(xy.end(), p.begin(), p.end());
                }
                return xy;
            };
            std::vector<float> outer, hole;
            if (shading.isRadial) {
                if (clipHi)
                    outer = disc(1.f);
                if (clipLo)
                    hole = disc(shading.lo);
            } else {
                region->validate();
                Ra::Bounds b = region->bounds;      // The band
                for (auto p : { polygon(b.lx, b.ly), polygon(b.ux, b.ly), polygon(b.ux, b.uy), polygon(b.lx, b.uy) })
                    outer.insert(outer.end(), p.begin(), p.end());
            }
            for (auto& subject : flattenedPath(*clipPath, kFlatness)) {
                addPolygon(clip, outer.size() ? clippedPolygon(subject, outer) : subject);
                if (hole.size())
                    addPolygon(clip, clippedPolygon(subject, hole));
            }
        }
        Ra::Paint paint(colors.data(), locations.data(), colors.size(), unit.concat(ctm.invert()), shading.isRadial);
        scene->addPath(path, ctm, paint, 0.f, flags, clipBounds, & clip, blend);
    }
    
    static constexpr float kFlatness = 1e-2f;      // The page space error of flattened clip paths
    
    // The path's subpaths as polygons of x, y pairs, with curves flattened to within tolerance
    static std::vector<std::vector<float>> flattenedPath(Ra::Path& path, float tolerance) {
        std::vector<std::vector<float>> polygons;
        path->validate();
        const float *pts = path->points.base;  const uint8_t *types = path->types.base;
        float x0 = 0.f, y0 = 0.f;
        auto add = [&](float x, float y) {
            if (polygons.empty())
                polygons.emplace_back();
            polygons.back().emplace_back(x), polygons.back().emplace_back(y), x0 = x, y0 = y;
        };
        auto steps = [&](float d) { return int(fminf(256.f, fmaxf(1.f, ceilf(sqrtf(d / tolerance))))); };
        for (size_t i = 0; i < path->types.end; i += path->TypeSizes[types[i]]) {
            const float *p = pts + 2 * i;
            switch (types[i]) {
                case Ra::Geometry::kMove:
                    polygons.emplace_back(), add(p[0], p[1]);
                    break;
                case Ra::Geometry::kLine:
                    add(p[0], p[1]);
                    break;
                case Ra::Geometry::kQuadratic: {
                    float ax = x0, ay = y0;
                    for (int k = 1, n = steps(0.25f * hypotf(ax - 2.f * p[0] + p[2], ay - 2.f * p[1] + p[3])); k <= n; k++) {
                        float t = float(k) / n, s = 1.f - t;
                        add(s * s * ax + 2.f * s * t * p[0] + t * t * p[2], s * s * ay + 2.f * s * t * p[1] + t * t * p[3]);
                    }
                    break;
                }
                case Ra::Geometry::kCubic: {
                    float ax = x0, ay = y0;
                    float d = fmaxf(hypotf(ax - 2.f * p[0] + p[2], ay - 2.f * p[1] + p[3]), hypotf(p[0] - 2.f * p[2] + p[4], p[1] - 2.f * p[3] + p[5]));
                    for (int k = 1, n = steps(0.75f * d); k <= n; k++) {
                        float t = float(k) / n, s = 1.f - t;
                        add(s * s * s * ax + 3.f * s * t * (s * p[0] + t * p[2]) + t * t * t * p[4], s * s * s * ay + 3.f * s * t * (s * p[1] + t * p[3]) + t * t * t * p[5]);
                    }
                    break;
                }
                default:
                    break;
            }
        }
        return polygons;
    }
    // The subject polygon clipped to the convex one (Sutherland-Hodgman), which keeps the subject's winding within it
    static std::vector<float> clippedPolygon(std::vector<float> subject, std::vector<float> convex) {
        size_t m = convex.size() / 2;
        float area = 0.f;
        for (size_t i = 0; i < m; i++)
            area += convex[2 * i] * convex[2 * ((i + 1) % m) + 1] - convex[2 * ((i + 1) % m)] * convex[2 * i + 1];
        if (area < 0.f)     // Counter-clockwise, so its inside is left of each edge
            for (size_t i = 0, j = m - 1; i < j; i++, j--)
                std::swap(convex[2 * i], convex[2 * j]), std::swap(convex[2 * i + 1], convex[2 * j + 1]);
        std::vector<float> input;
        for (size_t e = 0; e < m && subject.size(); e++) {
            float ax = convex[2 * e], ay = convex[2 * e + 1], bx = convex[2 * ((e + 1) % m)], by = convex[2 * ((e + 1) % m) + 1];
            auto side = [&](float x, float y) { return (bx - ax) * (y - ay) - (by - ay) * (x - ax); };
            input.swap(subject), subject.clear();
            for (size_t n = input.size() / 2, i = 0; i < n; i++) {
                size_t j = (i + n - 1) % n;
                float px = input[2 * j], py = input[2 * j + 1], cx = input[2 * i], cy = input[2 * i + 1], sp = side(px, py), sc = side(cx, cy);
                if ((sp >= 0.f) != (sc >= 0.f)) {
                    float t = sp / (sp - sc);
                    subject.emplace_back(px + t * (cx - px)), subject.emplace_back(py + t * (cy - py));
                }
                if (sc >= 0.f)
                    subject.emplace_back(cx), subject.emplace_back(cy);
            }
        }
        return subject;
    }
    static void addPolygon(Ra::Path& path, const std::vector<float>& xy) {
        if (xy.size() < 6)
            return;
        path->moveTo(xy[0], xy[1]);
        for (size_t i = 2; i < xy.size(); i += 2)
            path->lineTo(xy[i], xy[i + 1]);
        path->close();
    }
    
    // The path transformed, appended to p
    static Ra::Path transformedPath(Ra::Path& path, Ra::Transform m, Ra::Path p = Ra::Path()) {
        path->validate();
        const float *pts = path->points.base;  const uint8_t *types = path->types.base;
        auto x = [&](size_t i) { return pts[2 * i] * m.a + pts[2 * i + 1] * m.c + m.tx; };
        auto y = [&](size_t i) { return pts[2 * i] * m.b + pts[2 * i + 1] * m.d + m.ty; };
        for (size_t i = 0; i < path->types.end; i += path->TypeSizes[types[i]]) {
            switch (types[i]) {
                case Ra::Geometry::kMove:
                    p->moveTo(x(i), y(i));
                    break;
                case Ra::Geometry::kLine:
                    p->lineTo(x(i), y(i));
                    break;
                case Ra::Geometry::kQuadratic:
                    p->quadTo(x(i), y(i), x(i + 1), y(i + 1));
                    break;
                case Ra::Geometry::kCubic:
                    p->cubicTo(x(i), y(i), x(i + 1), y(i + 1), x(i + 2), y(i + 2));
                    break;
                case Ra::Geometry::kClose:
                    p->close();
                    break;
            }
        }
        return p;
    }
    
    static Ra::Paint paintFromPage(FPDF_PAGE page) {
        int width = FPDF_GetPageWidth(page);
        int height = FPDF_GetPageHeight(page);
        FPDF_BITMAP bitmap = FPDFBitmap_Create(width, height, 1);
        FPDF_RenderPageBitmap(bitmap, page, 0, 0, width, height, 0, 0);
        Ra::Paint paint = paintFromBitmap(bitmap);
        FPDFBitmap_Destroy(bitmap);
        return paint;
    }
   
    static Ra::Paint paintFromBitmap(FPDF_BITMAP bitmap, float opacity = 1.f) {
        int format = FPDFBitmap_GetFormat(bitmap);
        if (format != 4)
            return Ra::Paint();
        auto buffer = (Ra::Color *)FPDFBitmap_GetBuffer(bitmap);
        size_t width = FPDFBitmap_GetWidth(bitmap);
        size_t height = FPDFBitmap_GetHeight(bitmap);
        size_t stride = FPDFBitmap_GetStride(bitmap);
        premultiply(buffer, width, height, stride, opacity);
        return Ra::Paint(buffer, width, height, stride);
    }
    
    // Multiplies the alpha of straight alpha pixels, of an image drawn at ctm, by a soft mask's at their centers
    static void applyMask(Ra::Color *pixels, size_t width, size_t height, size_t stride, Ra::Transform ctm, const Shading& mask) {
        Ra::Transform inverse = mask.unit.concat(mask.ctm).invert();
        for (size_t y = 0; y < height; y++, pixels += stride / sizeof(Ra::Color))
            for (size_t x = 0; x < width; x++) {
                float u = (x + 0.5f) / width, v = 1.f - (y + 0.5f) / height;    // Rows run down from the unit square's top
                float alpha = mask.luminosityAt(u * ctm.a + v * ctm.c + ctm.tx, u * ctm.b + v * ctm.d + ctm.ty, inverse);
                pixels[x].a = uint8_t(pixels[x].a * alpha + 0.5f);
            }
    }
    
    // pdfium's BGRA bitmaps aren't premultiplied, but Rasterizer's images are. Their alpha is scaled by opacity first
    static void premultiply(Ra::Color *buffer, size_t width, size_t height, size_t stride, float opacity = 1.f) {
        for (size_t y = 0; y < height; y++, buffer += stride / sizeof(Ra::Color))
            for (size_t x = 0; x < width; x++)
                if (opacity != 1.f)
                    buffer[x] = buffer[x].withOpacity(opacity).premultiplied();
                else if (buffer[x].a != 255)
                    buffer[x] = buffer[x].premultiplied();
    }
    
    // Renders the object, removed from its page or form, over bounds, its page bounds. A form's object stays in form space, with
    // its clip & pattern, so is rendered with the form's ctm
    static Ra::Paint paintFromPageObject(FPDF_PAGE page, FPDF_PAGEOBJECT form, FPDF_PAGEOBJECT page_object, Ra::Transform formCTM, float opacity, Ra::Bounds& bounds) {
        int width = FPDF_GetPageWidth(page);
        int height = FPDF_GetPageHeight(page);

        if (form)
            FPDFFormObj_RemoveObject(form, page_object);
        else
            FPDFPage_RemoveObject(page, page_object);
        
        float left, bottom, right, top;
        FPDFPageObj_GetBounds(page_object, & left, & bottom, & right, & top);
        bounds = Ra::Bounds(Ra::Bounds(left, bottom, right, top).quad(formCTM)).integral().intersect(Ra::Bounds(0.f, 0.f, width, height));
        if (bounds.lx >= bounds.ux || bounds.ly >= bounds.uy) {     // Off the page, so the bitmap would hold none of it
            FPDFPageObj_Destroy(page_object);
            return Ra::Paint();
        }

        FPDF_DOCUMENT doc = FPDF_CreateNewDocument();
        FPDF_PAGE new_page = FPDFPage_New(doc, 0, width, height);
        FPDFPage_InsertObject(new_page, page_object);
        
        // pdfium applies the matrix after flipping the page to device space, so formCTM is flipped too
        float s = 2;
        Ra::Transform flip(1.f, 0.f, 0.f, -1.f, 0.f, height);
        Ra::Transform ctm = flip.concat(formCTM).concat(flip).concat(Ra::Transform(s, 0.f, 0.f, s, 0.f, 0.f));
        width *= s, height *= s;
        
        FS_RECTF clip;  clip.left = 0.f, clip.bottom = 0.f, clip.right = width, clip.top = height;
        FPDF_BITMAP bitmap = FPDFBitmap_Create(width, height, 1);
    
        FPDF_RenderPageBitmapWithMatrix(bitmap, new_page, (FS_MATRIX *)& ctm, & clip, 0);
        
        Ra::Paint paint;
        if (FPDFBitmap_GetFormat(bitmap) == 4) {
            auto buffer = (Ra::Color *)FPDFBitmap_GetBuffer(bitmap);
            size_t stride = FPDFBitmap_GetStride(bitmap);
            size_t offset = (height - s * bounds.uy) * stride / sizeof(Ra::Color) + s * bounds.lx;     // bounds are within the page
            premultiply(buffer + offset, s * bounds.width(), s * bounds.height(), stride, opacity);
            paint = Ra::Paint(buffer + offset, s * bounds.width(), s * bounds.height(), stride);
        }
        FPDFBitmap_Destroy(bitmap);
        FPDF_ClosePage(new_page);
        FPDF_CloseDocument(doc);
        return paint;
    }

    struct PathWriter {
        float x, y;
        std::vector<float> bezier;
        
        Ra::Path createPathFromGlyphPath(FPDF_GLYPHPATH path) {
            Ra::Path p;  int segmentCount = FPDFGlyphPath_CountGlyphSegments(path);
            if (segmentCount > 0)
                p->prealloc(segmentCount);
            for (int i = 0; i < segmentCount; i++)
                writeSegment(FPDFGlyphPath_GetGlyphPathSegment(path, i), p);
            return p;
        }
        
        void writeSegment(FPDF_PATHSEGMENT segment, Ra::Path& p) {
            FPDFPathSegment_GetPoint(segment, & x, & y);
            switch (FPDFPathSegment_GetType(segment)) {
                case FPDF_SEGMENT_MOVETO:
                    p->moveTo(x, y);
                    break;
                case FPDF_SEGMENT_LINETO:
                    p->lineTo(x, y);
                    break;
                case FPDF_SEGMENT_BEZIERTO:
                    bezier.emplace_back(x);
                    bezier.emplace_back(y);
                    if (bezier.size() == 6) {
                        p->cubicTo(bezier[0], bezier[1], bezier[2], bezier[3], bezier[4], bezier[5]);
                        bezier.clear();
                    }
                    break;
                default:
                    break;
            }
            if (FPDFPathSegment_GetClose(segment))
                p->close();
        }
    };
};

typedef RasterizerPDF RaPDF;
