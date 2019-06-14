//
//  RasterizerDB.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 29/04/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import <stdio.h>
#import <sqlite3.h>
#import "Rasterizer.hpp"
#import "RasterizerFont.hpp"

struct RasterizerDB {
    ~RasterizerDB() { close(); }
    int open(const char *filename) { return sqlite3_open(filename, & db); }
    void close() { sqlite3_close(db), db = nullptr; }
    int exec(const char *sql) { return sqlite3_exec(db, sql, NULL, NULL, NULL); }
    
    int beginImport(const char *table, const char **names, int count) {
        Rasterizer::Row<char> str;
        str = str + "BEGIN TRANSACTION; CREATE TABLE IF NOT EXISTS _ts(t REAL, tid INT UNIQUE); INSERT INTO _ts SELECT 0.0, rowid FROM sqlite_master WHERE NOT EXISTS(SELECT t FROM _ts WHERE tid = sqlite_master.rowid); " + "CREATE TABLE IF NOT EXISTS _" + table + " (";
        for (int i = 0; i < count; i++)
            str = str + (i == 0 ? "" : ", ") + names[i] + " text";
        str = str + "); DELETE FROM _" + table;
        exec(str.base);
        str = str.empty() + "INSERT INTO _" + table + " VALUES (";
        for (int i = 0; i < count; i++)
            str = str + (i == 0 ? "" : ", ") + "@" + i;
        str = str + ")";
        return sqlite3_prepare_v2(db, str.base, -1, & stmt, NULL);
    }
    int endImport(const char *table, const char **names, int count) {
        Rasterizer::Row<char> str, cols, tabs, joins;
        int lengths[count];
        tabs = tabs + "_" + table;
        writeColumnMetrics(tabs.base, names, "MAX", count, lengths, false);
        str = str.empty() + "CREATE TABLE IF NOT EXISTS " + table + "(id INTEGER PRIMARY KEY, ";
        for (int i = 0; i < count; i++)
            if (names[i][0] == '_') {
                str = str + (i == 0 ? "" : ", ") + table + names[i] + " int";
                cols = cols + (i == 0 ? "" : ", ") + table + names[i] + ".rowid";
                tabs = tabs + ", " + table + names[i];
                joins = joins + (joins.base ? " AND " : "") + & names[i][1] + " = _" + table + "." + names[i];
            } else {
                str = str + (i == 0 ? "" : ", ") + names[i] + " varchar(" + lengths[i] + ") UNIQUE";
                cols = cols + (i == 0 ? "" : ", ") + "_" + table + "." + names[i];
            }
        str = str + ")";
        int status = exec(str.base);
        for (int i = 0; i < count; i++)
            if (names[i][0] == '_') {
                str = str.empty() + "CREATE TABLE IF NOT EXISTS " + table + names[i] + "(id INTEGER PRIMARY KEY, " + & names[i][1] + " varchar(" + lengths[i] + ") UNIQUE)";
                status = exec(str.base);
                str = str.empty() + "INSERT INTO " + table + names[i] + " SELECT DISTINCT NULL, " + names[i] + " FROM _" + table + " ORDER BY " + names[i] + " ASC";
                status = exec(str.base);
            }
        str = str.empty() + "INSERT INTO " + table + " SELECT NULL, " + cols.base + " FROM " + tabs.base + " WHERE " + joins.base;
        exec(str.base), exec("END TRANSACTION");
        sqlite3_finalize(stmt), stmt = nullptr;
        str = str.empty() + "BEGIN TRANSACTION; DROP TABLE _" + table + "; VACUUM; END TRANSACTION;";
        return exec(str.base);
    }
    void insert(const char *table, int count, char **values) {
        for (int i = 0; i < count; i++)
            sqlite3_bind_text(stmt, i + 1, values[i], -1, SQLITE_STATIC);
        sqlite3_step(stmt), sqlite3_reset(stmt);
    }
    void writeColumnValues(const char *sql, void *values, bool real) {
        sqlite3_stmt *pStmt;
        if (sqlite3_prepare_v2(db, sql, -1, & pStmt, NULL) == SQLITE_OK && sqlite3_step(pStmt) == SQLITE_ROW) {
            for (int i = 0; i < sqlite3_column_count(pStmt); i++)
                if (real)
                    ((float *)values)[i] = sqlite3_column_double(pStmt, i);
                else
                    ((int *)values)[i] = sqlite3_column_int(pStmt, i);
        }
        sqlite3_finalize(pStmt);
    }
    void writeColumnMetrics(const char *table, const char **names, const char *fn, int count, void *metrics, bool real) {
        Rasterizer::Row<char> str;
        str = str + "SELECT ";
        for (int i = 0; i < count; i++)
            str = str + (i == 0 ? "" : ", ") + fn + "(LENGTH(" + names[i] + "))";
        str = str + " FROM " + table;
        writeColumnValues(str.base, metrics, real);
    }
    int rowCount(const char *table) {
        int count;
        Rasterizer::Row<char> str;
        str = str + "SELECT COUNT(*) FROM " + table;
        writeColumnValues(str.base, & count, false);
        return count;
    }
    void writeTables(RasterizerFont& font, float size, Rasterizer::Bounds frame, Rasterizer::SceneList& list) {
        Rasterizer::Colorant bg0(240, 255), bg1(248, 255);
        int count, N;
        writeColumnValues("SELECT COUNT(*) FROM sqlite_master WHERE name NOT LIKE 'sqlite%'", & count, false);
        N = ceilf(sqrtf(count));
        float fw = frame.ux - frame.lx, fh = frame.uy - frame.ly, dim = (fh < fw ? fh : fw) / N, pad = dim / 24.f;
        sqlite3_stmt *pStmt;
        if (sqlite3_prepare_v2(db, "SELECT tbl_name FROM sqlite_master WHERE name NOT LIKE 'sqlite%' ORDER BY tbl_name ASC", -1, & pStmt, NULL) == SQLITE_OK) {
            Rasterizer::Scene& bg = list.addScene();
            for (int i = 0, status = sqlite3_step(pStmt); status == SQLITE_ROW; status = sqlite3_step(pStmt), i++) {
                int x = i % N, y = i / N;
                Rasterizer::Bounds b = { frame.lx + x * dim, frame.uy - (y + 1) * dim, frame.lx + (x + 1) * dim, frame.uy - y * dim };
                Rasterizer::Path path;
                path.ref->addBounds(b);
                bg.addPath(path, Rasterizer::Transform::identity(), i & 1 ? bg1 : bg0);
                writeTable(font, size, 0.5f, Rasterizer::Bounds(b.lx + pad, b.ly + pad, b.ux - pad, b.uy - pad), (const char *)sqlite3_column_text(pStmt, 0), list);
            }
        }
        sqlite3_finalize(pStmt);
    }
    void writeTable(RasterizerFont& font, float size, float t, Rasterizer::Bounds frame, const char *table, Rasterizer::SceneList& list) {
        Rasterizer::Colorant red(0, 0, 255, 255), black(0, 255);
        Rasterizer::Row<char> str;
        str = str + "SELECT * FROM " + table + " LIMIT 1";
        sqlite3_stmt *pStmt0, *pStmt1;
        if (sqlite3_prepare_v2(db, str.base, -1, & pStmt0, NULL) == SQLITE_OK && sqlite3_step(pStmt0) == SQLITE_ROW) {
            int columns = sqlite3_column_count(pStmt0), lengths[columns], types[columns], total = 0, i, j, status, rows, count, n, range, lower, upper;
            float fs, lx, ux, s = size / float(font.unitsPerEm), h = s * (font.ascent - font.descent + font.lineGap);
            const char *names[columns];
            for (i = 0; i < columns; i++)
                types[i] = sqlite3_column_type(pStmt0, i), names[i] = sqlite3_column_name(pStmt0, i), lengths[i] = types[i] == SQLITE_TEXT ? 24 : strstr(names[i], "_") == NULL && strcmp(names[i], "id") ? 4 : 0, total += lengths[i];
            total = total < 24 ? 24 : total, fs = (frame.ux - frame.lx) / (s * total * font.em * (font.monospace ? 1.f : 0.666f)), size *= fs, h *= fs;
            rows = ceilf((frame.uy - frame.ly) / h), count = rowCount(table), n = (1.f - t) * float(count), range = ceilf(0.5f * rows);
            lower = n - range, upper = n + range, lower = lower < 0 ? 0 : lower, upper = upper > count ? count : upper;
            str = str.empty() + "SELECT ";
            for (int i = 0; i < columns; i++)
                if (types[i] == SQLITE_TEXT)
                    str = str + (i == 0 ? "" : ", ") + "CASE WHEN LENGTH(" + names[i] + ") < 24 THEN " + names[i] + " ELSE SUBSTR(" + names[i] + ", 1, 11) || '…' || SUBSTR(" + names[i] + ", LENGTH(" + names[i] + ") - 11) END AS " + names[i];
                else
                    str = str + (i == 0 ? "" : ", ") + names[i];
            str = str + " FROM " + table + " LIMIT " + lower + ", " + (upper - lower);
            
            if (sqlite3_prepare_v2(db, str.base, -1, & pStmt1, NULL) == SQLITE_OK) {
                Rasterizer::Scene& header = list.addScene();
                list.ctms.back().tx = frame.lx, list.ctms.back().ty = frame.uy;
                for (lx = 0.f, i = 0; i < columns; i++, lx = ux)
                    if (lx != (ux = lx + (frame.ux - frame.lx) * float(lengths[i]) / float(total)))
                        RasterizerFont::writeGlyphs(font, size, red, Rasterizer::Bounds(lx, -FLT_MAX, ux, 0.f), false, true, names[i], header);
                Rasterizer::Transform clip(frame.ux - frame.lx, 0.f, 0.f, frame.uy - frame.ly - h, frame.lx, frame.ly);
                for (j = lower, status = sqlite3_step(pStmt1); status == SQLITE_ROW; status = sqlite3_step(pStmt1), j++) {
                    Rasterizer::Scene& scene = list.addScene();
                    list.ctms.back().tx = frame.lx, list.ctms.back().ty = 0.5f * (frame.uy - h + frame.ly) - (j - n + 1) * h, list.clips.back() = clip;
                    for (lx = 0.f, i = 0; i < columns; i++, lx = ux)
                        if (lx != (ux = lx + (frame.ux - frame.lx) * float(lengths[i]) / float(total)))
                            RasterizerFont::writeGlyphs(font, size, black, Rasterizer::Bounds(lx, -FLT_MAX, ux, 0.f), false, true, (const char *)sqlite3_column_text(pStmt1, i), scene);
                }
            }
            sqlite3_finalize(pStmt1);
        }
        sqlite3_finalize(pStmt0);
    }
    sqlite3 *db = nullptr;
    sqlite3_stmt *stmt = nullptr;
};
