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
        
        Ra::Path createPathFromClipPath(FPDF_CLIPPATH clipPath, int index) {
            Ra::Path p;  int segmentCount = FPDFClipPath_CountPathSegments(clipPath, index);
            for (int i = 0; i < segmentCount; i++)
                writeSegment(FPDFClipPath_GetPathSegment(clipPath, index, i), p);
            return p;
        }
        
        Ra::Path createPathFromGlyphPath(FPDF_GLYPHPATH path) {
            Ra::Path p;  int segmentCount = FPDFGlyphPath_CountGlyphSegments(path);
            for (int i = 0; i < segmentCount; i++)
                writeSegment(FPDFGlyphPath_GetGlyphPathSegment(path, i), p);
            return p;
        }
        
        Ra::Path createPathFromObject(FPDF_PAGEOBJECT pageObject) {
            Ra::Path p;  int segmentCount = FPDFPath_CountSegments(pageObject);
            for (int i = 0; i < segmentCount; i++)
                writeSegment(FPDFPath_GetPathSegment(pageObject, i), p);
            return p;
        }
        
        void writeSegment(FPDF_PATHSEGMENT segment, Ra::Path& p) {
            if (FPDFPathSegment_GetPoint(segment, & x, & y) == 0)
                return;
            switch (FPDFPathSegment_GetType(segment)) {
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
                        if (lengthsq(px, py, x, y) > 1e-4f) {
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

    static bool isRect(Ra::Path p) {
        float *pts = p->points.base, ax, ay, bx, by, t0, t1;
        if (p->types.end != 6 || p->counts[Ra::Geometry::kLine] != 4 || pts[0] != pts[10] || pts[1] != pts[11])
            return false;
        ax = pts[2] - pts[0], ay = pts[3] - pts[1], bx = pts[4] - pts[2], by = pts[5] - pts[3];
        t0 = (ax * bx + ay * by) / (ax * ax + ay * ay);
        ax = pts[6] - pts[4], ay = pts[7] - pts[5], bx = pts[8] - pts[6], by = pts[9] - pts[7];
        t1 = (ax * bx + ay * by) / (ax * ax + ay * ay);
        return fabsf(t0) < 1e-3f && fabsf(t1) < 1e-3f;
    }
    
    static void writeTextBoxesToScene(FPDF_TEXTPAGE text_page, Ra::Scene& scene) {
        int charCount = FPDFText_CountChars(text_page);
        double left = 0, bottom = 0, right = 0, top = 0;
        Ra::Transform unit;
        Ra::Colorant black(0, 0, 0, 64);
        for (int i = 0; i < charCount; i++) {
            if (FPDFText_GetUnicode(text_page, i) > 32 &&
                FPDFText_GetCharBox(text_page, i, & left, & right, & bottom, & top)) {
                Ra::Path path;
                path->addBounds(Ra::Bounds(left, bottom, right, top));
                scene.addPath(path, unit, black, -1.f, 0);
            }
        }
    }
    
   static void writeTextToScene(FPDF_PAGEOBJECT pageObject, FPDF_TEXTPAGE text_page, int baseIndex, char16_t *buffer, unsigned long textSize, FS_MATRIX m, Ra::Scene& scene) {
        float fontSize = 1.f;
        FPDFTextObj_GetFontSize(pageObject, & fontSize);
        FPDF_FONT font = FPDFTextObj_GetFont(pageObject);
        assert(font);
        unsigned int R = 0, G = 0, B = 0, A = 255;
        FPDFPageObj_GetFillColor(pageObject, & R, & G, & B, & A);

        for (int g = 0; g < textSize; g++) {
            auto glyph = buffer[g];
            assert((glyph & ~0x7FFFF) == 0);
            if (glyph > 32) {
                FPDF_GLYPHPATH path = FPDFFont_GetGlyphPath(font, glyph, fontSize);
                Ra::Path p = PathWriter().createPathFromGlyphPath(path);
                Ra::Transform textCTM = Ra::Transform(m.a, m.b, m.c, m.d, m.e, m.f);
                double left = 0, bottom = 0, right = 0, top = 0;
                FPDFText_GetCharBox(text_page, baseIndex + g, & left, & right, & bottom, & top);
                Ra::Bounds b = p->bounds.unit(textCTM);
                textCTM.tx += left - b.lx;
                textCTM.ty += bottom - b.ly;
                scene.addPath(p, textCTM, Ra::Colorant(B, G, R, A), 0.f, 0);
            }
        }
    }
    
    static void writePathToScene(FPDF_PAGEOBJECT pageObject, FS_MATRIX m, Ra::Scene& scene) {
        int fillmode;
        FPDF_BOOL stroke;
         
        if (FPDFPath_GetDrawMode(pageObject, & fillmode, & stroke)) {
            Ra::Path path = PathWriter().createPathFromObject(pageObject);
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
                        Ra::Path clip = PathWriter().createPathFromClipPath(clipPath, clipIndex);
                        if (isRect(path) && (path->bounds.contains(clip->bounds)))
                            path = clip;
                    }
                }
            }
            Ra::Transform ctm = Ra::Transform(m.a, m.b, m.c, m.d, m.e, m.f);
            uint8_t flags = 0;
            flags |= fillmode == FPDF_FILLMODE_ALTERNATE ? Ra::Scene::kFillEvenOdd : 0;
            flags |= cap == FPDF_LINECAP_ROUND ? Ra::Scene::kRoundCap : 0;
            flags |= cap == FPDF_LINECAP_PROJECTING_SQUARE ? Ra::Scene::kSquareCap : 0;
            scene.addPath(path, ctm, Ra::Colorant(B, G, R, A), width, flags);
        }
    }
    
    static inline Ra::Transform transformForPage(FPDF_PAGE page, Ra::Scene& scene) {
        Ra::Bounds b = scene.bounds().integral();
        float left = b.lx, bottom = b.ly, right = b.ux, top = b.uy, tx = 0.f, ty = 0.f, sine = 0.f, cosine = 1.f;
        FPDFPage_GetMediaBox(page, & left, & bottom, & right, & top);
        int rot = FPDFPage_GetRotation(page);
        __sincosf(-rot * 0.5f * M_PI, & sine, & cosine);
        tx = rot == 2 ? right - left : rot == 3 ? top - bottom : tx;
        ty = rot == 1 ? right - left : rot == 2 ? top - bottom : ty;
        Ra::Transform originCTM(1.f, 0.f, 0.f, 1.f, -left, -bottom);
        Ra::Transform pageCTM(cosine, sine, -sine, cosine, tx, ty);
        return originCTM.concat(pageCTM);
    }
    
    static int indexForTextCTM(FS_MATRIX m, FPDF_TEXTPAGE text_page, int start, int end, float *xs, float *ys) {
        for (int i = start; i < end; i++)
            if (m.e == xs[i] && m.f == ys[i])
                return i;
        return -1;
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
                int charCount = FPDFText_CountChars(text_page);
                int objectCount = FPDFPage_CountObjects(page);
                char16_t text[4096], *back;
                float xs[charCount], ys[charCount];
                bzero(xs, sizeof(xs)), bzero(ys, sizeof(xs));
                FS_MATRIX m;
                
                for (int i = 0; i < charCount; i++)
                    if (FPDFText_GetMatrix(text_page, i, & m))
                        xs[i] = m.e, ys[i] = m.f;
                
                for (int i = 0; i < objectCount; i++) {
                    FPDF_PAGEOBJECT pageObject = FPDFPage_GetObject(page, i);
                    FPDFPageObj_GetMatrix(pageObject, & m);
                        
                    switch (FPDFPageObj_GetType(pageObject)) {
                        case FPDF_PAGEOBJ_TEXT: {
                            unsigned long size0 = FPDFTextObj_GetText(pageObject, text_page, (FPDF_WCHAR *)text, 4096);
                            
                            back = text, size = 0;
                            while (size < size0 && *back > 0)
                                back++, size++;
                            back = text + size - 1;
                            while (size && *back < 33)
                                *back-- = 0, size--;
                            
                            if (size > 0) {
                                int index = indexForTextCTM(m, text_page, 0, charCount, xs, ys);
                                assert(index != -1);
                                
                                writeTextToScene(pageObject, text_page, index, text, size, m, scene);
                            }
                            break;
                        }
                        case FPDF_PAGEOBJ_PATH:
                            writePathToScene(pageObject, m, scene);
                            break;
                        default:
                            break;
                    }
                }
                
                writeTextBoxesToScene(text_page, scene);
                list.addScene(scene, transformForPage(page, scene));
                
                FPDFText_ClosePage(text_page);
                FPDF_ClosePage(page);
            }
            FPDF_CloseDocument(doc);
        }
        FPDF_DestroyLibrary();
    }
};
