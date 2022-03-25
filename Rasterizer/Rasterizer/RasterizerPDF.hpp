//
//  RasterizerPDF.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 07/03/2022.
//  Copyright © 2022 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"
#import "xxhash.h"
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
            FPDFPathSegment_GetPoint(segment, & x, & y);
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
            if (FPDFPathSegment_GetClose(segment))
                p->close();
        }
    };
    struct PointIndex {
        float x, y;  int index;
        inline bool operator< (const PointIndex& other) const { return x < other.x || (x == other.x && y < other.y) || (x == other.x && y == other.y && index < other.index); }
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

    static bool pathIsRect(Ra::Path p) {
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
        Ra::Colorant color(0, 0, 0, 64);
        Ra::Path rect;  rect->addBounds(Ra::Bounds(0, 0, 1, 1));
        for (int i = 0; i < charCount; i++)
            if (FPDFText_GetUnicode(text_page, i) > 32 && FPDFText_GetCharBox(text_page, i, & left, & right, & bottom, & top))
                scene.addPath(rect, Ra::Transform(right - left, 0, 0, top - bottom, left, bottom), color, -1.f, 0);
    }
    
   static void writeTextToScene(FPDF_PAGEOBJECT pageObject, FPDF_TEXTPAGE text_page, int baseIndex, char32_t *buffer, unsigned long textSize, FS_MATRIX m, Ra::Bounds clipBounds, Ra::Scene& scene) {
        float fontSize = 1.f;
        FPDFTextObj_GetFontSize(pageObject, & fontSize);
        FPDF_FONT font = FPDFTextObj_GetFont(pageObject);
        assert(font);
        unsigned int R = 0, G = 0, B = 0, A = 255;
        FPDFPageObj_GetFillColor(pageObject, & R, & G, & B, & A);
        double pleft = DBL_MAX;
        for (int g = 0; g < textSize; g++) {
            auto glyph = buffer[g];
            double left = 0, bottom = 0, right = 0, top = 0;
            FPDFText_GetCharBox(text_page, baseIndex + g, & left, & right, & bottom, & top);
            if (glyph >= 0xFFF0) {
                Ra::Path rect;  rect->addBounds(Ra::Bounds(0, 0, 1, 1));
                scene.addPath(rect, Ra::Transform(right - left, 0, 0, top - bottom, left, bottom), Ra::Colorant(0, 0, 255, 255), -1.f, 0);
            } else if (glyph > 32) {
                FPDF_GLYPHPATH path = FPDFFont_GetGlyphPath(font, glyph, fontSize);
                Ra::Path p = PathWriter().createPathFromGlyphPath(path);
                Ra::Transform textCTM = Ra::Transform(m.a, m.b, m.c, m.d, m.e, m.f);
                Ra::Bounds b = p->bounds.unit(textCTM);
                textCTM.tx += pleft == left ? right - b.ux : left - b.lx;
                textCTM.ty += bottom - b.ly;
                scene.addPath(p, textCTM, Ra::Colorant(B, G, R, A), 0.f, 0, clipBounds);
                pleft = left;
            }
        }
    }
    
    static void writePathToScene(FPDF_PAGEOBJECT pageObject, FS_MATRIX m, Ra::Bounds clipBounds, std::vector<Ra::Path>& clipPaths, Ra::Scene& scene) {
        int fillmode;
        FPDF_BOOL stroke;
         
        if (FPDFPath_GetDrawMode(pageObject, & fillmode, & stroke)) {
            Ra::Path path = PathWriter().createPathFromObject(pageObject);
            Ra::Transform ctm = Ra::Transform(m.a, m.b, m.c, m.d, m.e, m.f);
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
                
                if (pathIsRect(path))
                    for (auto clip : clipPaths)
                        if (!pathIsRect(clip))
                            path = clip;
            }
            uint8_t flags = 0;
            flags |= fillmode == FPDF_FILLMODE_ALTERNATE ? Ra::Scene::kFillEvenOdd : 0;
            flags |= cap == FPDF_LINECAP_ROUND ? Ra::Scene::kRoundCap : 0;
            flags |= cap == FPDF_LINECAP_PROJECTING_SQUARE ? Ra::Scene::kSquareCap : 0;
            scene.addPath(path, ctm, Ra::Colorant(B, G, R, A), width, flags, clipBounds);
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
                char32_t text[4096], *back;
                std::vector<PointIndex> sortedPointIndices;
                FS_MATRIX m, lastm;  lastm.e = FLT_MAX, lastm.f = FLT_MAX;
                int *indices = (int *)malloc(objectCount * sizeof(*indices));
                std::vector<int> sortedIndices;
                
                for (int i = 0; i < charCount; i++)
                    if (FPDFText_GetMatrix(text_page, i, & m)) {
                        if (lastm.e != m.e || lastm.f != m.f) {
                            PointIndex pi;  pi.x = m.e, pi.y = m.f, pi.index = i;
                            sortedPointIndices.emplace_back(pi);
                        }
                        lastm = m;
                    }
                std::sort(sortedPointIndices.begin(), sortedPointIndices.end());
                
                Ra::Bounds unitBounds(0, 0, 1, 1);
                Ra::Path unitRectPath;  unitRectPath->addBounds(unitBounds);
                
                for (int i = 0; i < objectCount; i++) {
                    indices[i] = -1;
                    FPDF_PAGEOBJECT pageObject = FPDFPage_GetObject(page, i);
                    if (FPDFPageObj_GetType(pageObject) == FPDF_PAGEOBJ_TEXT) {
                        FPDFPageObj_GetMatrix(pageObject, & m);
                        PointIndex pi;  pi.x = m.e, pi.y = m.f, pi.index = 0;
                        auto it = std::lower_bound(sortedPointIndices.begin(), sortedPointIndices.end(), pi);
                        if (it != sortedPointIndices.end() && it->x == m.e && it->y == m.f)
                            indices[i] = it->index, sortedIndices.emplace_back(it->index);
                    }
                }
                std::sort(sortedIndices.begin(), sortedIndices.end());
                
                Ra::Bounds clipBounds;
                std::vector<Ra::Path> clipPaths;
                
                size_t lastHash = ~0;
                float x, y;
                for (int i = 0; i < objectCount; i++) {
                    FPDF_PAGEOBJECT pageObject = FPDFPage_GetObject(page, i);
                    FPDFPageObj_GetMatrix(pageObject, & m);
                        
                    size_t hash = 0;
                    FPDF_CLIPPATH clipPath = FPDFPageObj_GetClipPath(pageObject);
                    int clipCount = FPDFClipPath_CountPaths(clipPath);
                    if (clipCount != -1) {
                        hash = XXH64(& clipCount, sizeof(clipCount), hash);
                        for (int j = 0; j < clipCount; j++) {
                            int segmentCount = FPDFClipPath_CountPathSegments(clipPath, j);
                            assert(segmentCount);
                            hash = XXH64(& segmentCount, sizeof(segmentCount), hash);
                            for (int k = 0; k < 1; k++) {
                                FPDF_PATHSEGMENT segment = FPDFClipPath_GetPathSegment(clipPath, j, k);
                                FPDFPathSegment_GetPoint(segment, & x, & y);
                                hash = XXH64(& x, sizeof(x), hash), hash = XXH64(& y, sizeof(y), hash);
                            }
                        }
                    }
                    
                    if (hash != lastHash) {
                        clipBounds = Ra::Bounds::max();
                        clipPaths.resize(0);
                        
                        fprintf(stderr, "i = %d, hash = %ld\n", i, hash);
                        lastHash = hash;
                        if (clipCount != -1) {
                            for (int j = 0; j < clipCount; j++) {
                                int segmentCount = FPDFClipPath_CountPathSegments(clipPath, j);
                                Ra::Path clip = PathWriter().createPathFromClipPath(clipPath, j);
                                clipPaths.emplace_back(clip);
                                clipBounds = clipBounds.intersect(clip->bounds);
                                bool isRect = pathIsRect(clip);
                                fprintf(stderr, "\tj = %d, segmentCount = %d, isRect = %d\n", j, segmentCount, isRect);
                            }
                        }
                        fprintf(stderr, "\tBounds = %g, %g, %g, %g\n", clipBounds.lx, clipBounds.ly, clipBounds.ux, clipBounds.uy);
                    }
                    switch (FPDFPageObj_GetType(pageObject)) {
                        case FPDF_PAGEOBJ_TEXT: {
                            int length = 0, len;
                            if (indices[i] != -1) {
                                auto it = std::lower_bound(sortedIndices.begin(), sortedIndices.end(), indices[i]);
                                if (it != sortedIndices.end() && *it == indices[i]) {
                                    length = (it + 1 == sortedIndices.end() ? charCount : it[1]) - it[0];
                                    for (int j = 0; j < length; j++)
                                        text[j] = FPDFText_GetUnicode(text_page, it[0] + j);
                                    for (back = text, len = 0; len < length && *back >= 32; )
                                        back++, len++;
                                    for (back = text + len - 1; len && *back == 32; )
                                        *back-- = 0, len--;
                                    writeTextToScene(pageObject, text_page, indices[i], text, len, m, clipBounds, scene);
                                }
                            }
                            break;
                        }
                        case FPDF_PAGEOBJ_PATH:
                            writePathToScene(pageObject, m, clipBounds, clipPaths, scene);
                            break;
                        case FPDF_PAGEOBJ_IMAGE: {
                            FPDF_IMAGEOBJ_METADATA metadata;
                            FPDFImageObj_GetImageMetadata(pageObject, page, & metadata);
                            Ra::Transform ctm = Ra::Transform(m.a, m.b, m.c, m.d, m.e, m.f);
                            Ra::Image image;
                            unsigned long size = FPDFImageObj_GetImageDataRaw(pageObject, NULL, INT_MAX);
                            if (size) {
                                void *bytes = malloc(size);
                                FPDFImageObj_GetImageDataRaw(pageObject, bytes, size);
                                image.init(bytes, size, metadata.width, metadata.height);
                                free(bytes);
                                scene.addPath(unitRectPath, ctm, Ra::Colorant(0, 0, 0, 64), 0, 0, clipBounds, & image);
                            }
                            break;
                        }
                        case FPDF_PAGEOBJ_SHADING: {
                            if (clipPaths.size())
                                scene.addPath(clipPaths[0], Ra::Transform(), Ra::Colorant(0, 0, 255, 64), 0, 0, clipBounds);
                            break;
                        }
                        default:
                            break;
                    }
                }
                
//                writeTextBoxesToScene(text_page, scene);
                list.addScene(scene, transformForPage(page, scene));
                
                FPDFText_ClosePage(text_page);
                FPDF_ClosePage(page);
                free(indices);
            }
            FPDF_CloseDocument(doc);
        }
        FPDF_DestroyLibrary();
    }
};
