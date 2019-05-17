//
//  RasterizerSQL.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 29/04/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//

#import "Rasterizer.hpp"
#import "RasterizerTrueType.hpp"
#import <sqlite3.h>

struct RasterizerSQL {
    struct DB {
        ~DB() { close(); }
        void open(const char *filename) {
            sqlite3_open(filename, & db);
        }
        void close() {
            sqlite3_close(db);
            db = nullptr;
        }
        Rasterizer::Bounds writeQuery(RasterizerTrueType::Font& font, float size, int columnSpaces, Rasterizer::Bounds frame, const char *sql, Rasterizer::Scene& scene) {
            sqlite3_stmt *pStmt;
            int status = sqlite3_prepare_v2(db, sql, -1, & pStmt, NULL);
            if (status == SQLITE_OK) {
                Rasterizer::Colorant red(0, 0, 255, 255), black(0, 0, 0, 255);
                Rasterizer::Bounds glyphs, bounds = frame;
                float s = size / float(font.unitsPerEm), space = font.space * s * columnSpaces, lineHeight = (font.ascent - font.descent + font.lineGap) * s;
                int columns = sqlite3_column_count(pStmt);
                for (int i = 0; i < columns; i++) {
                    bounds.lx = frame.lx + i * space, bounds.ux = bounds.lx + space;
                    glyphs = RasterizerTrueType::writeGlyphs(font, size, red, bounds, false, true, sqlite3_column_name(pStmt, i), scene);
                }
                do {
                    bounds.uy -= lineHeight;
                    status = sqlite3_step(pStmt);
                    if (status == SQLITE_ROW) {
                        for (int i = 0; i < columns; i++) {
                            int type = sqlite3_column_type(pStmt, i);
                            bounds.lx = frame.lx + i * space, bounds.ux = bounds.lx + space;
                            glyphs = RasterizerTrueType::writeGlyphs(font, size, black, bounds, false, true, (const char *)sqlite3_column_text(pStmt, i), scene);
                        }
                    }
                } while (status == SQLITE_ROW);
                return Rasterizer::Bounds(frame.lx, bounds.uy - lineHeight, bounds.ux, frame.uy);
            }
            sqlite3_finalize(pStmt);
            return Rasterizer::Bounds(0.f, 0.f, 0.f, 0.f);
        }
        sqlite3 *db = nullptr;
    };
};
