//
//  RasterizerSQL.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 29/04/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import <stdio.h>
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
            char *sql;
            asprintf(& sql, "SELECT COUNT(*) FROM %s", table);
            sqlite3_stmt *pStmt;
            int count = sqlite3_prepare_v2(db, sql, -1, & pStmt, NULL) == SQLITE_OK && sqlite3_step(pStmt) == SQLITE_ROW ? sqlite3_column_int(pStmt, 0) : INT_MAX;
            sqlite3_finalize(pStmt);
            free(sql);
            return count;
        }
        Rasterizer::Bounds writeTable(RasterizerTrueType::Font& font, float size, int columnSpaces, int rowSize, float t, Rasterizer::Bounds frame, const char *table, Rasterizer::SceneList& list) {
            Rasterizer::Bounds bnds(0.f, 0.f, 0.f, 0.f);
            int count = rowCount(table);
            char *sql;
            asprintf(& sql, "SELECT * FROM %s LIMIT %d", table, rowSize);
            sqlite3_stmt *pStmt;
            if (sqlite3_prepare_v2(db, sql, -1, & pStmt, NULL) == SQLITE_OK) {
                Rasterizer::Scene& header = list.addScene();
                Rasterizer::Colorant red(0, 0, 255, 255), black(0, 0, 0, 255);
                float s = size / float(font.unitsPerEm), columnWidth = font.space * s * columnSpaces, lineHeight = (font.ascent - font.descent + font.lineGap) * s;
                int columns = sqlite3_column_count(pStmt), rows = 1;
                for (int i = 0; i < columns; i++) {
                    Rasterizer::Bounds bounds = { frame.lx + i * columnWidth, frame.ly, frame.lx + (i + 1) * columnWidth, frame.uy };
                    RasterizerTrueType::writeGlyphs(font, size, red, bounds, false, true, sqlite3_column_name(pStmt, i), header);
                }
                for (int status = sqlite3_step(pStmt); status == SQLITE_ROW; status = sqlite3_step(pStmt), rows++) {
                    Rasterizer::Scene& scene = list.addScene();
                    for (int i = 0; i < columns; i++) {
                        Rasterizer::Bounds bounds = { frame.lx + i * columnWidth, frame.ly, frame.lx + (i + 1) * columnWidth, frame.uy - rows * lineHeight };
                        RasterizerTrueType::writeGlyphs(font, size, black, bounds, false, true, (const char *)sqlite3_column_text(pStmt, i), scene);
                    }
                }
                bnds = { frame.lx, frame.uy - rows * lineHeight, frame.lx + columns * columnWidth, frame.uy };
            }
            sqlite3_finalize(pStmt);
            free(sql);
            return bnds;
        }
        sqlite3 *db = nullptr;
    };
};
