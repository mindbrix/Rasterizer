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
    
    static int indexForGlyph(char16_t glyph, FPDF_FONT font, float fontSize, FPDF_TEXTPAGE text_page, Ra::Transform ctm) {
        int index = -1;
        float ascent = 0.f, descent = 0.f, width = 1.f;
        FPDFFont_GetAscent(font, fontSize, & ascent);
        FPDFFont_GetDescent(font, fontSize, & descent);
        FPDFFont_GetGlyphWidth(font, glyph, fontSize, & width);
        Ra::Bounds b0 = Ra::Bounds(0.f, descent, width, ascent).unit(ctm);
        float x = 0.5f * (b0.lx + b0.ux), y = 0.5f * (b0.ly + b0.uy);
        float sw = 0.5f * (b0.ux - b0.lx), sh = 0.5f * (b0.uy - b0.ly);
        float dx = width * ctm.a, dy = width * ctm.b;
        
        for (int j = 0; j < 10 && index == -1; j++) {
            index = FPDFText_GetCharIndexAtPos(text_page, x + j * dx, y + j * dy, sw, sh);
            if (index != -1) {
                char32_t unicode = FPDFText_GetUnicode(text_page, index);
                if (glyph != unicode)
                    index = -1;
            }
        }
        return index;
    }
    
    static void writeTextToScene(FPDF_PAGEOBJECT pageObject, FPDF_TEXTPAGE text_page, std::vector<char16_t>& chars, int charIndex, unsigned long textSize, Ra::Transform ctm, Ra::Scene& scene) {
//        if (textSize == 0)
//            return;
//        char16_t *buffer = & chars[charIndex];
//        unsigned long size = textSize;
        unsigned long size = FPDFTextObj_GetText(pageObject, text_page, nullptr, 0);
        char16_t buffer[size];
        bzero(buffer, sizeof(buffer[0]) * size);
        FPDFTextObj_GetText(pageObject, text_page, (FPDF_WCHAR *)buffer, size);
        while (size > 0 && buffer[size - 1] < 33)
            size--;
        
        float fontSize = 1.f, tx = 0.f, width = 0.f;
        FPDFTextObj_GetFontSize(pageObject, & fontSize);
        FPDF_FONT font = FPDFTextObj_GetFont(pageObject);
        assert(font);
        unsigned int R = 0, G = 0, B = 0, A = 255;
        FPDFPageObj_GetFillColor(pageObject, & R, & G, & B, & A);

        for (int g = 0; g < size; g++) {
            auto glyph = buffer[g];
            assert((glyph & ~0x7FFFF) == 0);
            FPDF_GLYPHPATH path = FPDFFont_GetGlyphPath(font, glyph, fontSize);
            if (charIndex == -1 && path) {
                charIndex = indexForGlyph(glyph, font, fontSize, text_page, ctm);
                if (charIndex != -1)
                    charIndex -= g;
                
                if (charIndex == -1 && glyph > 32) {
                    indexForGlyph(glyph, font, fontSize, text_page, ctm);
                    fprintf(stderr, "Not found\n");
                }
            }
            if (glyph > 32) {
                Ra::Path p;
                PathWriter().writePathFromGlyphPath(path, p);
                
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
    
    static int indexForTextCTM(FS_MATRIX ctm, FPDF_TEXTPAGE text_page, int start, int end) {
        FS_MATRIX m;
        int index = -1;
        for (int ch = start; index == -1 && ch < end; ch++)
            if (FPDFText_GetMatrix(text_page, ch, & m) && ctm.e == m.e && ctm.f == m.f)
                index = ch;
            
        return index;
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
                
                int charCount = FPDFText_CountChars(text_page);
                std::vector<char16_t> chars(charCount == 0 ? 0 : charCount + 1);
                int textCount = FPDFText_GetText(text_page, 0, charCount, (unsigned short *)chars.data());
                assert(textCount <= chars.size());
                
                Ra::Scene scene;
                int objectCount = FPDFPage_CountObjects(page);
                int textIndices[objectCount];
                bool textIndexFound[charCount];
                bzero(textIndexFound, sizeof(textIndexFound));
                char16_t buffer[charCount + 1];
                bzero(buffer, sizeof(buffer));
                
                int start = 0;
                FS_MATRIX m;
                for (int i = 0; i < objectCount; i++) {
                    textIndices[i] = -1;
//                    for (; start < charCount && textIndexFound[start]; start++)
//                        ;
                    FPDF_PAGEOBJECT pageObject = FPDFPage_GetObject(page, i);
                    if (FPDFPageObj_GetType(pageObject) == FPDF_PAGEOBJ_TEXT && FPDFPageObj_GetMatrix(pageObject, & m)) {
                        textIndices[i] = indexForTextCTM(m, text_page, start, charCount);
                        if (textIndices[i] != -1) {
                            unsigned long size = FPDFTextObj_GetText(pageObject, text_page, nullptr, 0);
                            for (int j = textIndices[i]; j < textIndices[i] + size && j < charCount; j++)
                                textIndexFound[j] = true;
                        }
                    }
                }
                
                Ra::Transform ctm;
                for (int i = 0; i < objectCount; i++) {
                    FPDF_PAGEOBJECT pageObject = FPDFPage_GetObject(page, i);
                    
                    if (FPDFPageObj_GetMatrix(pageObject, & m))
                        ctm = Ra::Transform(m.a, m.b, m.c, m.d, m.e, m.f);
                        
                    switch (FPDFPageObj_GetType(pageObject)) {
                        case FPDF_PAGEOBJ_TEXT:
                            writeTextToScene(pageObject, text_page, chars, textIndices[i], 0, ctm, scene);
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
