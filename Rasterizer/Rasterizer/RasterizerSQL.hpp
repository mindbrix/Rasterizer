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
        int rowCount(const char *table) {
            int count = INT_MAX;
            const char *select = "SELECT COUNT(*) FROM ";
            char sql[strlen(select) + strlen(table) + 1];
            sql[0] = 0, strcat(sql, select), strcat(sql, table);
            sqlite3_stmt *pStmt;
            if (sqlite3_prepare_v2(db, sql, -1, & pStmt, NULL) == SQLITE_OK && sqlite3_step(pStmt) == SQLITE_ROW)
                count = sqlite3_column_int(pStmt, 0);
            sqlite3_finalize(pStmt);
            return count;
        }
        Rasterizer::Bounds writeQuery(RasterizerTrueType::Font& font, float size, int columnSpaces, Rasterizer::Bounds frame, const char *table, Rasterizer::Scene& scene) {
            int count = rowCount(table);
            const char *select = "SELECT * FROM ";
            char sql[strlen(select) + strlen(table) + 1];
            sql[0] = 0, strcat(sql, select), strcat(sql, table);
            sqlite3_stmt *pStmt;
            if (sqlite3_prepare_v2(db, sql, -1, & pStmt, NULL) == SQLITE_OK) {
                Rasterizer::Colorant red(0, 0, 255, 255), black(0, 0, 0, 255);
                Rasterizer::Bounds bounds = frame;
                float s = size / float(font.unitsPerEm), space = font.space * s * columnSpaces, lineHeight = (font.ascent - font.descent + font.lineGap) * s;
                int columns = sqlite3_column_count(pStmt);
                for (int i = 0; i < columns; i++) {
                    bounds.lx = frame.lx + i * space, bounds.ux = bounds.lx + space;
                    RasterizerTrueType::writeGlyphs(font, size, red, bounds, false, true, sqlite3_column_name(pStmt, i), scene);
                }
                for (int status = sqlite3_step(pStmt); status == SQLITE_ROW; status = sqlite3_step(pStmt)) {
                    bounds.uy -= lineHeight;
                    for (int i = 0; i < columns; i++) {
                        bounds.lx = frame.lx + i * space, bounds.ux = bounds.lx + space;
                        RasterizerTrueType::writeGlyphs(font, size, black, bounds, false, true, (const char *)sqlite3_column_text(pStmt, i), scene);
                    }
                }
                return Rasterizer::Bounds(frame.lx, bounds.uy - lineHeight, bounds.ux, frame.uy);
            }
            sqlite3_finalize(pStmt);
            return Rasterizer::Bounds(0.f, 0.f, 0.f, 0.f);
        }
        sqlite3 *db = nullptr;
    };
};
