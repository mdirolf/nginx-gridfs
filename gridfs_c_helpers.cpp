#include <string.h>

#include "gridfs_c_helpers.h"

extern "C" gridfile_t get_gridfile(const unsigned char* mongod_host, const unsigned char* mongod_port, const unsigned char* gridfs_db, const unsigned char* gridfs_root_collection, const unsigned char* filename) {
    gridfile_t gridfile = {
        0,
        strlen((const char*)filename),
        (const char*)filename,
        (const char*)"text/plain"
    };

    return gridfile;
}
