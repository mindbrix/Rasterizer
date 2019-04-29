//
//  RasterizerSQL.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 29/04/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//

#import "Rasterizer.hpp"
#import "RasterizerTrueType.hpp"
#import <sqlite3.h>

struct RasterizerSQL {
    struct DB {
        ~DB() { close(); }
        void open(const char *filename) {
            status = sqlite3_open(filename, & db);
        }
        void close() {
            if (db != nullptr && status == SQLITE_OK)
                sqlite3_close(db);
            db = nullptr;
        }
        sqlite3_stmt *prepare(const char *zSql, int nByte) {
            sqlite3_stmt *pStmt;
            const char *zTail;
            status =  sqlite3_prepare(db, zSql, nByte, & pStmt, & zTail);
            return pStmt;
        }
        void writeQuery(RasterizerTrueType::Font font, float pointSize, const char *sql, Rasterizer::Scene& scene) {
            sqlite3_stmt *pStmt;
            const char *zTail;
            status = sqlite3_prepare(db, sql, (int)strlen(sql), & pStmt, & zTail);
            if (status == SQLITE_OK) {
                int columns = sqlite3_column_count(pStmt);
                do {
                    status = sqlite3_step(pStmt);
                    if (status == SQLITE_ROW) {
                        for (int i = 0; i < columns; i++) {
                            int type =  sqlite3_column_type(pStmt, i);
                        }
                    }
                } while (status == SQLITE_ROW);
            }
            sqlite3_finalize(pStmt);
        }
        sqlite3 *db = nullptr;
        int status = SQLITE_OK;
    };
};
