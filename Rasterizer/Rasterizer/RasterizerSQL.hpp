//
//  RasterizerSQL.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 29/04/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//

#import "Rasterizer.hpp"
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
        sqlite3 *db = nullptr;
        int status = SQLITE_OK;
    };
};
