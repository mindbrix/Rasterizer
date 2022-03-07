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


struct RasterizerPDF {
    static inline Ra::Colorant colorFromSVGColor(int color) {
        return Ra::Colorant((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, color >> 24);
    }
    static void writePathFromObject(FPDF_PAGEOBJECT pageObject, Ra::Path& p) {
        int segmentCount = FPDFPath_CountSegments(pageObject);
        if (segmentCount > 0) {
            float x, y;
            std::vector<float> bezier;
            
            for (int j = 0; j < segmentCount; j++) {
                FPDF_PATHSEGMENT segment = FPDFPath_GetPathSegment(pageObject, j);
                int segmentType = FPDFPathSegment_GetType(segment);
                FPDF_BOOL success = FPDFPathSegment_GetPoint(segment, & x, & y);
                if (success == 0)
                    continue;
                switch (segmentType) {
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
                FPDF_BOOL close = FPDFPathSegment_GetClose(segment);
                if (close)
                    p->close();
            }
        }
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
                Ra::Scene scene;
                int objectCount = FPDFPage_CountObjects(page);
                for (int i = 0; i < objectCount; i++) {
                    FPDF_PAGEOBJECT pageObject = FPDFPage_GetObject(page, i);
                    int type = FPDFPageObj_GetType(pageObject);
                    if (type == FPDF_PAGEOBJ_PATH) {
                        int segmentCount = FPDFPath_CountSegments(pageObject);
                        if (segmentCount > 0) {
                            Ra::Path path;
                            writePathFromObject(pageObject, path);
                            
                            int fillmode;
                            FPDF_BOOL stroke;
                            FPDF_BOOL gotMode = FPDFPath_GetDrawMode(pageObject, & fillmode, & stroke);
                            
                            float width;
                            FPDF_BOOL gotWidth = FPDFPageObj_GetStrokeWidth(pageObject, & width);
                            
                            unsigned int R, G, B, A;
                            FPDF_BOOL gotColor;
                            if (stroke)
                                gotColor = FPDFPageObj_GetStrokeColor(pageObject, & R, & G, & B, & A);
                            else
                                gotColor = FPDFPageObj_GetFillColor(pageObject, & R, & G, & B, & A);
                            
                            FS_MATRIX m;
                            FPDF_BOOL ok = FPDFPageObj_GetMatrix(pageObject, & m);
                            Ra::Transform ctm = ok ? Ra::Transform(m.a, m.b, m.c, m.d, m.e, m.f) : Ra::Transform();
                            scene.addPath(path, ctm, Ra::Colorant(B, G, R, A), stroke ? width : 0.f, fillmode == 1 ? Rasterizer::Scene::kFillEvenOdd : 0);
                        }
                    }
                }
                list.addScene(scene);
                FPDF_ClosePage(page);
            }
            FPDF_CloseDocument(doc);
        }
        FPDF_DestroyLibrary();
    }
};
