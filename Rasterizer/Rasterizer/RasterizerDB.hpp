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
        int columns = 0, total = 0;  std::vector<int> types, lengths;  std::vector<bool> rights;  std::vector<Ra::Row<char>> names;
        Ra::SceneList rows, chrome;
    };
    const Ra::Colorant kBlack = Ra::Colorant(0, 0, 0, 255), kClear = Ra::Colorant(0, 0, 0, 0), kRed = Ra::Colorant(0, 0, 255, 255), kGray = Ra::Colorant(144, 144, 144, 255);
    const static int kTextChars = 12, kRealChars = 2;
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
        str = str + " FROM " + tabs.base, writeColumnInts(str.base, lengths);
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
    void writeColumnInts(const char *sql, int *values) {
        sqlite3_stmt *pStmt = NULL;
        if (sqlite3_prepare_v2(db, sql, -1, & pStmt, NULL) == SQLITE_OK && sqlite3_step(pStmt) == SQLITE_ROW)
            for (int i = 0; i < sqlite3_column_count(pStmt); i++)
                values[i] = sqlite3_column_int(pStmt, i);
        sqlite3_finalize(pStmt);
    }
    void writeColumnStrings(const char *sql, Ra::Row<size_t>& indices, Ra::Row<char>& strings) {
        sqlite3_stmt *pStmt = NULL;
        strings.alloc(4096), strings.empty(), indices.empty();
        if (sqlite3_prepare_v2(db, sql, -1, & pStmt, NULL) == SQLITE_OK)
            for (int status = sqlite3_step(pStmt); status == SQLITE_ROW; status = sqlite3_step(pStmt))
                for (int i = 0; i < sqlite3_column_count(pStmt); i++)
                    *(indices.alloc(1)) = strings.end, strcpy(strings.alloc(sqlite3_column_bytes(pStmt, i) + 1), (const char *)sqlite3_column_text(pStmt, i));
        sqlite3_finalize(pStmt);
    }
    void writeTables(Ra::Bounds frame) {
        tables = std::vector<Table>();
        int count, N;
        writeColumnInts("SELECT COUNT(DISTINCT(tbl_name)) FROM sqlite_master WHERE name NOT LIKE 'sqlite%'", & count), N = ceilf(sqrtf(count));
        float fw = frame.ux - frame.lx, fh = frame.uy - frame.ly, dim = (fh < fw ? fh : fw) / N;
        Ra::Row<size_t> indices;  Ra::Row<char> strings;
        writeColumnStrings("SELECT tbl_name FROM sqlite_master WHERE name NOT LIKE 'sqlite%' ORDER BY tbl_name ASC", indices, strings);
        Ra::Scene background;
        for (int i = 0, x = 0, y = 0; i < indices.end; i++, x = i % N, y = i / N) {
            Ra::Bounds b = { frame.lx + x * dim, frame.uy - (y + 1) * dim, frame.lx + (x + 1) * dim, frame.uy - y * dim };
            Ra::Path bPath;  bPath.ref->addBounds(b);  background.addPath(bPath, Ra::Transform(), Ra::Colorant(0, 0, 0, 0), 0.f, 0);
            tables.emplace_back(strings.base + indices.base[i], b, 0.5f);
            writeTableMetadata(tables.back());
            writeTableLists(*font.ref, tables.back());
        }
        backgroundList.empty().addScene(background);
    }
    void writeTableMetadata(Table& table) {
        Ra::Row<char> str;  str = str + "SELECT * FROM " + table.name.base + " LIMIT 1";
        sqlite3_stmt *pStmt = NULL;
        if (sqlite3_prepare_v2(db, str.base, -1, & pStmt, NULL) == SQLITE_OK && sqlite3_step(pStmt) == SQLITE_ROW) {
            table.columns = sqlite3_column_count(pStmt);
            for (int i = 0; i < table.columns; i++) {
                table.types.emplace_back(sqlite3_column_type(pStmt, i));
                Ra::Row<char> name;  name = name + sqlite3_column_name(pStmt, i);
                table.names.emplace_back(name);
                int length = table.types.back() == SQLITE_TEXT ? kTextChars : strstr(name.base, "_") == NULL && strcmp(name.base, "id") ? kRealChars : 0;
                table.lengths.emplace_back(length);
                table.rights.emplace_back(length != kTextChars);
                table.total += length;
            }
        }
        sqlite3_finalize(pStmt);
    }
    void writeTableLists(RasterizerFont& font, Table& table) {
        if (font.isEmpty())
            return;
        Ra::Row<char> str;
        Ra::Bounds frame = table.bounds;
        table.rows.empty(), table.chrome.empty();
        str = str + "SELECT * FROM " + table.name.base + " LIMIT 1";
        sqlite3_stmt *pStmt = NULL;
        if (sqlite3_prepare_v2(db, str.base, -1, & pStmt, NULL) == SQLITE_OK && sqlite3_step(pStmt) == SQLITE_ROW) {
            int columns = sqlite3_column_count(pStmt), lengths[columns], types[columns], total = 0, i, rows, count, n, range, lower, upper;
            float fw, fh, fs, my, h, uy, gap = 0.125f;
            const char *names[columns];
            bool rights[columns];
            for (i = 0; i < columns; i++)
                types[i] = sqlite3_column_type(pStmt, i), names[i] = sqlite3_column_name(pStmt, i), lengths[i] = types[i] == SQLITE_TEXT ? kTextChars : strstr(names[i], "_") == NULL && strcmp(names[i], "id") ? kRealChars : 0, rights[i] = lengths[i] != kTextChars, total += lengths[i];
            total = total < kTextChars ? kTextChars : total;
            fw = frame.ux - frame.lx, fh = frame.uy - frame.ly;
            fs = fw / (total * font.unitsPerEm), h = fs * ((1.f + gap) * (font.ascent - font.descent) + font.lineGap), my = frame.uy - ceilf(0.5f * fh / h) * h;
            str = str.empty() + "SELECT COUNT(*) FROM " + table.name.base, writeColumnInts(str.base, & count);
            rows = ceilf(fh / h), range = ceilf(0.5f * rows), n = table.t * float(count);
            n = n > count - 1 ? count - 1 : n;
            lower = n - range, upper = n + range + 1, lower = lower < 0 ? 0 : lower, upper = upper > count ? count : upper;
            uy = my + h * (table.t * float(count) - lower);
            str = str.empty() + "SELECT ";
            for (int i = 0; i < columns; i++)
                if (types[i] == SQLITE_TEXT)
                    str = str + (i == 0 ? "" : ", ") + "CASE WHEN LENGTH(" + names[i] + ") < 24 THEN " + names[i] + " ELSE SUBSTR(" + names[i] + ", 1, 11) || '…' || SUBSTR(" + names[i] + ", LENGTH(" + names[i] + ") - 11) END AS " + names[i];
                else
                    str = str + (i == 0 ? "" : ", ") + names[i];
            str = str + " FROM " + table.name.base + " LIMIT " + lower + ", " + (upper - lower);
            Ra::Row<size_t> indices;  Ra::Row<char> strings;
            writeColumnStrings(str.base, indices, strings);
            if (indices.end) {
                Ra::Scene rows;
                Ra::Transform clip(frame.ux - frame.lx, 0.f, 0.f, frame.uy - frame.ly - h, frame.lx, frame.ly);
                RasterizerFont::layoutColumns(font, fw / total, gap, kBlack, Ra::Bounds(frame.lx, -FLT_MAX, frame.ux, uy), lengths, rights, columns, lower & 1, indices, strings, rows);
                table.rows.addScene(rows, Ra::Transform(), clip);
                Ra::Scene chrome;
                Ra::Row<size_t> hindices;  Ra::Row<char> hstrings;  hstrings.alloc(4096), hstrings.empty();
                for (i = 0; i < columns; i++)
                    *(hindices.alloc(1)) = hstrings.end, strcpy(hstrings.alloc(strlen(names[i]) + 1), names[i]);
                RasterizerFont::layoutColumns(font, fw / total, gap, kBlack, Ra::Bounds(frame.lx, -FLT_MAX, frame.ux, frame.uy), lengths, rights, columns, false, hindices, hstrings, chrome);
                Ra::Path linePath; linePath.ref->moveTo(frame.lx, my), linePath.ref->lineTo(frame.ux, my);
                chrome.addPath(linePath, Ra::Transform(), kRed, h / 64.f, 0);
                table.chrome.addScene(chrome);
            }
        }
        sqlite3_finalize(pStmt);
    }
    static void EventFunction(RasterizerState& state, void *info) {
        RasterizerDB& db = *((RasterizerDB *)info);
        db.readEvents(state);
    }
    void readEvents(RasterizerState& state) {
        for (RasterizerState::Event& e : state.events)
            if (e.type == RaSt::Event::kMouseMove) {
                float dx = state.scale * e.x, dy = state.scale * e.y;
                Ra::Range indices = RasterizerWinding::indicesForPoint(backgroundList, state.view, state.device, dx, dy);
                int si = indices.begin, pi = indices.end;
                if (pi != lastpi) {  lastpi = pi; }
                if (si != INT_MAX) {
                    Ra::Transform inv = backgroundList.scenes[si].paths[pi]->bounds.unit(state.view.concat(backgroundList.ctms[si])).invert();
                    tables[pi].t = dx * inv.b + dy * inv.d + inv.ty;
                    writeTableLists(*font.ref, tables[pi]);
                }
            }
    }
    static void WriteFunction(Ra::SceneList& list, void *info) {
        RasterizerDB& db = *((RasterizerDB *)info);
        list.addList(db.backgroundList);
        for (Table& table : db.tables)
            list.addList(table.rows), list.addList(table.chrome);
    }
    sqlite3 *db = nullptr;
    sqlite3_stmt *stmt = nullptr;
    std::vector<Table> tables;
    int lastpi = INT_MAX;
    Ra::SceneList backgroundList;
    Ra::Ref<RasterizerFont> font;
    size_t refCount;
};
