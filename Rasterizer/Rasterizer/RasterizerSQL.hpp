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
        static constexpr const char *kCreateFontsTable = "DROP TABLE Fonts; CREATE TABLE Fonts(name text, url text, display text, family text, style text);";
        static constexpr const char *kCreateViewsTable = "CREATE TABLE Views(Fonts_name text, table text, order smallint);";
        
        ~DB() { close(); }
        int open(const char *filename) {
            return sqlite3_open(filename, & db);
        }
        void close() {
            sqlite3_close(db);
            db = nullptr;
        }
        int beginTransaction() { return sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL); }
        int endTransaction() { return sqlite3_exec(db, "END TRANSACTION", NULL, NULL, NULL); }
        int exec(const char *sql) { return sqlite3_exec(db, sql, NULL, NULL, NULL); }
        
        void insertValues(const char *table, int count, char **values) {
            int status;
            char *sql;
            asprintf(& sql, "INSERT INTO %s VALUES (@name, @url, @display, @family, @style)", table);
            sqlite3_stmt *pStmt;
            if (sqlite3_prepare_v2(db, sql, -1, & pStmt, NULL) == SQLITE_OK) {
                for (int i = 0; i < count; i++)
                    status = sqlite3_bind_text(pStmt, i + 1, values[i], -1, SQLITE_STATIC);
                status = sqlite3_step(pStmt);
            }
            sqlite3_finalize(pStmt);
            free(sql);
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
            Rasterizer::Colorant red(0, 0, 255, 255), black(0, 0, 0, 255);
            float s = size / float(font.unitsPerEm), w = s * font.space * columnSpaces, h = s * (font.ascent - font.descent + font.lineGap), my = 0.5f * rowSize * h;
            int columns = 0, count = rowCount(table), n = (1.f - t) * float(count), range = ceilf(0.5f * rowSize) * 2, lower = n - range / 2, upper = n + range / 2;
            lower = lower < 0 ? 0 : lower, upper = upper > count ? count : upper;
            char *sql;
            asprintf(& sql, "SELECT * FROM %s LIMIT %d, %d", table, lower, upper - lower);
            sqlite3_stmt *pStmt;
            if (sqlite3_prepare_v2(db, sql, -1, & pStmt, NULL) == SQLITE_OK) {
                Rasterizer::Scene& header = list.addScene();
                columns = sqlite3_column_count(pStmt);
                for (int i = 0; i < columns; i++)
                    RasterizerTrueType::writeGlyphs(font, size, red, Rasterizer::Bounds(i * w, -FLT_MAX, (i + 1) * w, 0.f), false, true, sqlite3_column_name(pStmt, i), header);
                list.ctms.back().tx = frame.lx, list.ctms.back().ty = frame.uy;
                
                Rasterizer::Transform clip(columns * w, 0.f, 0.f, rowSize * h, frame.lx, frame.uy - (rowSize + 1) * h);
                for (int j = lower, status = sqlite3_step(pStmt); status == SQLITE_ROW; status = sqlite3_step(pStmt), j++) {
                    Rasterizer::Scene& scene = list.addScene();
                    for (int i = 0; i < columns; i++)
                        RasterizerTrueType::writeGlyphs(font, size, black, Rasterizer::Bounds(i * w, -FLT_MAX, (i + 1) * w, 0.f), false, true, (const char *)sqlite3_column_text(pStmt, i), scene);
                    list.ctms.back().tx = frame.lx, list.ctms.back().ty = frame.uy - my - (j - n) * h, list.clips.back() = clip;
                }
            }
            sqlite3_finalize(pStmt);
            free(sql);
            return { frame.lx, frame.uy - (rowSize + 1) * h, frame.lx + columns * w, frame.uy };
        }
        sqlite3 *db = nullptr;
    };
    struct View {
        View(int columnSpaces, int rowSize, const char *table) : columnSpaces(columnSpaces), rowSize(rowSize), t(1.f), bounds(0.f, 0.f, 0.f, 0.f) { strcpy(_table.alloc(strlen(table) + 1), table); }
        int columnSpaces, rowSize;
        float t;
        Rasterizer::Bounds bounds;
        Rasterizer::Row<char> _table;
    };
};
