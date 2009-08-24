/*
 * Copyright 2009 Michael Dirolf
 *
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
 */

typedef struct {
    /*
     * 0 - okay
     * 1 - mongo exception (maybe couldn't connect)
     * 2 - file doesn't exist
     */
    int error_code;

    int length;
    const char* data;
    const char* mimetype;
} gridfile_t;

#ifdef __cplusplus
extern "C"
#endif
gridfile_t get_gridfile(const char* mongod_host, const char* gridfs_db, const char* gridfs_root_collection, const char* filename);
