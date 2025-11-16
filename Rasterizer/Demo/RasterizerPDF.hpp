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
#import <map>
#import <vector>


struct RasterizerPDF {
    typedef std::map<void *, std::vector<int>> CharMap;
    
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
    
    static bool pathIsRect(Ra::Path p) {
        p->validate();
        float *pts = p->points.base, ax, ay, bx, by, t0, t1;
        if (p->types.end != 6 || p->counts[Ra::Geometry::kLine] != 4 || pts[0] != pts[10] || pts[1] != pts[11])
            return false;
        ax = pts[2] - pts[0], ay = pts[3] - pts[1], bx = pts[4] - pts[2], by = pts[5] - pts[3];
        t0 = (ax * bx + ay * by) / (ax * ax + ay * ay);
        ax = pts[6] - pts[4], ay = pts[7] - pts[5], bx = pts[8] - pts[6], by = pts[9] - pts[7];
        t1 = (ax * bx + ay * by) / (ax * ax + ay * ay);
        return fabsf(t0) < 1e-3f && fabsf(t1) < 1e-3f;
    }
    
    static void writeTextToScene(FPDF_PAGEOBJECT pageObject, FPDF_TEXTPAGE text_page, CharMap& charMap, FS_MATRIX m, Ra::Bounds *clipBounds, Ra::SceneRef& scene) {
        auto it = charMap.find((void *)pageObject);
        if (it != charMap.end()) {
            double left = 0, bottom = 0, right = 0, top = 0;
            Ra::Transform textCTM(m.a, m.b, m.c, m.d, m.e, m.f);
            float hairline = -1.f;
            unsigned int R = 0, G = 0, B = 0, A = 255;
            FPDFPageObj_GetFillColor(pageObject, & R, & G, & B, & A);
            Ra::BGRA red(0, 0, 255, 255), textColor(B, G, R, A);
            Ra::Path rect;  rect->addBounds(Ra::Bounds(0, 0, 1, 1)), rect->close();
            FPDF_FONT font = FPDFTextObj_GetFont(pageObject);
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
    
    static void writePathToScene(FPDF_PAGEOBJECT pageObject, FS_MATRIX m, Ra::Bounds* clipBounds, std::vector<Ra::Path>& clipPaths, Ra::SceneRef& scene) {
        int fillmode;
        FPDF_BOOL stroke;
        
        if (FPDFPath_GetDrawMode(pageObject, & fillmode, & stroke)) {
            Ra::Bounds clipUnion = clipBounds ? *clipBounds : Ra::Bounds::huge();
            Ra::Path path = PathWriter().createPathFromObject(pageObject);
            Ra::Transform ctm = Ra::Transform(m.a, m.b, m.c, m.d, m.e, m.f);
            unsigned int R = 0, G = 0, B = 0, A = 255;
            float width = 0.f;
            uint8_t flags = 0;
            if (stroke) {
                FPDFPageObj_GetStrokeColor(pageObject, & R, & G, & B, & A);
                FPDFPageObj_GetStrokeWidth(pageObject, & width);
                width = width == 0.f ? -1.f : width;
                int cap = FPDFPageObj_GetLineCap(pageObject);
                flags |= cap == FPDF_LINECAP_ROUND ? Ra::Scene::kRoundCap : 0;
                flags |= cap == FPDF_LINECAP_PROJECTING_SQUARE ? Ra::Scene::kSquareCap : 0;
                int join = FPDFPageObj_GetLineJoin(pageObject);
                flags |= join == FPDF_LINEJOIN_ROUND ? Ra::Scene::kRoundJoin : 0;
                size_t dashCount = FPDFPageObj_GetDashCount(pageObject);
                if (dashCount) {
                    float phase;
                    Ra::Vector<float> lengths(dashCount);
                    FPDFPageObj_GetDashPhase(pageObject, & phase);
                    FPDFPageObj_GetDashArray(pageObject, & lengths[0], dashCount);
                    Ra::Path dashed = Ra::Dasher::CreateDashedPath(path, phase, & lengths[0], dashCount);
                    path = dashed;
                }
            } else {
                FPDFPageObj_GetFillColor(pageObject, & R, & G, & B, & A);
                if (pathIsRect(path))
                    for (auto clip : clipPaths)
                        if (!pathIsRect(clip)) {
                            clipUnion = clipUnion.intersect(path->bounds);
                            path = clip;
                            break;
                        }
                flags |= fillmode == FPDF_FILLMODE_ALTERNATE ? Ra::Scene::kFillEvenOdd : 0;
            }
            scene->addPath(path, ctm, Ra::BGRA(B, G, R, A), width, flags, & clipUnion);
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

    static int getPageCount(const void *bytes, size_t size) {
        Ra::SceneList list;
        FPDF_LIBRARY_CONFIG config;
        config.version = 3;
        config.m_pUserFontPaths = nullptr;
        config.m_pIsolate = nullptr;
        config.m_v8EmbedderSlot = 0;
        config.m_pPlatform = nullptr;
        FPDF_InitLibraryWithConfig(&config);
        
        FPDF_DOCUMENT doc = FPDF_LoadMemDocument(bytes, int(size), NULL);
        int count = doc ? FPDF_GetPageCount(doc) : 0;
        FPDF_CloseDocument(doc);
        FPDF_DestroyLibrary();
        
        return count;
    }
    
    static Ra::Transform addPdfToScene(const void *bytes, size_t size, size_t pageIndex, Ra::SceneRef& scene) {
        Ra::Transform ctm;
        FPDF_LIBRARY_CONFIG config;
            config.version = 3;
            config.m_pUserFontPaths = nullptr;
            config.m_pIsolate = nullptr;
            config.m_v8EmbedderSlot = 0;
            config.m_pPlatform = nullptr;
        FPDF_InitLibraryWithConfig(&config);
        
        FPDF_DOCUMENT doc = FPDF_LoadMemDocument(bytes, int(size), NULL);
        if (doc) {
            int count = FPDF_GetPageCount(doc);
            if (count > 0) {
                pageIndex = pageIndex > count - 1 ? count - 1 : pageIndex;
                FPDF_PAGE page = FPDF_LoadPage(doc, int(pageIndex));
                FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);
                int charCount = FPDFText_CountChars(text_page);
                int objectCount = FPDFPage_CountObjects(page);
                
                ctm = transformForPage(page);
                
                CharMap charMap;
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
               
                Ra::Bounds clipBounds, *clipPtr = nullptr;
                std::vector<Ra::Path> clipPaths;
                Ra::Bounds unitBounds(0, 0, 1, 1);
                Ra::Path unitRectPath;  unitRectPath->addBounds(unitBounds);
                size_t lastHash = ~0;
                float x, y;
                for (int i = 0; i < objectCount; i++) {
                    FPDF_PAGEOBJECT pageObject = FPDFPage_GetObject(page, i);
                    FS_MATRIX m;
                    FPDFPageObj_GetMatrix(pageObject, & m);
                        
                    size_t hash = 0;
                    FPDF_CLIPPATH clipPath = FPDFPageObj_GetClipPath(pageObject);
                    int clipCount = FPDFClipPath_CountPaths(clipPath);
                    if (clipCount != -1) {
                        hash = XXH64(& clipCount, sizeof(clipCount), hash);
                        for (int j = 0; j < clipCount; j++) {
                            int segmentCount = FPDFClipPath_CountPathSegments(clipPath, j);
                            hash = XXH64(& segmentCount, sizeof(segmentCount), hash);
                            for (int k = 0; k < 1; k++) {
                                FPDF_PATHSEGMENT segment = FPDFClipPath_GetPathSegment(clipPath, j, k);
                                FPDFPathSegment_GetPoint(segment, & x, & y);
                                hash = XXH64(& x, sizeof(x), hash), hash = XXH64(& y, sizeof(y), hash);
                            }
                        }
                    }
                    if (lastHash != hash) {
                        lastHash = hash, clipPtr = nullptr, clipBounds = Ra::Bounds::huge(), clipPaths.resize(0);
                        if (clipCount != -1) {
                            for (int j = 0; j < clipCount; j++) {
                                Ra::Path clip = PathWriter().createPathFromClipPath(clipPath, j);
                                clipPaths.emplace_back(clip);
                                if (clip->isValid())
                                    clipBounds = clipBounds.intersect(clip->bounds);
                            }
                            clipPtr = & clipBounds;
                        }
                    }
                    switch (FPDFPageObj_GetType(pageObject)) {
                        case FPDF_PAGEOBJ_TEXT: {
                            writeTextToScene(pageObject, text_page, charMap, m, clipPtr, scene);
                            break;
                        }
                        case FPDF_PAGEOBJ_PATH:
                            writePathToScene(pageObject, m, clipPtr, clipPaths, scene);
                            break;
                        case FPDF_PAGEOBJ_IMAGE: {
                            FPDF_IMAGEOBJ_METADATA metadata;
                            FPDFImageObj_GetImageMetadata(pageObject, page, & metadata);
                            Ra::Transform ctm = Ra::Transform(m.a, m.b, m.c, m.d, m.e, m.f);
                            unsigned long size = FPDFImageObj_GetImageDataRaw(pageObject, NULL, INT_MAX);
                            if (size) {
                                FPDF_BITMAP bitmap = FPDFImageObj_GetRenderedBitmap(doc, page, pageObject);
                                int format = FPDFBitmap_GetFormat(bitmap);
                                if (format) {
                                    scene->addPath(unitRectPath, ctm, Ra::BGRA(0, 0, 0, 64), 0, 0, clipPtr);
                                }
                                FPDFBitmap_Destroy(bitmap);
                            }
                            break;
                        }
                        case FPDF_PAGEOBJ_SHADING: {
                            if (clipPaths.size())
                                scene->addPath(clipPaths[0], Ra::Transform(), Ra::BGRA(0, 0, 255, 64), 0, 0, clipPtr);
                            break;
                        }
                        default:
                            break;
                    }
                }
                FPDFText_ClosePage(text_page);
                FPDF_ClosePage(page);
            }
            FPDF_CloseDocument(doc);
        }
        FPDF_DestroyLibrary();
        return ctm;
    }
};
