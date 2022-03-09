//
//  RasterizerPDF.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 07/03/2022.
//  Copyright © 2022 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"
#import "fpdfview.h"
#import "fpdf_edit.h"
#import "fpdf_transformpage.h"
#import "fpdf_text.h"


struct RasterizerPDF {
    struct PathWriter {
        float x, y, px = 0.f, py = 0.f;
        std::vector<float> bezier;
        
        int writePathFromClipPath(FPDF_CLIPPATH clipPath, int index, Ra::Path& p) {
            int segmentCount = FPDFClipPath_CountPathSegments(clipPath, index);
            for (int j = 0; j < segmentCount; j++) {
                FPDF_PATHSEGMENT segment = FPDFClipPath_GetPathSegment(clipPath, index, j);
                writeSegment(segment, p);
            }
            return segmentCount;
        }
        
        int writePathFromGlyphPath(FPDF_GLYPHPATH path, Ra::Path& p) {
            int segmentCount = FPDFGlyphPath_CountGlyphSegments(path);
            for (int j = 0; j < segmentCount; j++) {
                FPDF_PATHSEGMENT segment = FPDFGlyphPath_GetGlyphPathSegment(path, j);
                writeSegment(segment, p);
            }
            return segmentCount;
        }
        
        int writePathFromObject(FPDF_PAGEOBJECT pageObject, Ra::Path& p) {
            int segmentCount = FPDFPath_CountSegments(pageObject);
            for (int j = 0; j < segmentCount; j++) {
                FPDF_PATHSEGMENT segment = FPDFPath_GetPathSegment(pageObject, j);
                writeSegment(segment, p);
            }
            return segmentCount;
        }
        
        void writeSegment(FPDF_PATHSEGMENT segment, Ra::Path& p) {
            int segmentType = FPDFPathSegment_GetType(segment);
            FPDF_BOOL success = FPDFPathSegment_GetPoint(segment, & x, & y);
            if (success == 0)
                return;
            switch (segmentType) {
                case FPDF_SEGMENT_MOVETO:
                    p->moveTo(x, y);
                    px = x, py = y;
                    break;
                case FPDF_SEGMENT_LINETO:
                    p->lineTo(x, y);
                    px = x, py = y;
                    break;
                case FPDF_SEGMENT_BEZIERTO:
                    bezier.emplace_back(x);
                    bezier.emplace_back(y);
                    if (bezier.size() == 6) {
                        if (lengthsq(px, py, x, y) > 1e-9f) {
                            if (isLine(px, py, bezier[0], bezier[1], bezier[2], bezier[3], bezier[4], bezier[5]))
                                p->lineTo(x, y);
                            else
                                p->cubicTo(bezier[0], bezier[1], bezier[2], bezier[3], bezier[4], bezier[5]);
                            px = x, py = y;
                        }
                        bezier.clear();
                    }
                    break;
                default:
                    break;
            }
            FPDF_BOOL close = FPDFPathSegment_GetClose(segment);
            if (close)
                p->close();
        }
    };
    
    static inline bool isLine(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
        float ax, bx, cx, ay, by, cy, cdot, t0, t1;
        ax = x1 - x0, bx = x2 - x0, cx = x3 - x0;
        ay = y1 - y0, by = y2 - y0, cy = y3 - y0;
        cdot = cx * cx + cy * cy, t0 = (ax * -cy + ay * cx) / cdot, t1 = (bx * -cy + by * cx) / cdot;
        return fabsf(t0) < 1e-2f && fabsf(t1) < 1e-2f;
    }
    static inline float lengthsq(float x0, float y0, float x1, float y1) {
        return (x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0);
    }
    
    static void writePathToScene(FPDF_PAGEOBJECT pageObject, Ra::Transform ctm, Ra::Scene& scene) {
        int fillmode;
        FPDF_BOOL stroke;
         
        if (FPDFPath_GetDrawMode(pageObject, & fillmode, & stroke)) {
            Ra::Path path;
            PathWriter().writePathFromObject(pageObject, path);
            
            float width = 0.f;
            int cap = 0;
            unsigned int R = 0, G = 0, B = 0, A = 255;
            if (stroke) {
                FPDFPageObj_GetStrokeColor(pageObject, & R, & G, & B, & A);
                FPDFPageObj_GetStrokeWidth(pageObject, & width);
                width = width == 0.f ? -1.f : width;
                cap = FPDFPageObj_GetLineCap(pageObject);
            } else {
                FPDFPageObj_GetFillColor(pageObject, & R, & G, & B, & A);
                
                FPDF_CLIPPATH clipPath = FPDFPageObj_GetClipPath(pageObject);
                if (clipPath) {
                    int clipCount = FPDFClipPath_CountPaths(clipPath);
                    for (int clipIndex = 0; clipIndex < clipCount; clipIndex++) {
                        Ra::Path clip;
                        int segmentCount = PathWriter().writePathFromClipPath(clipPath, clipIndex, clip);
                        
                        if (segmentCount > 5) {
                            if (path->types.end == 6 && path->bounds.contains(clip->bounds)) {
                                path = clip;
                            }
                        }
                    }
                }
            }
            uint8_t flags = 0;
            flags |= fillmode == FPDF_FILLMODE_ALTERNATE ? Ra::Scene::kFillEvenOdd : 0;
            flags |= cap == FPDF_LINECAP_ROUND ? Ra::Scene::kRoundCap : 0;
            flags |= cap == FPDF_LINECAP_PROJECTING_SQUARE ? Ra::Scene::kSquareCap : 0;
            scene.addPath(path, ctm, Ra::Colorant(B, G, R, A), width, flags);
        }
    }
    
    static inline Ra::Transform transformForPage(FPDF_PAGE page, Ra::Bounds b) {
        int rotation = FPDFPage_GetRotation(page);
        float left = b.lx, bottom = b.ly, right = b.ux, top = b.uy, tx = 0.f, ty = 0.f, sine = 0.f, cosine = 1.f;
        FPDFPage_GetMediaBox(page, & left, & bottom, & right, & top);
        __sincosf(-rotation * 0.5f * M_PI, & sine, & cosine);
        switch (rotation) {
            case 1:
                ty = right - left;
                break;
            case 2:
                tx = right - left;
                ty = top - bottom;
                break;
            case 3:
                tx = top - bottom;
                break;
            default:
                break;
        }
        Ra::Transform originCTM(1.f, 0.f, 0.f, 1.f, -left, -bottom);
        Ra::Transform pageCTM(cosine, sine, -sine, cosine, tx, ty);
        return originCTM.concat(pageCTM);
    }
    
    static void writeScene(const void *bytes, size_t size, size_t pageIndex, Ra::SceneList& list) {
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
                
                Ra::Scene scene;
                int objectCount = FPDFPage_CountObjects(page);
                
                for (int i = 0; i < objectCount; i++) {
                    FPDF_PAGEOBJECT pageObject = FPDFPage_GetObject(page, i);
                    
                    Ra::Transform ctm;
                    FS_MATRIX m;
                    if (FPDFPageObj_GetMatrix(pageObject, & m))
                        ctm = Ra::Transform(m.a, m.b, m.c, m.d, m.e, m.f);
                        
                    int type = FPDFPageObj_GetType(pageObject);
                    if (type == FPDF_PAGEOBJ_TEXT) {
                        unsigned long size = FPDFTextObj_GetText(pageObject, text_page, nullptr, 0);
                        char16_t buffer[size];
                        bzero(buffer, sizeof(buffer[0]) * size);
                        FPDFTextObj_GetText(pageObject, text_page, (FPDF_WCHAR *)buffer, size);
                        
                        float fontSize = 1.f, tx = 0.f, width = 0.f;
                        FPDFTextObj_GetFontSize(pageObject, & fontSize);
                        FPDF_FONT font = FPDFTextObj_GetFont(pageObject);
                        assert(font);
                        unsigned int R = 0, G = 0, B = 0, A = 255;
                        FPDFPageObj_GetFillColor(pageObject, & R, & G, & B, & A);
                        
                        for (auto glyph : buffer) {
                            if (glyph > 31) {
                                FPDF_GLYPHPATH path = FPDFFont_GetGlyphPath(font, glyph, fontSize);
                                Ra::Path p;
                                PathWriter().writePathFromGlyphPath(path, p);
                                Ra::Transform m = ctm.concat(Ra::Transform(1, 0, 0, 1, tx, 0));
                                scene.addPath(p, m, Ra::Colorant(B, G, R, A), 0.f, 0);
                            }
                            if (FPDFFont_GetGlyphWidth(font, glyph, fontSize, & width))
                                tx += width;
                        }
                    } else if (type == FPDF_PAGEOBJ_PATH) {
                        writePathToScene(pageObject, ctm, scene);
                   }
                }
                Ra::Bounds b = scene.bounds().integral();
                Ra::Transform pageCTM = transformForPage(page, b);
                list.addScene(scene, pageCTM);
                
                FPDFText_ClosePage(text_page);
                FPDF_ClosePage(page);
            }
            FPDF_CloseDocument(doc);
        }
        FPDF_DestroyLibrary();
    }
};
