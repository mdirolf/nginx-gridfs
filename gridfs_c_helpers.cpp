/*
 * Copyright 2009-2010 Michael Dirolf
 *
 * Dual Licensed under the Apache License, Version 2.0 and the GNU
 * General Public License, version 2 or (at your option) any later
 * version.
 *
 * -- Apache License
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * -- GNU GPL
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "client/dbclient.h"
#include "client/gridfs.h"

#include "gridfs_c_helpers.h"

extern "C" gridfile_t get_gridfile(const char* mongod_host, const char* gridfs_db, const char* gridfs_root_collection, const char* filename) {
    gridfile_t mongo_exception = {
        1,
        0,
        "",
        ""
    };
    gridfile_t no_file = {
        2,
        0,
        "",
        ""
    };

    mongo::DBClientConnection connection;

    try {
        connection.connect(std::string(mongod_host));
    } catch (mongo::DBException &e) {
        return mongo_exception;
    }

    mongo::GridFS gridfs(connection, std::string(gridfs_db), std::string(gridfs_root_collection));
    mongo::GridFile gridfile = gridfs.findFile(std::string(filename));

    if (!gridfile.exists()) {
        return no_file;
    }

    std::stringstream oss (std::stringstream::in | std::stringstream::out | std::stringstream::binary);
    gridfile.write(oss);

    char* buffer;
    long size = gridfile.getContentLength();
    buffer = new char[size];
    oss.read(buffer, size);

    gridfile_t result = {
        0,
        size,
        buffer,
        gridfile.getContentType().c_str()
    };

    return result;
}

extern "C" void gridfile_delete(const char* data) {
    delete[] data;
}
