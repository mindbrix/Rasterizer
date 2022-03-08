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
    
    static void writePathFromGlyphPath(FPDF_GLYPHPATH path, Ra::Path& p) {
        PathWriter writer;
        int segmentCount = FPDFGlyphPath_CountGlyphSegments(path);
        for (int j = 0; j < segmentCount; j++) {
            FPDF_PATHSEGMENT segment = FPDFGlyphPath_GetGlyphPathSegment(path, j);
            writer.writeSegment(segment, p);
        }
    }
    
    static void writePathFromObject(FPDF_PAGEOBJECT pageObject, Ra::Path& p) {
        PathWriter writer;
        int segmentCount = FPDFPath_CountSegments(pageObject);
        for (int j = 0; j < segmentCount; j++) {
            FPDF_PATHSEGMENT segment = FPDFPath_GetPathSegment(pageObject, j);
            writer.writeSegment(segment, p);
        }
    }
    
    static inline Ra::Transform transformFromMatrix(FS_MATRIX m) {
        return Ra::Transform(m.a, m.b, m.c, m.d, m.e, m.f);
    }
    
    static void writeScene(const void *bytes, size_t size, Ra::SceneList& list) {
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
                FPDF_PAGE page = FPDF_LoadPage(doc, 0);
                FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);
                
                int rotation = FPDFPage_GetRotation(page);
                float left, bottom, right, top, tx = 0.f, ty = 0.f, sine = 0.f, cosine = 1.f;
                if (rotation != 0 && FPDFPage_GetMediaBox(page, & left, & bottom, & right, & top)) {
                    __sincosf(rotation * -0.5f * M_PI, & sine, & cosine);
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
                    }
                }
                Ra::Transform pageCTM(cosine, sine, -sine, cosine, tx, ty);
                std::vector<char16_t> buffer;
                Ra::Scene scene;
                int objectCount = FPDFPage_CountObjects(page);
                
                for (int i = 0; i < objectCount; i++) {
                    FPDF_PAGEOBJECT pageObject = FPDFPage_GetObject(page, i);
                    int type = FPDFPageObj_GetType(pageObject);
                    if (type == FPDF_PAGEOBJ_TEXT) {
                        unsigned long size = FPDFTextObj_GetText(pageObject, text_page, nullptr, 0);
                        buffer.resize(0), buffer.resize(size);
                        bzero(buffer.data(), sizeof(buffer[0]) * size);
                        FPDFTextObj_GetText(pageObject, text_page, (FPDF_WCHAR *)buffer.data(), size);
                        
                        Ra::Transform ctm;
                        FS_MATRIX m;
                        if (FPDFPageObj_GetMatrix(pageObject, & m))
                            ctm = transformFromMatrix(m);
                        
                        float fontSize = 1.f, tx = 0.f, width = 0.f;
                        FPDFTextObj_GetFontSize(pageObject, & fontSize);
                        FPDF_FONT font = FPDFTextObj_GetFont(pageObject);
                        
                        unsigned int R = 0, G = 0, B = 0, A = 255;
                        FPDFPageObj_GetFillColor(pageObject, & R, & G, & B, & A);
                        
                        for (auto glyph : buffer) {
                            FPDF_GLYPHPATH path = FPDFFont_GetGlyphPath(font, glyph, fontSize);
                            Ra::Path p;
                            writePathFromGlyphPath(path, p);
                            
                            Ra::Transform m = ctm.concat(Ra::Transform(1, 0, 0, 1, tx, 0));
                            scene.addPath(p, m, Ra::Colorant(B, G, R, A), 0.f, 0);
                            
                            if (FPDFFont_GetGlyphWidth(font, glyph, fontSize, & width))
                                tx += width;
                        }
                        
                    } else if (type == FPDF_PAGEOBJ_PATH) {
                        if (FPDFPath_CountSegments(pageObject) > 0) {
                            Ra::Path path;
                            writePathFromObject(pageObject, path);
                            
                            int fillmode;
                            FPDF_BOOL stroke;
                             
                            if (FPDFPath_GetDrawMode(pageObject, & fillmode, & stroke)) {
                                float width = 0.f;
                                int cap = 0;
                                unsigned int R = 0, G = 0, B = 0, A = 255;
                                if (stroke) {
                                    FPDFPageObj_GetStrokeColor(pageObject, & R, & G, & B, & A);
                                    FPDFPageObj_GetStrokeWidth(pageObject, & width);
                                    cap = FPDFPageObj_GetLineCap(pageObject);
                                } else
                                    FPDFPageObj_GetFillColor(pageObject, & R, & G, & B, & A);
                                
                                Ra::Transform ctm;
                                FS_MATRIX m;
                                if (FPDFPageObj_GetMatrix(pageObject, & m))
                                    ctm = transformFromMatrix(m);
                                
                                uint8_t flags = 0;
                                flags |= fillmode == 1 ? Rasterizer::Scene::kFillEvenOdd : 0;
                                flags |= cap == FPDF_LINECAP_ROUND ? Rasterizer::Scene::kRoundCap : 0;
                                flags |= cap == FPDF_LINECAP_PROJECTING_SQUARE ? Rasterizer::Scene::kSquareCap : 0;
                                scene.addPath(path, ctm, Ra::Colorant(B, G, R, A), width, flags);
                            }
                        }
                    }
                }
                list.addScene(scene, pageCTM);
                FPDFText_ClosePage(text_page);
                FPDF_ClosePage(page);
            }
            FPDF_CloseDocument(doc);
        }
        FPDF_DestroyLibrary();
    }
};
