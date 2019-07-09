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
    const int kTextChars = 24, kRealChars = 4;
    ~RasterizerDB() { close(); }
    int open(const char *filename) { return sqlite3_open(filename, & db); }
    void close() { sqlite3_close(db), db = nullptr; }
    
    void beginImport(const char *table, const char **names, int count) {
        Ra::Row<char> str;
        str = str + "BEGIN TRANSACTION; CREATE TABLE IF NOT EXISTS ras_ts(t REAL, tid INT UNIQUE); INSERT INTO ras_ts SELECT 1.0, rowid FROM sqlite_master WHERE NOT EXISTS(SELECT t FROM ras_ts WHERE tid = sqlite_master.rowid); " + "CREATE TABLE IF NOT EXISTS _" + table + " (";
        for (int i = 0; i < count; i++)
            str = str + (i == 0 ? "" : ", ") + names[i] + " text";
        str = str + "); DELETE FROM _" + table, sqlite3_exec(db, str.base, NULL, NULL, NULL);
        str = str.empty() + "INSERT INTO _" + table + " VALUES (";
        for (int i = 0; i < count; i++)
            str = str + (i == 0 ? "" : ", ") + "@" + i;
        str = str + ")", sqlite3_prepare_v2(db, str.base, -1, & stmt, NULL);
    }
    void endImport(const char *table, const char **names, int count) {
        Ra::Row<char> str, cols, tabs, joins;
        int lengths[count], status;
        tabs = tabs + "_" + table, str = str + "SELECT ";
        for (int i = 0; i < count; i++)
            str = str + (i == 0 ? "" : ", ") + "MAX(LENGTH(" + names[i] + "))";
        str = str + " FROM " + tabs.base, writeColumnValues(str.base, lengths, false);
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
        str = str + ")", sqlite3_exec(db, str.base, NULL, NULL, NULL);
        for (int i = 0; i < count; i++)
            if (names[i][0] == '_') {
                str = str.empty() + "CREATE TABLE IF NOT EXISTS " + table + names[i] + "(id INTEGER PRIMARY KEY, " + & names[i][1] + " varchar(" + lengths[i] + ") UNIQUE)";
                status = sqlite3_exec(db, str.base, NULL, NULL, NULL);
                str = str.empty() + "INSERT INTO " + table + names[i] + " SELECT DISTINCT NULL, " + names[i] + " FROM _" + table + " ORDER BY " + names[i] + " ASC";
                status = sqlite3_exec(db, str.base, NULL, NULL, NULL);
            }
        str = str.empty() + "INSERT INTO " + table + " SELECT NULL, " + cols.base + " FROM " + tabs.base + " WHERE " + joins.base;
        sqlite3_exec(db, str.base, NULL, NULL, NULL), sqlite3_exec(db, "END TRANSACTION", NULL, NULL, NULL);
        sqlite3_finalize(stmt), stmt = nullptr;
        str = str.empty() + "BEGIN TRANSACTION; DROP TABLE _" + table + "; VACUUM; END TRANSACTION;", sqlite3_exec(db, str.base, NULL, NULL, NULL);
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
    void writeTables(RasterizerFont& font, Ra::Bounds frame, Ra::SceneList& list) {
        Ra::Colorant bg[4] = { Ra::Colorant(240, 255), Ra::Colorant(244, 255), Ra::Colorant(248, 255), Ra::Colorant(253, 255) };
        Ra::Row<char> str;
        int count, N;
        Ra::SceneList tables;
        writeColumnValues("SELECT COUNT(DISTINCT(SUBSTR(tbl_name, 1, 1))) FROM sqlite_master WHERE name NOT LIKE 'sqlite%'", & count, false), N = ceilf(sqrtf(count));
        float fw = frame.ux - frame.lx, fh = frame.uy - frame.ly, dim = (fh < fw ? fh : fw) / N, pad = dim / float(kTextChars);
        sqlite3_stmt *pStmt0, *pStmt1;
        if (sqlite3_prepare_v2(db, "SELECT SUBSTR(tbl_name, 1, 1) as initial, COUNT(*) AS count FROM sqlite_master WHERE LOWER(initial) != UPPER(initial) AND name NOT LIKE 'sqlite%' GROUP BY initial ORDER BY initial ASC", -1, & pStmt1, NULL) == SQLITE_OK) {
            Ra::Ref<Ra::Scene> background;
            for (int i = 0, x = 0, y = 0, status = sqlite3_step(pStmt1); status == SQLITE_ROW; status = sqlite3_step(pStmt1), i++, x = i % N, y = i / N) {
                Ra::Bounds b = { frame.lx + x * dim, frame.uy - (y + 1) * dim, frame.lx + (x + 1) * dim, frame.uy - y * dim };
                Ra::Bounds gb = { b.lx + pad, b.ly + pad, b.ux - pad, b.uy - pad };
                int gCount = sqlite3_column_int(pStmt1, 1), gN = ceilf(sqrtf(gCount));
                float gdim = (gb.ux - gb.lx) / (gN + 2.f * (gN - 1.f) / float(kTextChars)), gpad = gdim * 2.f / float(kTextChars);
                str = str.empty() + "SELECT tbl_name, t FROM sqlite_master t0, ras_ts WHERE name NOT LIKE 'sqlite%' AND t0.rowid = ras_ts.tid AND SUBSTR(tbl_name, 1, 1) = '" + (const char *)sqlite3_column_text(pStmt1, 0) + "' ORDER BY tbl_name ASC";
                if (sqlite3_prepare_v2(db, str.base, -1, & pStmt0, NULL) == SQLITE_OK) {
                    for (int gi = 0, gx = 0, gy = 0, status = sqlite3_step(pStmt0); gi < gN * gN; status = status == SQLITE_ROW ? sqlite3_step(pStmt0) : status, gi++, gx = gi % gN, gy = gi / gN) {
                        float lx = gb.lx + gx * (gdim + gpad), uy = gb.uy - gy * (gdim + gpad);
                        Ra::Bounds tb = { lx, uy - gdim, lx + gdim, uy };
                        Ra::Bounds bb = { gx == 0 ? b.lx : tb.lx - 0.5f * gpad, gy == gN - 1 ? b.ly : tb.ly - 0.5f * gpad, gx == gN - 1 ? b.ux : tb.ux + 0.5f * gpad, gy == 0 ? b.uy : tb.uy + 0.5f * gpad };
                        background.ref->addPath(Ra::boundsPath(bb), Ra::Transform(), bg[((y & 1) ^ (x & 1)) * 2 + ((gy & 1) ^ (gx & 1))]);
                        if (status == SQLITE_ROW)
                            writeTable(font, sqlite3_column_double(pStmt0, 1), tb, (const char *)sqlite3_column_text(pStmt0, 0), tables);
                    }
                }
                sqlite3_finalize(pStmt0);
            }
            list.addScene(background);
            for (int i = 0; i < tables.scenes.size(); i++)
                list.addScene(tables.scenes[i], tables.ctms[i], tables.clips[i], 0.f, false);
        }
        sqlite3_finalize(pStmt1);
    }
    void writeTable(RasterizerFont& font, float t, Ra::Bounds frame, const char *table, Ra::SceneList& list) {
        Ra::Colorant red(0, 0, 255, 255), black(0, 255), gray(144, 255);
        Ra::Row<char> str;
        str = str + "SELECT * FROM " + table + " LIMIT 1";
        sqlite3_stmt *pStmt0 = NULL, *pStmt1 = NULL;
        if (sqlite3_prepare_v2(db, str.base, -1, & pStmt0, NULL) == SQLITE_OK && sqlite3_step(pStmt0) == SQLITE_ROW) {
            int columns = sqlite3_column_count(pStmt0), lengths[columns], types[columns], total = 0, i, j, status, rows, count, n, range, lower, upper;
            float fw, fh, fs, lx, ux, my, h;
            const char *names[columns];
            for (i = 0; i < columns; i++)
                types[i] = sqlite3_column_type(pStmt0, i), names[i] = sqlite3_column_name(pStmt0, i), lengths[i] = types[i] == SQLITE_TEXT ? kTextChars : strstr(names[i], "_") == NULL && strcmp(names[i], "id") ? kRealChars : 0, total += lengths[i];
            total = total < kTextChars ? kTextChars : total;
            fw = frame.ux - frame.lx, fh = frame.uy - frame.ly;
            fs = fw / (total * font.avg), h = fs * (font.ascent - font.descent + font.lineGap), my = frame.uy - roundf(0.5f * fh / h) * h;
            str = str.empty() + "SELECT COUNT(*) FROM " + table, writeColumnValues(str.base, & count, false);
            rows = ceilf(fh / h), range = ceilf(0.5f * rows), n = (1.f - (t < 1e-6f ? 1e-6f : t)) * float(count);
            lower = n - range, upper = n + range, lower = lower < 0 ? 0 : lower, upper = upper > count ? count : upper;
            str = str.empty() + "SELECT ";
            for (int i = 0; i < columns; i++)
                if (types[i] == SQLITE_TEXT)
                    str = str + (i == 0 ? "" : ", ") + "CASE WHEN LENGTH(" + names[i] + ") < 24 THEN " + names[i] + " ELSE SUBSTR(" + names[i] + ", 1, 11) || '…' || SUBSTR(" + names[i] + ", LENGTH(" + names[i] + ") - 11) END AS " + names[i];
                else
                    str = str + (i == 0 ? "" : ", ") + names[i];
            str = str + " FROM " + table + " LIMIT " + lower + ", " + (upper - lower);
            if (sqlite3_prepare_v2(db, str.base, -1, & pStmt1, NULL) == SQLITE_OK) {
                Ra::Ref<Ra::Scene> header, line;
                for (lx = 0.f, i = 0; i < columns; i++, lx = ux)
                    if (lx != (ux = lx + fw * float(lengths[i]) / float(total)))
                        RasterizerFont::writeGlyphs(font, fs * float(font.unitsPerEm), red, Ra::Bounds(lx, -FLT_MAX, ux, 0.f), false, true, lengths[i] != kTextChars, names[i], *header.ref);
                list.addScene(header, Ra::Transform(1.f, 0.f, 0.f, 1.f, frame.lx, frame.uy), Ra::Transform::nullclip(), 0.f, false);
                line.ref->addPath(Ra::boundsPath(Ra::Bounds(frame.lx, my - h / 256.f, frame.ux, my + h / 256.f)), Ra::Transform(), red);
                list.addScene(line);
                Ra::Transform clip(frame.ux - frame.lx, 0.f, 0.f, frame.uy - frame.ly - h, frame.lx, frame.ly);
                for (j = lower, status = sqlite3_step(pStmt1); status == SQLITE_ROW; status = sqlite3_step(pStmt1), j++) {
                    Ra::Ref<Ra::Scene> row;
                    Ra::Transform ctm = { 1.f, 0.f, 0.f, 1.f, frame.lx, my + h * ((1.f - t) * float(count) - j) };
                    for (lx = 0.f, i = 0; i < columns; i++, lx = ux)
                        if (lx != (ux = lx + fw * float(lengths[i]) / float(total)))
                            RasterizerFont::writeGlyphs(font, fs * float(font.unitsPerEm), j == n ? black : gray, Ra::Bounds(lx, -FLT_MAX, ux, 0.f), false, true, lengths[i] != kTextChars, (const char *)sqlite3_column_text(pStmt1, i), *row.ref);
                    list.addScene(row, ctm, clip, 0.f, false);
                }
            }
        }
        sqlite3_finalize(pStmt0), sqlite3_finalize(pStmt1);
    }
    sqlite3 *db = nullptr;
    sqlite3_stmt *stmt = nullptr;
};
