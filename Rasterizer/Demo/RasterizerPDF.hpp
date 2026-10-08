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
#import <map>
#import <vector>


struct RasterizerPDF {
    typedef std::map<void *, std::vector<int>> CharMap;
    
    struct ClipState {
        Ra::Bounds clipBounds, *clipPtr = nullptr;
        std::vector<Ra::Path> clipPaths;
        size_t lastHash = ~0;
        
        void update(FPDF_PAGEOBJECT page_object) {
            FPDF_CLIPPATH clip_path = FPDFPageObj_GetClipPath(page_object);
            size_t hash = clipHash(clip_path);
            int clipCount = FPDFClipPath_CountPaths(clip_path);
            
            if (lastHash != hash) {
                lastHash = hash, clipPtr = nullptr, clipBounds = Ra::Bounds::huge(), clipPaths.resize(0);
                if (clipCount != -1) {
                    for (int j = 0; j < clipCount; j++) {
                        Ra::Path clip = PathWriter().createPathFromClipPath(clip_path, j);
                        clipPaths.emplace_back(clip);
                        if (clip->isValid())
                            clipBounds = clipBounds.intersect(clip->bounds);
                    }
                    clipPtr = & clipBounds;
                }
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
    
    // An axial or concentric radial shading, read with CGPDF as pdfium doesn't expose shadings, so it can be drawn as a gradient.
    // unit maps Rasterizer's gradient space to shading space: linear gradients run up y from 0 to 1, & radial ones out from the
    // origin to radius 1. Unextended ends are drawn with geometry, as gradient colors extend
    struct Shading {
        Ra::Transform ctm, unit;        // ctm & alpha are the sh operator's, as pdfium can't get a shading object's matrix or color
        bool isValid = false, isRadial = false, extendLo = false, extendHi = false;
        float alpha = 1.f, lo = 0.f;    // lo is the gradient's start, the inner radius for a radial
        std::vector<Ra::Color> colors;
        std::vector<float> locations;
        
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
    
    // The page's shadings, from its content stream's sh operators in order with their ctms. pdfium makes a shading object for
    // each, in the same order, so they're matched by index while the counts agree
    struct Shadings {
        std::vector<Shading> shadings;
        size_t index = 0;
        
        struct State {
            Ra::Transform ctm;
            float alpha = 1.f;      // The fill alpha, ca
        };
        struct Scan {
            State state;
            std::vector<State> stack;
            std::vector<Shading> *shadings;
        };
        void read(const char *filename, size_t pageIndex) {
            CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8 *)filename, strlen(filename), false);
            CGPDFDocumentRef doc = url ? CGPDFDocumentCreateWithURL(url) : nullptr;
            CGPDFPageRef page = doc ? CGPDFDocumentGetPage(doc, pageIndex + 1) : nullptr;
            if (page) {
                Scan scan;  scan.shadings = & shadings;
                CGPDFContentStreamRef cs = CGPDFContentStreamCreateWithPage(page);
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
                    Scan& scan = *(Scan *)info;  const char *name;  CGPDFDictionaryRef dict;  CGPDFReal ca;
                    if (!CGPDFScannerPopName(scanner, & name))
                        return;
                    CGPDFObjectRef obj = CGPDFContentStreamGetResource(CGPDFScannerGetContentStream(scanner), "ExtGState", name);
                    if (obj && CGPDFObjectGetValue(obj, kCGPDFObjectTypeDictionary, & dict) && CGPDFDictionaryGetNumber(dict, "ca", & ca))
                        scan.state.alpha = fmaxf(0.f, fminf(1.f, ca));
                });
                CGPDFOperatorTableSetCallback(table, "sh", [](CGPDFScannerRef scanner, void *info) {
                    Scan& scan = *(Scan *)info;  const char *name;  CGPDFDictionaryRef dict;
                    if (!CGPDFScannerPopName(scanner, & name))
                        return;
                    CGPDFContentStreamRef cs = CGPDFScannerGetContentStream(scanner);
                    CGPDFObjectRef obj = CGPDFContentStreamGetResource(cs, "Shading", name);
                    scan.shadings->emplace_back();
                    Shading& shading = scan.shadings->back();
                    shading.ctm = scan.state.ctm, shading.alpha = scan.state.alpha;
                    shading.isValid = obj && CGPDFObjectGetValue(obj, kCGPDFObjectTypeDictionary, & dict) && shading.read(dict, cs);
                });
                CGPDFScannerRef scanner = CGPDFScannerCreate(cs, table, & scan);
                CGPDFScannerScan(scanner);
                CGPDFScannerRelease(scanner);
                CGPDFOperatorTableRelease(table);
                CGPDFContentStreamRelease(cs);
            }
            CGPDFDocumentRelease(doc);
            if (url)
                CFRelease(url);
        }
        // The shading for pdfium's next shading object, if it could be read
        const Shading *next() {
            size_t i = index++;
            return i < shadings.size() && shadings[i].isValid ? & shadings[i] : nullptr;
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

        ClipState clipState;
        
        FS_MATRIX m;
        Ra::Transform ctm;
        int objectCount = FPDFPage_CountObjects(page), shadingCount = 0;
        for (int i = 0; i < objectCount; i++)
            shadingCount += FPDFPageObj_GetType(FPDFPage_GetObject(page, i)) == FPDF_PAGEOBJ_SHADING;
        if (shadingCount != shadings.shadings.size())
            shadings.shadings.resize(0);    // They can't be matched, so are drawn as bitmaps

        for (int i = 0; i < objectCount; i++) {
            FPDF_PAGEOBJECT page_object = FPDFPage_GetObject(page, i);
            
            FPDFPageObj_GetMatrix(page_object, & m);
            ctm = Ra::Transform(m.a, m.b, m.c, m.d, m.e, m.f);
            clipState.update(page_object);
            
            switch (FPDFPageObj_GetType(page_object)) {
                case FPDF_PAGEOBJ_TEXT:
                    writeTextToScene(page_object, text_page, charMap, ctm, clipState.clipPtr, scene);
                    break;
                case FPDF_PAGEOBJ_PATH:
                    writePathToScene(page_object, ctm, clipState.clipPtr, clipState.clipPaths, scene);
                    break;
                case FPDF_PAGEOBJ_IMAGE:
                    writeImageToScene(doc, page, page_object, ctm, clipState.clipPtr, clipState.clipPaths, scene);
                    break;
                case FPDF_PAGEOBJ_SHADING:
                    writeShadingToScene(page, page_object, shadings.next(), clipState.clipPtr, clipState.clipPaths, scene);
                    if (FPDFPage_CountObjects(page) < objectCount)      // The bitmap fallback takes the object from the page
                        i--, objectCount--;
                    break;
                default:
                    break;
            }
        }
        FPDFText_ClosePage(text_page);
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
    
    static void writeTextToScene(FPDF_PAGEOBJECT page_object, FPDF_TEXTPAGE text_page, CharMap& charMap, Ra::Transform textCTM, Ra::Bounds *clipBounds, Ra::SceneRef& scene) {
        auto it = charMap.find((void *)page_object);
        if (it != charMap.end()) {
            double left = 0, bottom = 0, right = 0, top = 0;
            float hairline = -1.f;
            unsigned int R = 0, G = 0, B = 0, A = 255;
            FPDFPageObj_GetFillColor(page_object, & R, & G, & B, & A);
            Ra::Color red(0, 0, 255, 255), textColor(B, G, R, A);
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
                    scene->addPath(p, ctm, textColor, 0.f, 0);
                } else
                    scene->addPath(rect, Ra::Transform(right - left, 0, 0, top - bottom, left, bottom), red, hairline, 0);
            }
        }
    }
     
    static void writePathToScene(FPDF_PAGEOBJECT pageObject, Ra::Transform ctm, Ra::Bounds* clipBounds, std::vector<Ra::Path>& clipPaths, Ra::SceneRef& scene) {
        int fillmode;
        FPDF_BOOL stroke;
        Ra::Path *clipPath = clipPaths.size() == 0 || clipPaths[0]->isRect() ? nullptr : & clipPaths[0];
        
        if (FPDFPath_GetDrawMode(pageObject, & fillmode, & stroke)) {
            Ra::Path path = PathWriter().createPathFromObject(pageObject);
            unsigned int R = 0, G = 0, B = 0, A = 255;
            if (fillmode != FPDF_FILLMODE_NONE) {
                Ra::Path fill = path;
                Ra::Path *fillClipPath = clipPath;
                Ra::Transform fillCTM = ctm;
                FPDFPageObj_GetFillColor(pageObject, & R, & G, & B, & A);
                if (fill->isRect() && ctm.det() != 0.f) {
                    // A rect fill that covers a non-rect clip paints the clip itself; clip paths are in page space
                    Ra::Transform inv = ctm.invert();
                    for (auto clip : clipPaths)
                        if (!clip->isRect() && clip->isValid() && path->bounds.contains(Ra::Bounds(clip->bounds.quad(inv))))
                            fill = clip, fillClipPath = nullptr, fillCTM = Ra::Transform();
                }
                uint8_t flags = fillmode == FPDF_FILLMODE_ALTERNATE ? Ra::Draw::kFillEvenOdd : 0;
                if (fill->isValid())
                    scene->addPath(fill, fillCTM, Ra::Color(B, G, R, A), 0.f, flags, clipBounds, fillClipPath);
            }
            if (stroke) {
                float width = 0.f;
                uint8_t flags = 0;
                FPDFPageObj_GetStrokeColor(pageObject, & R, & G, & B, & A);
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
                    scene->addPath(path, ctm, Ra::Color(B, G, R, A), width, flags, clipBounds, clipPath);
            }
        }
    }
    
    static void writeImageToScene(FPDF_DOCUMENT doc, FPDF_PAGE page, FPDF_PAGEOBJECT page_object, Ra::Transform ctm, Ra::Bounds* clipBounds, std::vector<Ra::Path>& clipPaths, Ra::SceneRef& scene) {
        FPDF_BITMAP bitmap = FPDFImageObj_GetRenderedBitmap(doc, page, page_object);
        auto image = paintFromBitmap(bitmap);
        FPDFBitmap_Destroy(bitmap);
        if (!image.isImage())
            return;
        
        Ra::Bounds unitBounds(0, 0, 1, 1);
        Ra::Path unitRectPath;  unitRectPath->addBounds(unitBounds);
        scene->addPath(unitRectPath, ctm, image, 0, 0, clipBounds);
    }
    
    static void writeShadingToScene(FPDF_PAGE page, FPDF_PAGEOBJECT page_object, const Shading *shading, Ra::Bounds* clipBounds, std::vector<Ra::Path>& clipPaths, Ra::SceneRef& scene) {
        if (clipPaths.size() == 0)
            return;
        if (shading && clipPaths[0]->isValid() && shading->ctm.det() != 0.f)
            writeGradientToScene(*shading, clipBounds, clipPaths[0], scene);
        else {
            auto paint = paintFromPageObject(page, page_object);
            if (paint.isValid())
                scene->addPath(clipPaths[0], Ra::Transform(), paint, 0, 0, clipBounds);
        }
    }
    
    // Fills the clip with the gradient, or where an unextended end is inside the clip, the gradient's extent clipped by it
    static void writeGradientToScene(const Shading& shading, Ra::Bounds* clipBounds, Ra::Path& clip, Ra::SceneRef& scene) {
        if (shading.alpha == 0.f)
            return;
        std::vector<Ra::Color> colors = shading.colors;
        std::vector<float> locations = shading.locations;
        for (auto& color : colors)
            color.a = uint8_t(shading.alpha * 255.f + 0.5f);
        Ra::Transform unit = shading.unit.concat(shading.ctm);
        Ra::Bounds g = Ra::Bounds(clipBounds->quad(unit.invert()));     // The clip's extent in gradient space
        bool clipLo, clipHi;
        if (shading.isRadial) {
            float nx = fmaxf(0.f, fmaxf(g.lx, -g.ux)), ny = fmaxf(0.f, fmaxf(g.ly, -g.uy));
            float fx = fmaxf(fabsf(g.lx), fabsf(g.ux)), fy = fmaxf(fabsf(g.ly), fabsf(g.uy));
            clipLo = !shading.extendLo && nx * nx + ny * ny < shading.lo * shading.lo;
            clipHi = !shading.extendHi && fx * fx + fy * fy > 1.f;
        } else
            clipLo = !shading.extendLo && g.ly < 0.f, clipHi = !shading.extendHi && g.uy > 1.f;
            
        if (!clipLo && !clipHi) {
            Ra::Paint paint(colors.data(), locations.data(), colors.size(), unit, shading.isRadial);
            scene->addPath(clip, Ra::Transform(), paint, 0.f, 0, clipBounds);
            return;
        }
        Ra::Path path;  uint8_t flags = 0;
        if (shading.isRadial) {
            if (clipHi)
                path->addEllipse(Ra::Bounds(-1.f, -1.f, 1.f, 1.f));
            else
                path->addBounds(g);
            if (clipLo)
                path->addEllipse(Ra::Bounds(-shading.lo, -shading.lo, shading.lo, shading.lo)), flags = Ra::Draw::kFillEvenOdd;
        } else {
            Ra::Bounds band(g.lx, clipLo ? 0.f : g.ly, g.ux, clipHi ? 1.f : g.uy);
            if (band.ly >= band.uy)
                return;
            path->addBounds(band);
        }
        Ra::Paint paint(colors.data(), locations.data(), colors.size(), Ra::Transform(), shading.isRadial);
        scene->addPath(path, unit, paint, 0.f, flags, clipBounds, clip->isRect() ? nullptr : & clip);
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
   
    static Ra::Paint paintFromBitmap(FPDF_BITMAP bitmap) {
        int format = FPDFBitmap_GetFormat(bitmap);
        if (format != 4)
            return Ra::Paint();
        auto buffer = (Ra::Color *)FPDFBitmap_GetBuffer(bitmap);
        size_t width = FPDFBitmap_GetWidth(bitmap);
        size_t height = FPDFBitmap_GetHeight(bitmap);
        size_t stride = FPDFBitmap_GetStride(bitmap);
        return Ra::Paint(buffer, width, height, stride);
    }
    
    static Ra::Paint paintFromPageObject(FPDF_PAGE page, FPDF_PAGEOBJECT page_object) {
        int width = FPDF_GetPageWidth(page);
        int height = FPDF_GetPageHeight(page);

        FPDFPage_RemoveObject(page, page_object);
        
        float left, bottom, right, top;
        FPDFPageObj_GetBounds(page_object, & left, & bottom, & right, & top);
        auto bounds = Ra::Bounds(left, bottom, right, top).integral();
        
        FPDF_DOCUMENT doc = FPDF_CreateNewDocument();
        FPDF_PAGE new_page = FPDFPage_New(doc, 0, width, height);
        FPDFPage_InsertObject(new_page, page_object);
        
        float s = 2;
        Ra::Transform ctm(s, 0.f, 0.f, s, 0.f, 0.f);
        width *= s, height *= s;
        
        FS_RECTF clip;  clip.left = 0.f, clip.bottom = 0.f, clip.right = width, clip.top = height;
        FPDF_BITMAP bitmap = FPDFBitmap_Create(width, height, 1);
    
        FPDF_RenderPageBitmapWithMatrix(bitmap, new_page, (FS_MATRIX *)& ctm, & clip, 0);
        
        int format = FPDFBitmap_GetFormat(bitmap);
        if (format != 4)
            return Ra::Paint();
        auto buffer = (Ra::Color *)FPDFBitmap_GetBuffer(bitmap);
        
        size_t stride = FPDFBitmap_GetStride(bitmap);
        size_t offset = (height - s * bounds.uy) * stride / sizeof(Ra::Color) + s * bounds.lx;
        auto paint = Ra::Paint(buffer + offset, s * bounds.width(), s * bounds.height(), stride);
        
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
