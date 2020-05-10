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
#import "RasterizerState.hpp"
#import "RasterizerWinding.hpp"

struct RasterizerDB {
    struct Table {
        Table(const char *nm, Ra::Bounds bounds, float t) : bounds(bounds), t(t) { name = name + nm; }
        Ra::Row<char> name;
        Ra::Bounds bounds;
        float t;
    };
    const static int kTextChars = 24, kRealChars = 4;
    ~RasterizerDB() { close(); }
    int open(const char *filename) { close();  return sqlite3_open(filename, & db); }
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
    void writeTables(Ra::Bounds frame) {
        Ra::Colorant bg[4] = {
            Ra::Colorant(240, 240, 240, 255), Ra::Colorant(244, 244, 244, 255),
            Ra::Colorant(248, 248, 248, 255), Ra::Colorant(253, 253, 253, 255)
        };
        tables = std::vector<Table>();
        Ra::Row<char> str;
        int count, N;
        writeColumnValues("SELECT COUNT(DISTINCT(SUBSTR(tbl_name, 1, 1))) FROM sqlite_master WHERE name NOT LIKE 'sqlite%'", & count, false), N = ceilf(sqrtf(count));
        float fw, fh, dim, pad, lx, ly, hw;
        fw = frame.ux - frame.lx, fh = frame.uy - frame.ly, dim = (fh < fw ? fh : fw) / N, pad = dim / float(kTextChars), hw = dim / 1024.f;
        sqlite3_stmt *pStmt0, *pStmt1;
        if (sqlite3_prepare_v2(db, "SELECT SUBSTR(tbl_name, 1, 1) as initial, COUNT(*) AS count FROM sqlite_master WHERE LOWER(initial) != UPPER(initial) AND name NOT LIKE 'sqlite%' GROUP BY initial ORDER BY initial ASC", -1, & pStmt1, NULL) == SQLITE_OK) {
            Ra::Scene background, foreground;
            for (int i = 0, x = 0, y = 0, status = sqlite3_step(pStmt1); status == SQLITE_ROW; status = sqlite3_step(pStmt1), i++, x = i % N, y = i / N) {
                lx = frame.lx + x * dim, ly = frame.uy - (y + 1) * dim;
                Ra::Bounds b = { lx, ly, lx + dim, ly + dim }, gb = b.inset(pad, pad);
                int gCount = sqlite3_column_int(pStmt1, 1), gN = ceilf(sqrtf(gCount));
                float gdim = (gb.ux - gb.lx) / (gN + 2.f * (gN - 1.f) / float(kTextChars)), gpad = gdim * 2.f / float(kTextChars);
                str = str.empty() + "SELECT tbl_name, t FROM sqlite_master t0, ras_ts WHERE name NOT LIKE 'sqlite%' AND t0.rowid = ras_ts.tid AND SUBSTR(tbl_name, 1, 1) = '" + (const char *)sqlite3_column_text(pStmt1, 0) + "' ORDER BY tbl_name ASC";
                if (sqlite3_prepare_v2(db, str.base, -1, & pStmt0, NULL) == SQLITE_OK)
                    for (int gi = 0, gx = 0, gy = 0, status = sqlite3_step(pStmt0); gi < gN * gN; status = status == SQLITE_ROW ? sqlite3_step(pStmt0) : status, gi++, gx = gi % gN, gy = gi / gN) {
                        lx = gb.lx + gx * (gdim + gpad), ly = gb.uy - gy * (gdim + gpad) - gdim;
                        Ra::Bounds tb = { lx, ly, lx + gdim, ly + gdim };
                        Ra::Bounds bb = { gx == 0 ? b.lx : tb.lx - 0.5f * gpad, gy == gN - 1 ? b.ly : tb.ly - 0.5f * gpad, gx == gN - 1 ? b.ux : tb.ux + 0.5f * gpad, gy == 0 ? b.uy : tb.uy + 0.5f * gpad };
                        Ra::Path bbPath;  bbPath.ref->addBounds(bb);
                        background.addPath(bbPath, Ra::Transform(), bg[((y & 1) ^ (x & 1)) * 2 + ((gy & 1) ^ (gx & 1))], 0.f, 0);
                        Ra::Path fgPath;  fgPath.ref->addBounds(bb.inset(hw * 0.5f, hw * 0.5f));
                        foreground.addPath(fgPath, Ra::Transform(), Ra::Colorant(0, 0, 0, 0), hw, 0);
                        if (status == SQLITE_ROW)
                            tables.emplace_back(
                                (const char *)sqlite3_column_text(pStmt0, 0),
                                tb,
                                sqlite3_column_double(pStmt0, 1));
                    }
                sqlite3_finalize(pStmt0);
            }
            backgroundList.empty().addScene(background);
            foregroundList.empty().addScene(foreground);
            tableLists = std::vector<Ra::SceneList>(tables.size());
            for (int i = 0; i < tables.size(); i++)
                writeTable(*font.ref, tables[i].t, tables[i].bounds, tables[i].name.base, tableLists[i]);
        }
        sqlite3_finalize(pStmt1);
    }
    void writeTable(RasterizerFont& font, float t, Ra::Bounds frame, const char *table, Ra::SceneList& list) {
        Ra::Colorant red(0, 0, 255, 255), black(0, 0, 0, 255), gray(144, 144, 144, 255);
        Ra::Row<char> str;
        str = str + "SELECT * FROM " + table + " LIMIT 1";
        sqlite3_stmt *pStmt0 = NULL, *pStmt1 = NULL;
        if (sqlite3_prepare_v2(db, str.base, -1, & pStmt0, NULL) == SQLITE_OK && sqlite3_step(pStmt0) == SQLITE_ROW) {
            int columns = sqlite3_column_count(pStmt0), lengths[columns], types[columns], total = 0, i, j, status, rows, count, n, range, lower, upper;
            float fw, fh, fs, lx, ux, my, h, uy;
            const char *names[columns];
            for (i = 0; i < columns; i++)
                types[i] = sqlite3_column_type(pStmt0, i), names[i] = sqlite3_column_name(pStmt0, i), lengths[i] = types[i] == SQLITE_TEXT ? kTextChars : strstr(names[i], "_") == NULL && strcmp(names[i], "id") ? kRealChars : 0, total += lengths[i];
            total = total < kTextChars ? kTextChars : total;
            fw = frame.ux - frame.lx, fh = frame.uy - frame.ly;
            fs = fw / (total * font.avg), h = fs * (font.ascent - font.descent + font.lineGap), my = frame.uy - roundf(0.5f * fh / h) * h;
            str = str.empty() + "SELECT COUNT(*) FROM " + table, writeColumnValues(str.base, & count, false);
            rows = ceilf(fh / h), range = ceilf(0.5f * rows), n = t * float(count) + 0.5f * fs * float(font.lineGap) / h;
            n = n > count - 1 ? count - 1 : n;
            lower = n - range, upper = n + range + 1, lower = lower < 0 ? 0 : lower, upper = upper > count ? count : upper;
            uy = my + h * (t * float(count) - lower);
            str = str.empty() + "SELECT ";
            for (int i = 0; i < columns; i++)
                if (types[i] == SQLITE_TEXT)
                    str = str + (i == 0 ? "" : ", ") + "CASE WHEN LENGTH(" + names[i] + ") < 24 THEN " + names[i] + " ELSE SUBSTR(" + names[i] + ", 1, 11) || '…' || SUBSTR(" + names[i] + ", LENGTH(" + names[i] + ") - 11) END AS " + names[i];
                else
                    str = str + (i == 0 ? "" : ", ") + names[i];
            str = str + " FROM " + table + " LIMIT " + lower + ", " + (upper - lower);
            if (sqlite3_prepare_v2(db, str.base, -1, & pStmt1, NULL) == SQLITE_OK) {
                Ra::Scene chrome, rows;
                for (lx = frame.lx, i = 0; i < columns; i++, lx = ux)
                    if (lx != (ux = lx + fw * float(lengths[i]) / float(total)))
                        RasterizerFont::writeGlyphs(font, fs * float(font.unitsPerEm), black, Ra::Bounds(lx, -FLT_MAX, ux, frame.uy), false, true, lengths[i] != kTextChars, names[i], chrome);
                Ra::Path linePath; linePath.ref->moveTo(frame.lx, my), linePath.ref->lineTo(frame.ux, my);
                chrome.addPath(linePath, Ra::Transform(), red, h / 128.f, 0);
                Ra::Transform clip(frame.ux - frame.lx, 0.f, 0.f, frame.uy - frame.ly - h, frame.lx, frame.ly);
                for (j = lower, status = sqlite3_step(pStmt1); status == SQLITE_ROW; status = sqlite3_step(pStmt1), j++, uy -= h)
                    for (lx = frame.lx, i = 0; i < columns; i++, lx = ux)
                        if (lx != (ux = lx + fw * float(lengths[i]) / float(total)))
                            RasterizerFont::writeGlyphs(font, fs * float(font.unitsPerEm), j == n ? red : gray, Ra::Bounds(lx, -FLT_MAX, ux, uy), false, true, lengths[i] != kTextChars, (const char *)sqlite3_column_text(pStmt1, i), rows);
                list.addScene(chrome), list.addScene(rows, Ra::Transform(), clip);
            }
        }
        sqlite3_finalize(pStmt0), sqlite3_finalize(pStmt1);
    }
    bool readEvents(RasterizerState& state) {
        bool redraw = false;
        if (db)
            for (RasterizerState::Event& e : state.events)
                switch (e.type) {
                    case RasterizerState::Event::kMouseMove: {
                        float dx = state.scale * e.x, dy = state.scale * e.y, ux, uy;
                        Ra::Range indices = RasterizerWinding::indicesForPoint(backgroundList, state.view, state.device, dx, dy);
                        int si = indices.begin, pi = indices.end;
                        if (pi != lastpi) {
                            if (lastpi != INT_MAX) { // exit
                                foregroundList.scenes[0].colors[lastpi] = Ra::Colorant(0, 0, 0, 0);
                                redraw = true;
                            }
                            if (pi != INT_MAX) { // enter
                                foregroundList.scenes[0].colors[pi] = Ra::Colorant(0, 0, 0, 255);
                                redraw = true;
                            }
                        }
                        lastpi = pi;
                        if (si != INT_MAX) {
                            Ra::Transform inv = backgroundList.scenes[si].paths[pi]->bounds.unit(state.view.concat(backgroundList.ctms[si])).invert();
                            ux = dx + inv.a + dy * inv.c + inv.tx, uy = dx * inv.b + dy * inv.d + inv.ty;
                            writeTable(*font.ref, uy, tables[pi].bounds, tables[pi].name.base, tableLists[pi].empty());
                            redraw = true;
                        }
                        break;
                    }
                    default:
                        break;
                }
        return redraw;
    }
    void writeList(Ra::SceneList& list) {
        list.addList(backgroundList);
        for (Ra::SceneList& tableList: tableLists)
            list.addList(tableList);
        list.addList(foregroundList);
    }
    sqlite3 *db = nullptr;
    sqlite3_stmt *stmt = nullptr;
    std::vector<Table> tables;
    int lastpi = INT_MAX;
    std::vector<Ra::SceneList> tableLists;
    Ra::SceneList backgroundList, foregroundList;
    Ra::Ref<RasterizerFont> font;
    size_t refCount;
};
