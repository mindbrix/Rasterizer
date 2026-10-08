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
#import <algorithm>
#import <array>
#import <map>
#import <set>
#import <vector>


struct RasterizerPDF {
    typedef std::map<void *, std::vector<int>> CharMap;
    
    struct ClipState {
        Ra::Bounds clipBounds, *clipPtr = nullptr;
        std::vector<Ra::Path> clipPaths;
        size_t lastHash = ~0;
        
        // A form's objects' clips are in form space, & are within its clip, the parent's
        void update(FPDF_PAGEOBJECT page_object, Ra::Transform formCTM, const ClipState *parent) {
            FPDF_CLIPPATH clip_path = FPDFPageObj_GetClipPath(page_object);
            size_t hash = clipHash(clip_path);
            int clipCount = FPDFClipPath_CountPaths(clip_path);
            
            if (lastHash != hash) {
                lastHash = hash, clipPtr = nullptr, clipBounds = Ra::Bounds::huge(), clipPaths.resize(0);
                if (clipCount != -1) {
                    for (int j = 0; j < clipCount; j++) {
                        Ra::Path clip = PathWriter().createPathFromClipPath(clip_path, j);
                        if (parent)
                            clip = transformedPath(clip, formCTM);
                        clipPaths.emplace_back(clip);
                        if (clip->isValid())
                            clipBounds = clipBounds.intersect(clip->bounds);
                    }
                    clipPtr = & clipBounds;
                }
                if (parent && parent->clipPtr) {
                    clipPaths.insert(clipPaths.end(), parent->clipPaths.begin(), parent->clipPaths.end());
                    clipBounds = clipBounds.intersect(parent->clipBounds), clipPtr = & clipBounds;
                }
                // Draws are only clipped to the first clip path, & the bounds of all, so put the non-rect ones first
                std::stable_partition(clipPaths.begin(), clipPaths.end(), [](Ra::Path& clip) { return !clip->isRect(); });
            }
        }
        static size_t clipHash(FPDF_CLIPPATH clip_path) {
            size_t hash = 0;
            float x, y;
            int clipCount = FPDFClipPath_CountPaths(clip_path);
            if (clipCount != -1) {
                hash = XXH64(& clipCount, sizeof(clipCount), hash);
                for (int j = 0; j < clipCount; j++) {
                    int segmentCount = FPDFClipPath_CountPathSegments(clip_path, j);
                    assert(segmentCount);
                    hash = XXH64(& segmentCount, sizeof(segmentCount), hash);
                    for (int k = 0; k < 1; k++) {
                        FPDF_PATHSEGMENT segment = FPDFClipPath_GetPathSegment(clip_path, j, k);
                        FPDFPathSegment_GetPoint(segment, & x, & y);
                        hash = XXH64(& x, sizeof(x), hash), hash = XXH64(& y, sizeof(y), hash);
                    }
                }
            }
            return hash;
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
        int type = -1;
        float d0 = 0.f, d1 = 1.f, N = 1.f;
        size_t outputs = 0, size = 0;
        std::vector<float> range, c0, c1, bounds, encode, decode, samples;
        std::vector<Function> fns;
        
        bool read(CGPDFObjectRef obj) {
            CGPDFDictionaryRef dict = nullptr;  CGPDFStreamRef stream = nullptr;  CGPDFInteger t;  std::vector<float> domain;
            if (CGPDFObjectGetValue(obj, kCGPDFObjectTypeStream, & stream))
                dict = CGPDFStreamGetDictionary(stream);
            else
                CGPDFObjectGetValue(obj, kCGPDFObjectTypeDictionary, & dict);
            if (dict == nullptr || !CGPDFDictionaryGetInteger(dict, "FunctionType", & t) || !readNumbers(dict, "Domain", domain) || domain.size() < 2 || domain[0] > domain[1])
                return false;
            type = int(t), d0 = domain[0], d1 = domain[1];
            readNumbers(dict, "Range", range);
            if (type == 0)
                return readSampled(dict, stream);
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
            std::vector<float> sizes;  CGPDFInteger bps;  CGPDFDataFormat format;
            if (stream == nullptr || !readNumbers(dict, "Size", sizes) || sizes.size() != 1 || sizes[0] < 1.f || !CGPDFDictionaryGetInteger(dict, "BitsPerSample", & bps) || bps < 1 || bps > 32 || range.size() < 2 || range.size() % 2)
                return false;
            size = sizes[0], outputs = range.size() / 2;
            if (!readNumbers(dict, "Encode", encode))
                encode = { 0.f, float(size - 1) };
            if (!readNumbers(dict, "Decode", decode))
                decode = range;
            if (encode.size() != 2 || decode.size() != range.size())
                return false;
            CFDataRef data = CGPDFStreamCopyData(stream, & format);
            if (data == nullptr)
                return false;
            size_t count = size * outputs;
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
    struct Shadings {
        static constexpr size_t kMaxDepth = 32;
        enum { kStroke = 4 };       // A path's draw mode is its FPDF_FILLMODE, & kStroke if it's stroked, as pdfium's
        struct Place {              // The form an object is in, or -1 for the page, & its index among the form's objects of its kind
            int container = -1;
            uint32_t ordinal = 0;
            bool operator==(const Place& other) const { return container == other.container && ordinal == other.ordinal; }
        };
        struct PathFill {
            int pattern = -1;       // The fill's shading pattern in patterns, or -1 for a color
            int mask = kNoMask;     // In masks
            float alpha = 1.f;
            uint8_t blend = kBlendNormal, mode = 0;
            int container = -1;     // Its form, or -1 for the page
            Ra::Bounds bounds;      // Of its points, in its form's space, as pdfium's bounds of a fill. pdfium's form space is the
                                    // ctm at the Do, & its objects' matrices include the form matrix
        };
        std::vector<Shading> shadings, patterns, masks;
        struct Group {              // A transparency group form's soft mask, opacity & blend mode, which its contents start without
            int mask = kNoMask;
            float alpha = 1.f;
            uint8_t blend = kBlendNormal;
            Place place;            // Among the form's shadings & forms
        };
        std::vector<Place> shadingPlaces;       // Of shadings, among their form's shadings & forms
        std::vector<PathFill> paths;
        typedef std::pair<CGPDFDictionaryRef, std::array<float, 6>> MaskKey;     // A soft mask & the ctm it's set at
        std::map<MaskKey, int> maskIndices;
        std::map<CGPDFDictionaryRef, Shading> maskGroups;
        std::vector<Group> forms;
        size_t index = 0, pathIndex = 0, formIndex = 0;
        
        struct State {
            Ra::Transform ctm, space;   // space is the content stream's default space, which pattern matrices map to
            float alpha = 1.f;          // The fill alpha, ca
            bool isPatternSpace = false;
            int pattern = -1, mask = kNoMask;
            uint8_t blend = kBlendNormal;
        };
        struct Scan {
            State state;
            std::vector<State> stack;
            Shadings *shadings;
            CGPDFOperatorTableRef table;
            size_t points = 0, depth = 0;      // points in the current path, as pdfium makes no object for a path of one point
            Ra::Bounds bounds;                 // Of the current path, in user space
            int container = -1;                // The form index of the content stream, or -1 for the page's
            Ra::Transform base;                // The ctm at the stream's form's Do, so pdfium's space for its objects
            uint32_t others = 0;               // The stream's shadings & forms so far
        };
        void read(const char *filename, size_t pageIndex) {
            CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8 *)filename, strlen(filename), false);
            CGPDFDocumentRef doc = url ? CGPDFDocumentCreateWithURL(url) : nullptr;
            CGPDFPageRef page = doc ? CGPDFDocumentGetPage(doc, pageIndex + 1) : nullptr;
            if (page) {
                CGPDFDictionaryRef resources = nullptr;
                CGPDFDictionaryGetDictionary(CGPDFPageGetDictionary(page), "Resources", & resources);
                CGPDFContentStreamRef cs = CGPDFContentStreamCreateWithPage(page);
                CGPDFOperatorTableRef table = createTable(needsPaths(resources));
                scan(cs, table, State(), 0);
                CGPDFOperatorTableRelease(table);
                CGPDFContentStreamRelease(cs);
            }
            CGPDFDocumentRelease(doc);
            if (url)
                CFRelease(url);
        }
        void scan(CGPDFContentStreamRef cs, CGPDFOperatorTableRef table, State state, size_t depth, int container = -1, Ra::Transform base = Ra::Transform()) {
            Scan scan;  scan.shadings = this, scan.table = table, scan.state = state, scan.depth = depth, scan.container = container, scan.base = base;
            CGPDFScannerRef scanner = CGPDFScannerCreate(cs, table, & scan);
            CGPDFScannerScan(scanner);
            CGPDFScannerRelease(scanner);
        }
        // Paths are only tracked for pattern fills, soft masks & blend modes, in the resources or those of their forms. Each resources
        // dictionary is visited once, as forms often share them, & can list themselves
        static bool needsPaths(CGPDFDictionaryRef resources) {
            std::set<CGPDFDictionaryRef> visited;
            return needsPaths(resources, 0, visited);
        }
        static bool needsPaths(CGPDFDictionaryRef resources, size_t depth, std::set<CGPDFDictionaryRef>& visited) {
            struct Info { size_t depth; std::set<CGPDFDictionaryRef> *visited; bool found; } info = { depth, & visited, false };
            CGPDFDictionaryRef dict;
            if (resources == nullptr || depth > kMaxDepth || !visited.insert(resources).second)
                return false;
            if (CGPDFDictionaryGetDictionary(resources, "Pattern", & dict) && CGPDFDictionaryGetCount(dict) > 0)
                return true;
            if (CGPDFDictionaryGetDictionary(resources, "ExtGState", & dict))
                CGPDFDictionaryApplyFunction(dict, [](const char *key, CGPDFObjectRef obj, void *info) {
                    CGPDFDictionaryRef gs, smask;  CGPDFObjectRef bm;
                    if (CGPDFObjectGetValue(obj, kCGPDFObjectTypeDictionary, & gs) && (CGPDFDictionaryGetDictionary(gs, "SMask", & smask)
                        || (CGPDFDictionaryGetObject(gs, "BM", & bm) && readBlendMode(bm) != kBlendNormal)))
                        ((Info *)info)->found = true;
                }, & info);
            if (!info.found && CGPDFDictionaryGetDictionary(resources, "XObject", & dict))
                CGPDFDictionaryApplyFunction(dict, [](const char *key, CGPDFObjectRef obj, void *info) {
                    Info& i = *(Info *)info;  CGPDFStreamRef stream;  CGPDFDictionaryRef res;
                    if (!i.found && CGPDFObjectGetValue(obj, kCGPDFObjectTypeStream, & stream) && CGPDFDictionaryGetDictionary(CGPDFStreamGetDictionary(stream), "Resources", & res))
                        i.found = needsPaths(res, i.depth + 1, *i.visited);
                }, & info);
            return info.found;
        }
        static CGPDFOperatorTableRef createTable(bool tracksPaths) {
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
                scan.shadings->shadingPlaces.emplace_back(Place{ scan.container, scan.others++ });
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
                    || !CGPDFDictionaryGetName(dict = CGPDFStreamGetDictionary(stream), "Subtype", & name) || strcmp(name, "Form"))
                    return;
                // A transparency group's soft mask, opacity & blend mode apply to the group, so its contents start without them. Other
                // forms' contents inherit them, as pdfium's objects in them do
                CGPDFDictionaryRef groupDict;
                bool isGroup = CGPDFDictionaryGetDictionary(dict, "Group", & groupDict) && CGPDFDictionaryGetName(groupDict, "S", & name) && !strcmp(name, "Transparency");
                Group group;
                if (isGroup)
                    group.mask = scan.state.mask, group.alpha = scan.state.alpha, group.blend = scan.state.blend;
                group.place = { scan.container, scan.others++ };
                scan.shadings->forms.emplace_back(group);
                int container = int(scan.shadings->forms.size() - 1);
                if (scan.depth < kMaxDepth) {
                    State state = scan.state;
                    if (isGroup)
                        state.alpha = 1.f, state.mask = kNoMask, state.blend = kBlendNormal;
                    if (readNumbers(dict, "Matrix", m) && m.size() == 6)
                        state.ctm = Ra::Transform(m[0], m[1], m[2], m[3], m[4], m[5]).concat(state.ctm);
                    state.space = state.ctm;
                    CGPDFDictionaryGetDictionary(dict, "Resources", & resources);
                    CGPDFContentStreamRef form = CGPDFContentStreamCreateWithStream(stream, resources, cs);
                    scan.shadings->scan(form, scan.table, state, scan.depth + 1, container, scan.state.ctm);
                    CGPDFContentStreamRelease(form);
                }
            });
            if (tracksPaths)
                addPathCallbacks(table);
            return table;
        }
        // Tracks the fill color space & pattern, & records a path fill for each path painting operator
        static void addPathCallbacks(CGPDFOperatorTableRef table) {
            CGPDFOperatorTableSetCallback(table, "cs", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  const char *name;  CGPDFArrayRef array;
                scan.state.isPatternSpace = false, scan.state.pattern = -1;
                if (!CGPDFScannerPopName(scanner, & name))
                    return;
                CGPDFObjectRef obj = strcmp(name, "Pattern") ? CGPDFContentStreamGetResource(CGPDFScannerGetContentStream(scanner), "ColorSpace", name) : nullptr;
                scan.state.isPatternSpace = obj == nullptr ? !strcmp(name, "Pattern")
                    : (CGPDFObjectGetValue(obj, kCGPDFObjectTypeName, & name) && !strcmp(name, "Pattern"))
                    || (CGPDFObjectGetValue(obj, kCGPDFObjectTypeArray, & array) && CGPDFArrayGetName(array, 0, & name) && !strcmp(name, "Pattern"));
            });
            CGPDFOperatorTableSetCallback(table, "scn", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  const char *name;
                if (!scan.state.isPatternSpace || !CGPDFScannerPopName(scanner, & name))
                    return;
                CGPDFContentStreamRef cs = CGPDFScannerGetContentStream(scanner);
                CGPDFObjectRef obj = CGPDFContentStreamGetResource(cs, "Pattern", name);
                std::vector<Shading>& patterns = scan.shadings->patterns;
                patterns.emplace_back();
                Shading& pattern = patterns.back();
                pattern.isValid = obj && readPattern(obj, cs, pattern);
                pattern.ctm = pattern.ctm.concat(scan.state.space);
                scan.state.pattern = int(patterns.size() - 1);
            });
            for (const char *op : { "g", "rg", "k" })
                CGPDFOperatorTableSetCallback(table, op, [](CGPDFScannerRef scanner, void *info) {
                    Scan& scan = *(Scan *)info;
                    scan.state.isPatternSpace = false, scan.state.pattern = -1;
                });
            CGPDFOperatorTableSetCallback(table, "m", [](CGPDFScannerRef scanner, void *info) { addPoints(scanner, *(Scan *)info, 1, 1); });
            CGPDFOperatorTableSetCallback(table, "l", [](CGPDFScannerRef scanner, void *info) { addPoints(scanner, *(Scan *)info, 1, 1); });
            CGPDFOperatorTableSetCallback(table, "c", [](CGPDFScannerRef scanner, void *info) { addPoints(scanner, *(Scan *)info, 3, 3); });
            for (const char *op : { "v", "y" })
                CGPDFOperatorTableSetCallback(table, op, [](CGPDFScannerRef scanner, void *info) { addPoints(scanner, *(Scan *)info, 2, 3); });
            CGPDFOperatorTableSetCallback(table, "re", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;  CGPDFReal x, y, w, h;
                scan.points += 5;
                if (CGPDFScannerPopNumber(scanner, & h) && CGPDFScannerPopNumber(scanner, & w) && CGPDFScannerPopNumber(scanner, & y) && CGPDFScannerPopNumber(scanner, & x))
                    scan.bounds.extend(x, y), scan.bounds.extend(x + w, y + h);
            });
            CGPDFOperatorTableSetCallback(table, "n", [](CGPDFScannerRef scanner, void *info) {
                Scan& scan = *(Scan *)info;
                scan.points = 0, scan.bounds = Ra::Bounds();
            });
            CGPDFOperatorTableSetCallback(table, "f", [](CGPDFScannerRef scanner, void *info) { addPath(*(Scan *)info, FPDF_FILLMODE_WINDING); });
            CGPDFOperatorTableSetCallback(table, "F", [](CGPDFScannerRef scanner, void *info) { addPath(*(Scan *)info, FPDF_FILLMODE_WINDING); });
            CGPDFOperatorTableSetCallback(table, "f*", [](CGPDFScannerRef scanner, void *info) { addPath(*(Scan *)info, FPDF_FILLMODE_ALTERNATE); });
            for (const char *op : { "B", "b" })
                CGPDFOperatorTableSetCallback(table, op, [](CGPDFScannerRef scanner, void *info) { addPath(*(Scan *)info, FPDF_FILLMODE_WINDING | kStroke); });
            for (const char *op : { "B*", "b*" })
                CGPDFOperatorTableSetCallback(table, op, [](CGPDFScannerRef scanner, void *info) { addPath(*(Scan *)info, FPDF_FILLMODE_ALTERNATE | kStroke); });
            for (const char *op : { "S", "s" })
                CGPDFOperatorTableSetCallback(table, op, [](CGPDFScannerRef scanner, void *info) { addPath(*(Scan *)info, kStroke); });
        }
        // Adds the last count of an operator's points, of which it has points
        static void addPoints(CGPDFScannerRef scanner, Scan& scan, int count, size_t points) {
            CGPDFReal x, y;
            scan.points += points;
            for (int i = 0; i < count && CGPDFScannerPopNumber(scanner, & y) && CGPDFScannerPopNumber(scanner, & x); i++)
                scan.bounds.extend(x, y);
        }
        static void addPath(Scan& scan, uint8_t mode) {
            if (scan.points > 1) {
                PathFill fill;
                fill.pattern = (mode & 3) && scan.state.isPatternSpace ? scan.state.pattern : -1, fill.mask = scan.state.mask, fill.alpha = scan.state.alpha;
                fill.blend = scan.state.blend, fill.mode = mode, fill.container = scan.container;
                fill.bounds = Ra::Bounds(scan.bounds.quad(scan.state.ctm.concat(scan.base.invert())));
                scan.shadings->paths.emplace_back(fill);
            }
            scan.points = 0, scan.bounds = Ra::Bounds();
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
            CGPDFOperatorTableRef table = createTable(true);
            Shadings contents;
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
        // The shading for pdfium's next shading object, if it could be read
        const Shading *next() {
            size_t i = index++;
            return i < shadings.size() && shadings[i].isValid ? & shadings[i] : nullptr;
        }
        // The fill of pdfium's next path object, if it can be matched
        std::vector<int> pathMatches;      // The path for each of pdfium's, or -1
        const PathFill *nextPath() {
            size_t i = pathIndex++;
            return i < pathMatches.size() && pathMatches[i] >= 0 ? & paths[pathMatches[i]] : nullptr;
        }
        const Shading *pattern(const PathFill *fill) const {
            return fill && fill->pattern >= 0 && patterns[fill->pattern].isValid ? & patterns[fill->pattern] : nullptr;
        }
        // The group of pdfium's next form object, if it can be matched
        const Group *nextForm() {
            size_t i = formIndex++;
            return i < forms.size() ? & forms[i] : nullptr;
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
        FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);
        CharMap charMap;
        writeCharMap(text_page, charMap);
        
        Match match;
        for (size_t i = 0; i < shadings.paths.size(); i++)
            match.paths[shadings.paths[i].container].emplace_back(i);
        matchObjects(page, nullptr, -1, shadings, match);
        if (!match.shadings || match.shading != shadings.shadings.size())
            shadings.shadings.resize(0);    // They can't be matched, so are drawn as bitmaps
        if (!match.forms || match.form != shadings.forms.size())      // They can't be matched, so their soft masks & opacities are
            shadings.forms.resize(0), shadings.pathMatches.assign(shadings.pathMatches.size(), -1);    // ignored, & nor can their paths
        
        writeObjectsToScene(doc, page, nullptr, Ra::Transform(), nullptr, kNoMask, 1.f, kBlendNormal, text_page, charMap, shadings, scene);
        FPDFText_ClosePage(text_page);
    }
    
    // pdfium's shadings & forms are matched, depth first, with the scan's by their place in their form, & a kind with any mismatch is
    // dropped. Paths, which the scan & pdfium can disagree on, are aligned within each form by draw mode & bounds, which for a stroke
    // contain the scan's, so a path only one has is skipped
    struct Match {
        static constexpr size_t kWindow = 16;     // The scan's paths searched for each of pdfium's
        bool shadings = true, forms = true;
        size_t shading = 0, form = 0;
        std::map<int, std::vector<size_t>> paths;   // The scan's paths in each form, in order
        std::map<int, size_t> next;                 // The next of them to match
    };
    static void matchObjects(FPDF_PAGE page, FPDF_PAGEOBJECT form, int container, Shadings& shadings, Match& match) {
        uint32_t others = 0;
        std::vector<size_t>& paths = match.paths[container];  size_t& next = match.next[container];
        int objectCount = form ? FPDFFormObj_CountObjects(form) : FPDFPage_CountObjects(page);
        for (int i = 0; i < objectCount; i++) {
            FPDF_PAGEOBJECT page_object = form ? FPDFFormObj_GetObject(form, i) : FPDFPage_GetObject(page, i);
            int type = FPDFPageObj_GetType(page_object);
            if (type == FPDF_PAGEOBJ_PATH) {
                int fillmode, j = -1;  FPDF_BOOL stroke;  float l, b, r, t;
                if (next < paths.size() && FPDFPath_GetDrawMode(page_object, & fillmode, & stroke) && FPDFPageObj_GetBounds(page_object, & l, & b, & r, & t))
                    for (size_t k = next; j < 0 && k < paths.size() && k < next + Match::kWindow; k++) {
                        const Shadings::PathFill& fill = shadings.paths[paths[k]];
                        if (fill.mode == (fillmode | (stroke ? Shadings::kStroke : 0)) && boundsMatch(fill.bounds, Ra::Bounds(l, b, r, t), stroke))
                            j = int(paths[k]), next = k + 1;
                    }
                shadings.pathMatches.emplace_back(j);
            } else if (type == FPDF_PAGEOBJ_SHADING) {
                size_t j = match.shading++;
                match.shadings = match.shadings && j < shadings.shadingPlaces.size() && shadings.shadingPlaces[j] == Shadings::Place{ container, others };
                others++;
            } else if (type == FPDF_PAGEOBJ_FORM) {
                size_t j = match.form++;
                match.forms = match.forms && j < shadings.forms.size() && shadings.forms[j].place == Shadings::Place{ container, others };
                others++;
                matchObjects(page, page_object, int(j), shadings, match);
            }
        }
    }
    // Whether pdfium's bounds of a path are the scan's, or for a stroke, which pdfium's include, contain them
    static bool boundsMatch(Ra::Bounds scan, Ra::Bounds pdf, bool stroke) {
        float tol = 1e-3f * (1.f + fmaxf(fmaxf(fabsf(pdf.lx), fabsf(pdf.ux)), fmaxf(fabsf(pdf.ly), fabsf(pdf.uy))));
        if (stroke)
            return pdf.lx <= scan.lx + tol && pdf.ly <= scan.ly + tol && pdf.ux >= scan.ux - tol && pdf.uy >= scan.uy - tol;
        return fabsf(pdf.lx - scan.lx) <= tol && fabsf(pdf.ly - scan.ly) <= tol && fabsf(pdf.ux - scan.ux) <= tol && fabsf(pdf.uy - scan.uy) <= tol;
    }
    
    static void countObjects(FPDF_PAGE page, FPDF_PAGEOBJECT form, size_t& paths, size_t& shadings, size_t& forms) {
        int objectCount = form ? FPDFFormObj_CountObjects(form) : FPDFPage_CountObjects(page);
        for (int i = 0; i < objectCount; i++) {
            FPDF_PAGEOBJECT page_object = form ? FPDFFormObj_GetObject(form, i) : FPDFPage_GetObject(page, i);
            int type = FPDFPageObj_GetType(page_object);
            paths += type == FPDF_PAGEOBJ_PATH, shadings += type == FPDF_PAGEOBJ_SHADING;
            if (type == FPDF_PAGEOBJ_FORM)
                forms++, countObjects(page, page_object, paths, shadings, forms);
        }
    }
    
    // Writes the page's objects, or a form's, whose objects are in form space, within its clip, & for a transparency group, under its
    // soft mask, opacity & blend mode. Under a mask only fills are drawn, as gradients with the mask's alpha. The opacity & blend mode
    // are applied to each object, not the group, & an object's own blend mode replaces the group's
    static void writeObjectsToScene(FPDF_DOCUMENT doc, FPDF_PAGE page, FPDF_PAGEOBJECT form, Ra::Transform formCTM, ClipState *parentClip, int mask, float opacity, uint8_t blend, FPDF_TEXTPAGE text_page, CharMap& charMap, Shadings& shadings, Ra::SceneRef& scene) {
        ClipState clipState;
        
        FS_MATRIX m;
        Ra::Transform ctm;
        int objectCount = form ? FPDFFormObj_CountObjects(form) : FPDFPage_CountObjects(page);
        for (int i = 0; i < objectCount; i++) {
            FPDF_PAGEOBJECT page_object = form ? FPDFFormObj_GetObject(form, i) : FPDFPage_GetObject(page, i);
            
            FPDFPageObj_GetMatrix(page_object, & m);
            ctm = Ra::Transform(m.a, m.b, m.c, m.d, m.e, m.f).concat(formCTM);
            clipState.update(page_object, formCTM, parentClip);
            
            switch (FPDFPageObj_GetType(page_object)) {
                case FPDF_PAGEOBJ_TEXT:
                    if (mask == kNoMask)
                        writeTextToScene(page_object, text_page, charMap, ctm, opacity, blend, clipState.clipPtr, scene);
                    break;
                case FPDF_PAGEOBJ_PATH:
                    writePathToScene(page, form, page_object, ctm, formCTM, mask, opacity, blend, shadings, clipState.clipPtr, clipState.clipPaths, scene);
                    break;
                case FPDF_PAGEOBJ_IMAGE:
                    if (mask == kNoMask)
                        writeImageToScene(doc, page, page_object, ctm, opacity, blend, clipState.clipPtr, clipState.clipPaths, scene);
                    break;
                case FPDF_PAGEOBJ_SHADING:
                    writeShadingToScene(page, form, page_object, formCTM, mask, opacity, blend, shadings, clipState.clipPtr, clipState.clipPaths, scene);
                    break;
                case FPDF_PAGEOBJ_FORM: {
                    const Shadings::Group *group = shadings.nextForm();
                    int formMask = group && group->mask != kNoMask ? group->mask : mask;
                    float formOpacity = opacity * (group ? group->alpha : 1.f);
                    uint8_t formBlend = group && group->blend != kBlendNormal ? group->blend : blend;
                    if (formMask == kUnsupportedMask || formOpacity == 0.f) {      // Skipped, & its contents
                        size_t paths = 0, shadingCount = 0, forms = 0;
                        countObjects(page, page_object, paths, shadingCount, forms);
                        shadings.pathIndex += paths, shadings.index += shadingCount, shadings.formIndex += forms;
                    } else
                        writeObjectsToScene(doc, page, page_object, ctm, & clipState, formMask, formOpacity, formBlend, text_page, charMap, shadings, scene);
                    break;
                }
                default:
                    break;
            }
            if ((form ? FPDFFormObj_CountObjects(form) : FPDFPage_CountObjects(page)) < objectCount)      // A bitmap fallback took the object
                i--, objectCount--;
        }
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
    
    static void writeTextToScene(FPDF_PAGEOBJECT page_object, FPDF_TEXTPAGE text_page, CharMap& charMap, Ra::Transform textCTM, float opacity, uint8_t blend, Ra::Bounds *clipBounds, Ra::SceneRef& scene) {
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
     
    static void writePathToScene(FPDF_PAGE page, FPDF_PAGEOBJECT form, FPDF_PAGEOBJECT pageObject, Ra::Transform ctm, Ra::Transform formCTM, int mask, float opacity, uint8_t blend, Shadings& shadings, Ra::Bounds* clipBounds, std::vector<Ra::Path>& clipPaths, Ra::SceneRef& scene) {
        int fillmode;
        FPDF_BOOL stroke;
        const Shadings::PathFill *pathFill = shadings.nextPath();
        const Shading *pattern = shadings.pattern(pathFill);
        bool isPattern = pathFill && pathFill->pattern >= 0;
        float alpha = (pathFill ? pathFill->alpha : 1.f) * opacity;
        mask = pathFill && pathFill->mask != kNoMask ? pathFill->mask : mask;
        blend = pathFill && pathFill->blend != kBlendNormal ? pathFill->blend : blend;
        Ra::Path *clipPath = clipPaths.size() == 0 || clipPaths[0]->isRect() ? nullptr : & clipPaths[0];
        
        if (mask != kUnsupportedMask && FPDFPath_GetDrawMode(pageObject, & fillmode, & stroke)) {
            Ra::Path path = PathWriter().createPathFromObject(pageObject);
            unsigned int R = 0, G = 0, B = 0, A = 255;
            if (fillmode != FPDF_FILLMODE_NONE) {
                Ra::Path fill = path;
                Ra::Path *fillClipPath = clipPath;
                Ra::Transform fillCTM = ctm;
                FPDFPageObj_GetFillColor(pageObject, & R, & G, & B, & A);
                A = A * opacity + 0.5f;
                if (fill->isRect() && ctm.det() != 0.f) {
                    // A rect fill that covers a non-rect clip paints the clip itself; clip paths are in page space
                    Ra::Transform inv = ctm.invert();
                    for (auto clip : clipPaths)
                        if (!clip->isRect() && clip->isValid() && path->bounds.contains(Ra::Bounds(clip->bounds.quad(inv))))
                            fill = clip, fillClipPath = nullptr, fillCTM = Ra::Transform();
                }
                uint8_t flags = fillmode == FPDF_FILLMODE_ALTERNATE ? Ra::Draw::kFillEvenOdd : 0;
                bool isGradient = fill->isValid() && fillCTM.det() != 0.f;
                Shading masked;
                if (mask >= 0) {
                    if (isGradient && (pattern || !isPattern) && Shading::masked(pattern, Ra::Color(B, G, R, A), shadings.masks[mask], masked))
                        writeGradientToScene(masked, pattern ? alpha : 1.f, blend, fill, fillCTM, flags, clipBounds, fillClipPath, scene);
                } else if (pattern && isGradient)
                    writeGradientToScene(*pattern, alpha, blend, fill, fillCTM, flags, clipBounds, fillClipPath, scene);
                else if (isPattern && !stroke) {
                    // An unsupported pattern fill is drawn as a bitmap of its page bounds, which takes the object from the page
                    Ra::Bounds bounds;  Ra::Path rect;
                    auto paint = paintFromPageObject(page, form, pageObject, formCTM, opacity, bounds);
                    rect->addBounds(bounds);
                    if (paint.isValid())
                        scene->addPath(rect, Ra::Transform(), paint, 0.f, 0, clipBounds, nullptr, blend);
                    return;
                } else if (fill->isValid())
                    scene->addPath(fill, fillCTM, Ra::Color(B, G, R, A), 0.f, flags, clipBounds, fillClipPath, blend);
            }
            if (stroke && mask == kNoMask) {
                float width = 0.f;
                uint8_t flags = 0;
                FPDFPageObj_GetStrokeColor(pageObject, & R, & G, & B, & A);
                A = A * opacity + 0.5f;
                FPDFPageObj_GetStrokeWidth(pageObject, & width);
                width = width == 0.f ? -1.f : width;
                int cap = FPDFPageObj_GetLineCap(pageObject);
                flags |= cap == FPDF_LINECAP_ROUND ? Ra::Draw::kRoundCap : 0;
                flags |= cap == FPDF_LINECAP_PROJECTING_SQUARE ? Ra::Draw::kSquareCap : 0;
                int join = FPDFPageObj_GetLineJoin(pageObject);
                flags |= join == FPDF_LINEJOIN_ROUND ? Ra::Draw::kRoundJoin : 0;
                size_t dashCount = FPDFPageObj_GetDashCount(pageObject);
                if (dashCount) {
                    float phase;
                    Ra::Vector<float> lengths(dashCount);
                    FPDFPageObj_GetDashPhase(pageObject, & phase);
                    FPDFPageObj_GetDashArray(pageObject, & lengths[0], dashCount);
                    path = Ra::Dasher::CreateDashedPath(path, phase, & lengths[0], dashCount);;
                }
                if (path->isValid())
                    scene->addPath(path, ctm, Ra::Color(B, G, R, A), width, flags, clipBounds, clipPath, blend);
            }
        }
    }
    
    // pdfium's bitmap has the image's own opacity, so only a group's is applied
    static void writeImageToScene(FPDF_DOCUMENT doc, FPDF_PAGE page, FPDF_PAGEOBJECT page_object, Ra::Transform ctm, float opacity, uint8_t blend, Ra::Bounds* clipBounds, std::vector<Ra::Path>& clipPaths, Ra::SceneRef& scene) {
        FPDF_BITMAP bitmap = FPDFImageObj_GetRenderedBitmap(doc, page, page_object);
        auto image = paintFromBitmap(bitmap, opacity);
        FPDFBitmap_Destroy(bitmap);
        if (!image.isImage())
            return;
        
        Ra::Bounds unitBounds(0, 0, 1, 1);
        Ra::Path unitRectPath;  unitRectPath->addBounds(unitBounds);
        scene->addPath(unitRectPath, ctm, image, 0, 0, clipBounds, nullptr, blend);
    }
    
    static void writeShadingToScene(FPDF_PAGE page, FPDF_PAGEOBJECT form, FPDF_PAGEOBJECT page_object, Ra::Transform formCTM, int mask, float opacity, uint8_t blend, Shadings& shadings, Ra::Bounds* clipBounds, std::vector<Ra::Path>& clipPaths, Ra::SceneRef& scene) {
        const Shading *shading = shadings.next();
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
        } else if (mask == kNoMask) {
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
    
    static Ra::Path transformedPath(Ra::Path& path, Ra::Transform m) {
        Ra::Path p;
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
        
        Ra::Path createPathFromClipPath(FPDF_CLIPPATH clipPath, int index) {
            Ra::Path p;  int segmentCount = FPDFClipPath_CountPathSegments(clipPath, index);
            if (segmentCount > 0)
                p->prealloc(segmentCount);
            for (int i = 0; i < segmentCount; i++)
                writeSegment(FPDFClipPath_GetPathSegment(clipPath, index, i), p);
            return p;
        }
        
        Ra::Path createPathFromGlyphPath(FPDF_GLYPHPATH path) {
            Ra::Path p;  int segmentCount = FPDFGlyphPath_CountGlyphSegments(path);
            if (segmentCount > 0)
                p->prealloc(segmentCount);
            for (int i = 0; i < segmentCount; i++)
                writeSegment(FPDFGlyphPath_GetGlyphPathSegment(path, i), p);
            return p;
        }
        
        Ra::Path createPathFromObject(FPDF_PAGEOBJECT pageObject) {
            Ra::Path p;  int segmentCount = FPDFPath_CountSegments(pageObject);
            if (segmentCount > 0)
                p->prealloc(segmentCount);
            for (int i = 0; i < segmentCount; i++)
                writeSegment(FPDFPath_GetPathSegment(pageObject, i), p);
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
