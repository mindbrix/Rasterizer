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
    
    static void writeTextToScene(FPDF_PAGEOBJECT pageObject, FPDF_TEXTPAGE text_page, Ra::Transform ctm, Ra::Scene& scene) {
        unsigned long size = FPDFTextObj_GetText(pageObject, text_page, nullptr, 0);
        char16_t buffer[size];
        bzero(buffer, sizeof(buffer[0]) * size);
        FPDFTextObj_GetText(pageObject, text_page, (FPDF_WCHAR *)buffer, size);
        while (buffer[size - 1] == 0 && size > 0)
            size--;

        FPDF_WCHAR tfl[] = { 'e', 'f', 'f' };
        if (size > 2 && memcmp(buffer, tfl, sizeof(tfl)) == 0) {
            int z = 0;
        }
        float fontSize = 1.f, ascent = 0.f, descent = 0.f, tx = 0.f, width = 0.f;
        FPDFTextObj_GetFontSize(pageObject, & fontSize);
        FPDF_FONT font = FPDFTextObj_GetFont(pageObject);
        assert(font);
        unsigned int R = 0, G = 0, B = 0, A = 255;
        FPDFPageObj_GetFillColor(pageObject, & R, & G, & B, & A);
        FPDFFont_GetAscent(font, fontSize, & ascent);
        FPDFFont_GetDescent(font, fontSize, & descent);
        
        int charIndex = -1;
        for (int g = 0; g < size; g++) {
            auto glyph = buffer[g];
            assert((glyph & ~0x7FFFF) == 0);
            FPDF_GLYPHPATH path = FPDFFont_GetGlyphPath(font, glyph, fontSize);
            Ra::Path p;
            PathWriter().writePathFromGlyphPath(path, p);
            if (charIndex == -1 && path) {
                if (!FPDFFont_GetGlyphWidth(font, glyph, fontSize, & width))
                    width = 1.f;
                Ra::Bounds b0 = Ra::Bounds(0.f, descent, width, ascent).unit(ctm);
                float sx = 0.5f * (b0.lx + b0.ux), sy = 0.5f * (b0.ly + b0.uy);
                float sw = 0.5f * (b0.ux - b0.lx), sh = 0.5f * (b0.uy - b0.ly);
                float dx = width * ctm.a, dy = width * ctm.b;
                for (int j = 0; j < 3 && charIndex == -1; j++, sx += dx, sy += dy) {
                    charIndex = FPDFText_GetCharIndexAtPos(text_page, sx, sy, sw, sh);
                    if (charIndex != -1) {
                        char32_t unicode = FPDFText_GetUnicode(text_page, charIndex);
                        if (glyph != unicode)
                            charIndex = -1;
                    }
                }
                if (charIndex != -1) {
                    if (g > 0) {
                        charIndex -= g;
                    }
                }
                if (charIndex == -1 && glyph > 32) {
//                    assert(g > 0);
                    fprintf(stderr, "Not found\n");
                }
            }
            if (glyph > 32) {
                Ra::Transform textCTM = ctm;
                double left = 0, bottom = 0, right = 0, top = 0;
                if (charIndex != -1 && FPDFText_GetCharBox(text_page, charIndex + g, & left, & right, & bottom, & top)) {
                    Ra::Bounds b = p->bounds.unit(ctm);
                    textCTM.tx += left - b.lx;
                    textCTM.ty += bottom - b.ly;
                } else {
                    textCTM = textCTM.concat(Ra::Transform(1, 0, 0, 1, tx, 0));
                }
                scene.addPath(p, textCTM, Ra::Colorant(B, G, R, A), 0.f, 0);
            }
            if (FPDFFont_GetGlyphWidth(font, glyph, fontSize, & width))
                tx += width;
        }
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
    
    static inline Ra::Transform transformForPage(FPDF_PAGE page, Ra::Scene& scene) {
        Ra::Bounds b = scene.bounds().integral();
        float left = b.lx, bottom = b.ly, right = b.ux, top = b.uy, tx = 0.f, ty = 0.f, sine = 0.f, cosine = 1.f;
        FPDFPage_GetMediaBox(page, & left, & bottom, & right, & top);
        int rotation = FPDFPage_GetRotation(page);
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
                        
                    switch (FPDFPageObj_GetType(pageObject)) {
                        case FPDF_PAGEOBJ_TEXT:
                            writeTextToScene(pageObject, text_page, ctm, scene);
                            break;
                        case FPDF_PAGEOBJ_PATH:
                            writePathToScene(pageObject, ctm, scene);
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
