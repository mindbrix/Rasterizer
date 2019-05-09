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
        void writeQuery(RasterizerTrueType::Font& font, float size, Rasterizer::Bounds frame, const char *sql, Rasterizer::Scene& scene) {
            sqlite3_stmt *pStmt;
            const char *zTail;
            int status = sqlite3_prepare(db, sql, (int)strlen(sql), & pStmt, & zTail);
            if (status == SQLITE_OK) {
                Rasterizer::Bounds glyphs, bounds = frame;
                float s = size / float(font.unitsPerEm), space = font.space * s, lineHeight = (font.ascent - font.descent + font.lineGap) * s;
                int columns = sqlite3_column_count(pStmt);
                for (int i = 0; i < columns; i++) {
                    const char *name = sqlite3_column_name(pStmt, i);
                    glyphs = RasterizerTrueType::writeGlyphs(font, size, Rasterizer::Colorant(0, 0, 0, 255), bounds, false, name, scene);
                    bounds.lx = glyphs.ux + space;
                    int type = sqlite3_column_type(pStmt, i);
                }
                do {
                    bounds.lx = frame.lx, bounds.uy -= lineHeight;
                    status = sqlite3_step(pStmt);
                    if (status == SQLITE_ROW) {
                        for (int i = 0; i < columns; i++) {
                            glyphs = RasterizerTrueType::writeGlyphs(font, size, Rasterizer::Colorant(0, 0, 0, 255), bounds, false, "{}", scene);
                            bounds.lx = glyphs.ux + space;
                        }
                    }
                } while (status == SQLITE_ROW);
            }
            sqlite3_finalize(pStmt);
        }
        sqlite3 *db = nullptr;
    };
};
