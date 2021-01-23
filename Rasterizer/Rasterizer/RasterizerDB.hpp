//
//  RasterizerDB.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 29/04/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import <stdio.h>
#import <sqlite3.h>
#import <unordered_map>
#import "xxhash.h"
#import "Rasterizer.hpp"
#import "RasterizerFont.hpp"
#import "RasterizerState.hpp"
#import "RasterizerWinding.hpp"

struct RasterizerDB {
    struct Table {
        Table(const char *nm, Ra::Bounds bounds) : bounds(bounds) { name = name + nm; hash = XXH64(name.base, name.end, 0); }
        Ra::Row<char> name;  Ra::Bounds bounds;  size_t hash = 0;
        int columns = 0, total = 0, count = 0;  std::vector<int> types, lengths;  std::vector<uint8_t> opposites;  std::vector<Ra::Row<char>> names;
        Ra::SceneList rows, chrome;
    };
    const Ra::Colorant kBlack = Ra::Colorant(0, 0, 0, 255), kClear = Ra::Colorant(0, 0, 0, 0), kRed = Ra::Colorant(0, 0, 255, 255), kGray = Ra::Colorant(144, 144, 144, 255);
    const static int kTextChars = 12, kRealChars = 2;
    constexpr const static float kLineGap = 0.25f;
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
        Ra::Scene background;  background.tag = 2;
        for (int i = 0, x = 0, y = 0; i < indices.end; i++, x = i % N, y = i / N) {
            Ra::Bounds bounds = { frame.lx + x * dim, frame.uy - (y + 1) * dim, frame.lx + (x + 1) * dim, frame.uy - y * dim };
            Ra::Path bPath;  bPath.ref->addBounds(bounds);  background.addPath(bPath, Ra::Transform(), Ra::Colorant(0, 0, 0, 0), 0.f, 0);
            tables.emplace_back(strings.base + indices.base[i], bounds);
            Table& table = tables.back();
            if (ts.find(table.hash) == ts.end())
                ts[table.hash] = 0.f;
            writeTableMetadata(*font.ref, table);
            writeTableLists(*font.ref, table);
        }
        backgroundList.empty().addScene(background);
    }
    void writeTableMetadata(RasterizerFont& font, Table& table) {
        Ra::Row<char> str;  str = str + "SELECT * FROM " + table.name.base + " LIMIT 1";
        sqlite3_stmt *pStmt = NULL;
        if (sqlite3_prepare_v2(db, str.base, -1, & pStmt, NULL) == SQLITE_OK && sqlite3_step(pStmt) == SQLITE_ROW) {
            table.columns = sqlite3_column_count(pStmt);
            for (int length, i = 0; i < table.columns; i++) {
                table.types.emplace_back(sqlite3_column_type(pStmt, i));
                Ra::Row<char> name;  name = name + sqlite3_column_name(pStmt, i);
                table.names.emplace_back(name);
                length = table.types.back() == SQLITE_TEXT ? kTextChars : kRealChars;
                table.lengths.emplace_back(length);
                table.opposites.emplace_back(strcmp(name.base, "id") && length != kTextChars);
                table.total += length;
            }
        }
        table.total = table.total < kTextChars ? kTextChars : table.total;
        sqlite3_finalize(pStmt);
        str = str.empty() + "SELECT COUNT(*) FROM " + table.name.base, writeColumnInts(str.base, & table.count);
    }
    void writeTableLists(RasterizerFont& font, Table& table) {
        if (font.isEmpty() || table.columns == 0)
            return;
        table.rows.empty(), table.chrome.empty();
        float t = ts[table.hash], fs, my, uh, uy;
        int i, rows, n, range, lower, upper;
        fs = (table.bounds.ux - table.bounds.lx) / table.total;
        uh = fs / font.unitsPerEm * ((1.f + kLineGap) * (font.ascent - font.descent) + font.lineGap);
        rows = ceilf((table.bounds.uy - table.bounds.ly) / uh), range = ceilf(0.5f * rows), n = t * float(table.count);
        my = table.bounds.uy - range * uh;
        n = n > table.count - 1 ? table.count - 1 : n;
        lower = n - range, upper = n + range + 1, lower = lower < 0 ? 0 : lower, upper = upper > table.count ? table.count : upper;
        uy = my + uh * (t * float(table.count) - lower);
        Ra::Row<char> str;  str = str + "SELECT ";
        for (int i = 0; i < table.columns; i++) {
            if (table.types[i] == SQLITE_TEXT)
                str = str + (i == 0 ? "" : ", ") + "CASE WHEN LENGTH(" + table.names[i].base + ") < 24 THEN " + table.names[i].base + " ELSE SUBSTR(" + table.names[i].base + ", 1, 11) || '…' || SUBSTR(" + table.names[i].base + ", LENGTH(" + table.names[i].base + ") - 11) END AS " + table.names[i].base;
            else
                str = str + (i == 0 ? "" : ", ") + table.names[i].base;
        }
        str = str + " FROM " + table.name.base + " LIMIT " + lower + ", " + (upper - lower);
        Ra::Row<size_t> indices;  Ra::Row<char> strings;
        writeColumnStrings(str.base, indices, strings);
        if (indices.end) {
            Ra::Scene rows;
            Ra::Transform clip(table.bounds.ux - table.bounds.lx, 0.f, 0.f, table.bounds.uy - table.bounds.ly - uh, table.bounds.lx, table.bounds.ly);
            RasterizerFont::layoutColumns(font, fs, kLineGap, kBlack, Ra::Bounds(table.bounds.lx, -FLT_MAX, table.bounds.ux, uy), & table.lengths[0], (bool *)& table.opposites[0], table.columns, lower & 1, indices, strings, rows);
            table.rows.addScene(rows, Ra::Transform(), clip);
            Ra::Scene chrome;
            Ra::Row<size_t> hindices;  Ra::Row<char> hstrings;  hstrings.alloc(4096), hstrings.empty();
            for (i = 0; i < table.columns; i++)
                *(hindices.alloc(1)) = hstrings.end, strcpy(hstrings.alloc(strlen(table.names[i].base) + 1), table.names[i].base);
            RasterizerFont::layoutColumns(font, fs, kLineGap, kBlack, Ra::Bounds(table.bounds.lx, -FLT_MAX, table.bounds.ux, table.bounds.uy), & table.lengths[0], (bool *)& table.opposites[0], table.columns, true, hindices, hstrings, chrome);
            Ra::Path linePath; linePath.ref->moveTo(table.bounds.lx, my), linePath.ref->lineTo(table.bounds.ux, my);
            chrome.addPath(linePath, Ra::Transform(), kRed, uh / 64.f, 0);
            table.chrome.addScene(chrome);
        }
    }
    static void EventFunction(Ra::SceneList& list, RasterizerState& state, void *info) {
        RasterizerDB& db = *((RasterizerDB *)info);
        db.readEvents(list, state);
    }
    void readEvents(Ra::SceneList& list, RasterizerState& state) {
        for (RasterizerState::Event& e : state.events)
            if (e.type == RaSt::Event::kMouseDown) {
                Ra::Range indices = RasterizerWinding::indicesForPoint(list, state.view, state.device, state.dx, state.dy, 2);
                if (indices.begin != INT_MAX)
                    downsi = indices.begin, downpi = indices.end;
            } else if (e.type == RaSt::Event::kDragged) {
                if ((state.flags & RaSt::Event::kShift) == 0 && downsi != INT_MAX) {
                    int si = downsi, pi = downpi;
                    Ra::Transform inv = list.scenes[si].paths[pi]->bounds.unit(state.view.concat(list.ctms[si])).invert();
                    ts[tables[pi].hash] = state.dx * inv.b + state.dy * inv.d + inv.ty;
                    writeTableLists(*font.ref, tables[pi]);
                }
            } else if (e.type == RaSt::Event::kMouseMove) {
                Ra::Range indices = downsi != INT_MAX ? Ra::Range(downsi, downpi) : RasterizerWinding::indicesForPoint(list, state.view, state.device, state.dx, state.dy, 2);
                int si = indices.begin, pi = indices.end;
                if (pi != lastpi) {  lastpi = pi; }
                if (si != INT_MAX) {
                    Ra::Transform inv = list.scenes[si].paths[pi]->bounds.unit(state.view.concat(list.ctms[si])).invert();
                    ts[tables[pi].hash] = state.dx * inv.b + state.dy * inv.d + inv.ty;
                    writeTableLists(*font.ref, tables[pi]);
                }
            } else if (e.type == RaSt::Event::kMouseUp)
                downpi = downsi = lastpi = INT_MAX;
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
    std::unordered_map<size_t, float> ts;
    int lastpi = INT_MAX, downsi = INT_MAX, downpi = INT_MAX;
    Ra::SceneList backgroundList;
    Ra::Ref<RasterizerFont> font;
    size_t refCount;
};
