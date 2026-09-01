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
                
                ctm = transformForPage(page);
                if (0) {
                    Ra::Paint paint = paintFromPage(page);
                    Ra::Bounds bounds(0, 0, paint.w, paint.h);
                    Ra::Path path;  path->addBounds(bounds);
                    scene->addPath(path, Ra::Transform(), paint, 0, 0);
                } else
                    writePageToScene(doc, page, scene);
                
                FPDF_ClosePage(page);
            }
            FPDF_CloseDocument(doc);
        }
        FPDF_DestroyLibrary();
        return ctm;
    }
    
    static void writePageToScene(FPDF_DOCUMENT doc, FPDF_PAGE page, Ra::SceneRef& scene) {
        FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);
        CharMap charMap;
        writeCharMap(text_page, charMap);

        ClipState clipState;
        
        FS_MATRIX m;
        Ra::Transform ctm;
        int objectCount = FPDFPage_CountObjects(page);

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
                    writeShadingToScene(page, page_object, clipState.clipPtr, clipState.clipPaths, scene);
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
            float width = 0.f;
            uint8_t flags = 0;
            if (stroke) {
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
            } else {
                FPDFPageObj_GetFillColor(pageObject, & R, & G, & B, & A);
                if (path->isRect())
                    for (auto clip : clipPaths)
                        if (!clip->isRect())
                            path = clip, clipPath = nullptr;
                flags |= fillmode == FPDF_FILLMODE_ALTERNATE ? Ra::Draw::kFillEvenOdd : 0;
            }
            scene->addPath(path, ctm, Ra::Color(B, G, R, A), width, flags, clipBounds, clipPath);
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
    
    static void writeShadingToScene(FPDF_PAGE page, FPDF_PAGEOBJECT page_object, Ra::Bounds* clipBounds, std::vector<Ra::Path>& clipPaths, Ra::SceneRef& scene) {
        if (clipPaths.size()) {
            auto paint = paintFromPageObject(page, page_object);
            scene->addPath(clipPaths[0], Ra::Transform(), paint, 0, 0, clipBounds);
        }
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
