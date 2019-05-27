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
        static constexpr const char *kFontsTable = "fonts";
        static constexpr const char *kInsertSelect = "SELECT 'test', fontFamily.rowid, fontStyle.rowid FROM fontFamily, fontStyle WHERE family = 'Helvetica' AND style = 'Bold';";
        static constexpr const char *kSelectTables = "SELECT tbl_name FROM sqlite_master ORDER BY tbl_name ASC;";
        const int kColumnSpaces = 6, kColumnCount = 5, kRowSize = 8;
        
        ~DB() { close(); }
        int open(const char *filename) {
            return sqlite3_open(filename, & db);
        }
        void close() {
            sqlite3_close(db);
            db = nullptr;
        }
        int beginTransaction() { return exec("BEGIN TRANSACTION"); }
        int endTransaction() { return exec("END TRANSACTION"); }
        int exec(const char *sql) { return sqlite3_exec(db, sql, NULL, NULL, NULL); }
        
        int beginImport(const char *table, const char **names, int count) {
            char *str0, *str1, *sql;
            asprintf(& str0, "%s text", names[0]);
            for (int i = 1; i < count; i++)
                asprintf(& str1, "%s, %s text", str0, names[i]), free(str0), str0 = str1;
            asprintf(& sql, "CREATE TABLE IF NOT EXISTS _%s(%s); DELETE FROM _%s;", table, str0, table);
            int status = exec(sql);
            free(sql), free(str0);
            return status;
        }
        int endImport(const char *table, const char **names, int count) {
            char *str0, *str1, *sql;
            asprintf(& str0, "MAX(LENGTH(%s))", names[0]);
            for (int i = 1; i < count; i++)
                asprintf(& str1, "%s, MAX(LENGTH(%s))", str0, names[i]), free(str0), str0 = str1;
            asprintf(& sql, "SELECT %s FROM _%s", str0, table);
            int lengths[count];
            writeRowValues(sql, lengths);
            free(sql), free(str0);
            
            if (names[0][0] == '_')
                asprintf(& str0, "%s int", & names[0][1]);
            else
                asprintf(& str0, "%s varchar(%d)", names[0], lengths[0]);
            for (int i = 1; i < count; i++)
                if (names[i][0] == '_')
                    asprintf(& str1, "%s, %s int", str0, & names[i][1]), free(str0), str0 = str1;
                else
                    asprintf(& str1, "%s, %s varchar(%d)", str0, names[i], lengths[i]), free(str0), str0 = str1;
            asprintf(& sql, "CREATE TABLE IF NOT EXISTS %s(%s); DELETE FROM %s;", table, str0, table);
            int status = exec(sql);
            free(sql), free(str0);
            
            return status;
        }
        void insert(const char *table, int count, char **values) {
            char *sql, val[count * 4 + 1], *v = val;
            *v++ = '@', *v++ = '0';
            for (int i = 1; i < count; i++)
                *v++ = ',', *v++ = ' ', *v++ = '@', *v++ = '0' + i;
            *v = 0;
            asprintf(& sql, "INSERT INTO %s VALUES (%s)", table, val);
            sqlite3_stmt *pStmt;
            if (sqlite3_prepare_v2(db, sql, -1, & pStmt, NULL) == SQLITE_OK) {
                for (int i = 0; i < count; i++)
                    sqlite3_bind_text(pStmt, i + 1, values[i], -1, SQLITE_STATIC);
                sqlite3_step(pStmt);
            }
            sqlite3_finalize(pStmt);
            free(sql);
        }
        void writeRowValues(const char *sql, int *values) {
            sqlite3_stmt *pStmt;
            if (sqlite3_prepare_v2(db, sql, -1, & pStmt, NULL) == SQLITE_OK && sqlite3_step(pStmt) == SQLITE_ROW)
                for (int i = 0; i < sqlite3_column_count(pStmt); i++)
                    values[i] = sqlite3_column_int(pStmt, i);
            sqlite3_finalize(pStmt);
        }
        int rowCount(const char *table) {
            int count;
            char *sql;
            asprintf(& sql, "SELECT COUNT(*) FROM %s", table);
            writeRowValues(sql, & count);
            free(sql);
            return count;
        }
        Rasterizer::Bounds writeTables(RasterizerTrueType::Font& font, float size, Rasterizer::Bounds frame, Rasterizer::SceneList& list) {
            float s = size / float(font.unitsPerEm), h = s * (font.ascent - font.descent + font.lineGap), w = h * kColumnSpaces * kColumnCount;
            sqlite3_stmt *pStmt;
            if (sqlite3_prepare_v2(db, kSelectTables, -1, & pStmt, NULL) == SQLITE_OK) {
                for (int i = 0, status = sqlite3_step(pStmt); status == SQLITE_ROW; status = sqlite3_step(pStmt), i++) {
                    Rasterizer::Bounds bounds = { frame.lx + i * w, -FLT_MAX, frame.lx + (i + 1) * w, frame.uy };
                    writeTable(font, size, 1.f, bounds, (const char *)sqlite3_column_text(pStmt, 0), list);
                }
            }
            sqlite3_finalize(pStmt);
            return frame;
        }
        Rasterizer::Bounds writeTable(RasterizerTrueType::Font& font, float size, float t, Rasterizer::Bounds frame, const char *table, Rasterizer::SceneList& list) {
            Rasterizer::Colorant red(0, 0, 255, 255), black(0, 0, 0, 255);
            float s = size / float(font.unitsPerEm), h = s * (font.ascent - font.descent + font.lineGap), w = h * kColumnSpaces, my = 0.5f * kRowSize * h;
            int columns = 0, count = rowCount(table), n = (1.f - t) * float(count), range = ceilf(0.5f * kRowSize) * 2, lower = n - range / 2, upper = n + range / 2;
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
                
                Rasterizer::Transform clip(columns * w, 0.f, 0.f, kRowSize * h, frame.lx, frame.uy - (kRowSize + 1) * h);
                for (int j = lower, status = sqlite3_step(pStmt); status == SQLITE_ROW; status = sqlite3_step(pStmt), j++) {
                    Rasterizer::Scene& scene = list.addScene();
                    for (int i = 0; i < columns; i++)
                        RasterizerTrueType::writeGlyphs(font, size, black, Rasterizer::Bounds(i * w, -FLT_MAX, (i + 1) * w, 0.f), false, true, (const char *)sqlite3_column_text(pStmt, i), scene);
                    list.ctms.back().tx = frame.lx, list.ctms.back().ty = frame.uy - my - (j - n) * h, list.clips.back() = clip;
                }
            }
            sqlite3_finalize(pStmt);
            free(sql);
            return { frame.lx, frame.uy - (kRowSize + 1) * h, frame.lx + columns * w, frame.uy };
        }
        sqlite3 *db = nullptr;
    };
};
