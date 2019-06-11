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
    static constexpr const char *kCountTables = "SELECT COUNT(*) FROM sqlite_master;";
    static constexpr const char *kSelectTableNames = "SELECT tbl_name FROM sqlite_master ORDER BY tbl_name ASC;";
    
    ~RasterizerDB() { close(); }
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
        Rasterizer::Row<char> str;
        str.cat("CREATE TABLE IF NOT EXISTS _").cat(table).cat(" (");
        for (int i = 0; i < count; i++) {
            if (i > 0)
                str.cat(", ");
            str.cat(names[i]).cat(" text");
        }
        str.cat("); DELETE FROM _").cat(table);
        return exec(str.base);
    }
    int endImport(const char *table, const char **names, int count) {
        char *str0, *str1, *sql, *cols, *tabs, *joins = nullptr, *tmp;
        asprintf(& tabs, "_%s", table);
        int lengths[count];
        writeColumnMetrics(tabs, names, "MAX", count, lengths, false);
        if (names[0][0] == '_') {
            asprintf(& str0, "%s%s int", table, names[0]);
            asprintf(& cols, "%s%s.rowid", table, names[0]);
            asprintf(& tmp, "%s, %s%s", tabs, table, names[0]), free(tabs), tabs = tmp;
            asprintf(& joins, "%s = _%s.%s", & names[0][1], table, names[0]);
        } else {
            asprintf(& str0, "%s varchar(%d)", names[0], lengths[0]);
            asprintf(& cols, "_%s.%s", table, names[0]);
        }
        for (int i = 1; i < count; i++)
            if (names[i][0] == '_') {
                asprintf(& str1, "%s, %s%s int", str0, table, names[i]), free(str0), str0 = str1;
                asprintf(& tmp, "%s, %s%s.rowid", cols, table, names[i]), free(cols), cols = tmp;
                asprintf(& tmp, "%s, %s%s", tabs, table, names[i]), free(tabs), tabs = tmp;
                if (joins)
                    asprintf(& tmp, "%s AND %s = _%s.%s", joins, & names[i][1], table, names[i]), free(joins), joins = tmp;
                else
                    asprintf(& joins, "%s = _%s.%s", & names[i][1], table, names[i]);
            } else {
                asprintf(& str1, "%s, %s varchar(%d)", str0, names[i], lengths[i]), free(str0), str0 = str1;
                asprintf(& tmp, "%s, _%s.%s", cols, table, names[i]), free(cols), cols = tmp;
            }
        asprintf(& sql, "CREATE TABLE IF NOT EXISTS %s(id INTEGER PRIMARY KEY, %s); DELETE FROM %s;", table, str0, table);
        int status = exec(sql);
        free(sql), free(str0);
        
        for (int i = 0; i < count; i++)
            if (names[i][0] == '_') {
                asprintf(& sql, "CREATE TABLE IF NOT EXISTS %s%s(id INTEGER PRIMARY KEY, %s varchar(%d)); DELETE FROM %s%s;", table, names[i], & names[i][1], lengths[i], table, names[i]);
                status = exec(sql);
                free(sql);
                asprintf(& sql, "INSERT INTO %s%s SELECT DISTINCT NULL, %s FROM _%s ORDER BY %s ASC;", table, names[i], names[i], table, names[i]);
                status = exec(sql);
                free(sql);
            }
        asprintf(& sql, "INSERT INTO %s SELECT NULL, %s FROM %s WHERE %s; DROP TABLE _%s; VACUUM;", table, cols, tabs, joins, table);
        status = exec(sql);
        free(sql), free(cols), free(tabs), free(joins);
        return status;
    }
    void insert(const char *table, int count, char **values) {
        char num[2] = { 0, 0 };
        Rasterizer::Row<char> str;
        str.cat("INSERT INTO _").cat(table).cat(" VALUES (");
        for (int i = 0; i < count; i++) {
            if (i > 0)
                str.cat(", ");
            num[0] = '0' + i, str.cat("@").cat(num);
        }
        str.cat(")");
        sqlite3_stmt *pStmt;
        if (sqlite3_prepare_v2(db, str.base, -1, & pStmt, NULL) == SQLITE_OK) {
            for (int i = 0; i < count; i++)
                sqlite3_bind_text(pStmt, i + 1, values[i], -1, SQLITE_STATIC);
            sqlite3_step(pStmt);
        }
        sqlite3_finalize(pStmt);
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
        str.cat("SELECT ");
        for (int i = 0; i < count; i++) {
            if (i > 0)
                str.cat(", ");
            str.cat(fn).cat("(LENGTH(").cat(names[i]).cat("))");
        }
        str.cat(" FROM ").cat(table);
        writeColumnValues(str.base, metrics, real);
    }
    int rowCount(const char *table) {
        int count;
        Rasterizer::Row<char> str;
        str.cat("SELECT COUNT(*) FROM ").cat(table);
        writeColumnValues(str.base, & count, false);
        return count;
    }
    void writeTables(RasterizerFont& font, float size, Rasterizer::Bounds frame, Rasterizer::SceneList& list) {
        int count, N;
        writeColumnValues(kCountTables, & count, false);
        N = ceilf(sqrtf(count));
        float fw = frame.ux - frame.lx, fh = frame.uy - frame.ly, dim = fw < fh ? fh : fw / N, padding = 1.f - 1.f / 24.f;
        sqlite3_stmt *pStmt;
        if (sqlite3_prepare_v2(db, kSelectTableNames, -1, & pStmt, NULL) == SQLITE_OK) {
            for (int i = 0, status = sqlite3_step(pStmt); status == SQLITE_ROW; status = sqlite3_step(pStmt), i++) {
                int x = i % N, y = i / N;
                Rasterizer::Bounds bounds = { frame.lx + x * dim, frame.uy - (y + 1) * dim * padding, frame.lx + (x + 1) * dim * padding, frame.uy - y * dim };
                writeTable(font, size, 0.5f, bounds, (const char *)sqlite3_column_text(pStmt, 0), list);
            }
        }
        sqlite3_finalize(pStmt);
    }
    void writeTable(RasterizerFont& font, float size, float t, Rasterizer::Bounds frame, const char *table, Rasterizer::SceneList& list) {
        Rasterizer::Colorant red(0, 0, 255, 255), black(0, 0, 0, 255);
        Rasterizer::Row<char> str;
        str.cat("SELECT * FROM ").cat(table).cat(" LIMIT 1");
        sqlite3_stmt *pStmt0, *pStmt1;
        if (sqlite3_prepare_v2(db, str.base, -1, & pStmt0, NULL) == SQLITE_OK && sqlite3_step(pStmt0) == SQLITE_ROW) {
            int columns = sqlite3_column_count(pStmt0), lengths[columns], types[columns], total = 0, i, j, status;
            float fs, lx, ux, s = size / float(font.unitsPerEm), h = s * (font.ascent - font.descent + font.lineGap);
            const char *names[columns];
            for (i = 0; i < columns; i++)
                types[i] = sqlite3_column_type(pStmt0, i), names[i] = sqlite3_column_name(pStmt0, i), lengths[i] = types[i] != SQLITE_TEXT ? 0 : 24, total += lengths[i];
            fs = (frame.ux - frame.lx) / (s * total * font.em * (font.monospace ? 1.f : 0.666f)), size *= fs, h *= fs;
            int rows = ceilf((frame.uy - frame.ly) / h), count = rowCount(table), n = (1.f - t) * float(count), range = ceilf(0.5f * rows), lower = n - range, upper = n + range;
            lower = lower < 0 ? 0 : lower, upper = upper > count ? count : upper;
            str.empty().cat("SELECT ");
            for (int i = 0; i < columns; i++) {
                if (i > 0)
                    str.cat(", ");
                if (types[i] == SQLITE_TEXT)
                    str.cat("CASE WHEN LENGTH(").cat(names[i]).cat(") < 24 THEN ").cat(names[i]).cat(" ELSE SUBSTR(").cat(names[i]).cat(", 1, 11) || '…' || SUBSTR(").cat(names[i]).cat(", LENGTH(").cat(names[i]).cat(") - 11) END AS ").cat(names[i]);
                else
                    str.cat("0");
            }
            str.cat(" FROM ").cat(table).cat(" LIMIT "), sprintf(str.alloc(32), "%d, %d", lower, upper - lower);
            
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
};
