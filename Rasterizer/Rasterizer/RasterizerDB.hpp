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
#import "RasterizerTrueType.hpp"

struct RasterizerDB {
    static constexpr const char *kFontsTable = "fonts";
    static constexpr const char *kCountTables = "SELECT COUNT(*) FROM sqlite_master;";
    static constexpr const char *kSelectTableNames = "SELECT tbl_name FROM sqlite_master ORDER BY tbl_name ASC;";
    const int kColumnSpaces = 6, kColumnCount = 5, kRowSize = 16;
    
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
                asprintf(& sql, "INSERT INTO %s%s SELECT DISTINCT NULL, %s FROM _%s;", table, names[i], names[i], table);
                status = exec(sql);
                free(sql);
            }
        asprintf(& sql, "INSERT INTO %s SELECT NULL, %s FROM %s WHERE %s; DROP TABLE _%s; VACUUM;", table, cols, tabs, joins, table);
        status = exec(sql);
        free(sql), free(cols), free(tabs), free(joins);
        return status;
    }
    void insert(const char *table, int count, char **values) {
        char *sql, val[count * 4 + 1], *v = val;
        *v++ = '@', *v++ = '0';
        for (int i = 1; i < count; i++)
            *v++ = ',', *v++ = ' ', *v++ = '@', *v++ = '0' + i;
        *v = 0;
        asprintf(& sql, "INSERT INTO _%s VALUES (%s)", table, val);
        sqlite3_stmt *pStmt;
        if (sqlite3_prepare_v2(db, sql, -1, & pStmt, NULL) == SQLITE_OK) {
            for (int i = 0; i < count; i++)
                sqlite3_bind_text(pStmt, i + 1, values[i], -1, SQLITE_STATIC);
            sqlite3_step(pStmt);
        }
        sqlite3_finalize(pStmt);
        free(sql);
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
        char *str0, *str1, *sql;
        asprintf(& str0, "%s(LENGTH(%s))", fn, names[0]);
        for (int i = 1; i < count; i++)
            asprintf(& str1, "%s, %s(LENGTH(%s))", str0, fn, names[i]), free(str0), str0 = str1;
        asprintf(& sql, "SELECT %s FROM %s", str0, table);
        writeColumnValues(sql, metrics, real);
        free(sql), free(str0);
    }
    int rowCount(const char *table) {
        int count;
        char *sql;
        asprintf(& sql, "SELECT COUNT(*) FROM %s", table);
        writeColumnValues(sql, & count, false);
        free(sql);
        return count;
    }
    void writeTables(RasterizerTrueType::Font& font, float size, Rasterizer::Bounds frame, Rasterizer::SceneList& list) {
        int count, N;
        writeColumnValues(kCountTables, & count, false);
        N = ceilf(sqrtf(count));
        float fw = frame.ux - frame.lx, fh = frame.uy - frame.ly, dim = fw < fh ? fh : fw / N;
        sqlite3_stmt *pStmt;
        if (sqlite3_prepare_v2(db, kSelectTableNames, -1, & pStmt, NULL) == SQLITE_OK) {
            for (int i = 0, status = sqlite3_step(pStmt); status == SQLITE_ROW; status = sqlite3_step(pStmt), i++) {
                int x = i % N, y = i / N;
                Rasterizer::Bounds bounds = { frame.lx + x * dim, frame.uy - (y + 1) * dim, frame.lx + (x + 1) * dim, frame.uy - y * dim };
                writeTable(font, size, 1.f, bounds, (const char *)sqlite3_column_text(pStmt, 0), list);
            }
        }
        sqlite3_finalize(pStmt);
    }
    void writeTable(RasterizerTrueType::Font& font, float size, float t, Rasterizer::Bounds frame, const char *table, Rasterizer::SceneList& list) {
        Rasterizer::Colorant red(0, 0, 255, 255), black(0, 0, 0, 255);
        float s = size / float(font.unitsPerEm), h = s * (font.ascent - font.descent + font.lineGap), my = 0.5f * kRowSize * h;
        int columns = 0, count = rowCount(table), n = (1.f - t) * float(count), range = ceilf(0.5f * kRowSize) * 2, lower = n - range / 2, upper = n + range / 2;
        lower = lower < 0 ? 0 : lower, upper = upper > count ? count : upper;
        char *sql;
        asprintf(& sql, "SELECT * FROM %s LIMIT %d, %d", table, lower, upper - lower);
        sqlite3_stmt *pStmt;
        if (sqlite3_prepare_v2(db, sql, -1, & pStmt, NULL) == SQLITE_OK) {
            Rasterizer::Scene& header = list.addScene();
            columns = sqlite3_column_count(pStmt);
            float avgLengths[columns], tw, fs, lx, ux;
            int lengths[columns], total = 0, i, j, status;
            const char *names[columns];
            for (i = 0; i < columns; i++)
                names[i] = sqlite3_column_name(pStmt, i), lengths[i] = strstr(names[i], "_") ? 0 : (int)strlen(names[i]);
            writeColumnMetrics(table, names, "AVG", columns, avgLengths, true);
            for (i = 0; i < columns; i++)
                avgLengths[i] = ceilf(avgLengths[i] * 1.f), lengths[i] = lengths[i] > avgLengths[i] ? lengths[i] : avgLengths[i], total += lengths[i];
            tw = s * total * font.space * (font.monospace ? 1.f : 2.f);
            fs = powf(2.f, floorf(log2f((frame.ux - frame.lx) / tw)));
            for (lx = 0.f, i = 0; i < columns; i++, lx = ux) {
                ux = lx + fs * tw * float(lengths[i]) / float(total);
                RasterizerTrueType::writeGlyphs(font, fs * size, red, Rasterizer::Bounds(lx, -FLT_MAX, ux, 0.f), false, true, names[i], header);
            }
            list.ctms.back().tx = frame.lx, list.ctms.back().ty = frame.uy;
            
            Rasterizer::Transform clip(frame.ux - frame.lx, 0.f, 0.f, frame.uy - frame.ly, frame.lx, frame.ly);
            for (j = lower, status = sqlite3_step(pStmt); status == SQLITE_ROW; status = sqlite3_step(pStmt), j++) {
                Rasterizer::Scene& scene = list.addScene();
                for (lx = 0.f, i = 0; i < columns; i++, lx = ux) {
                    ux = lx + fs * tw * float(lengths[i]) / float(total);
                    RasterizerTrueType::writeGlyphs(font, fs * size, black, Rasterizer::Bounds(lx, -FLT_MAX, ux, 0.f), false, true, (const char *)sqlite3_column_text(pStmt, i), scene);
                }
                list.ctms.back().tx = frame.lx, list.ctms.back().ty = frame.uy - fs * (j - lower + 1) * h, list.clips.back() = clip;
            }
        }
        sqlite3_finalize(pStmt);
        free(sql);
    }
    sqlite3 *db = nullptr;
};
