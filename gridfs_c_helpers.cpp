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

    std::ostringstream oss;
    gridfile.write(oss);
    std::string contents = oss.str();

    gridfile_t result = {
        0,
        contents.size(),
        contents.c_str(),
        "text/plain"
    };

    return result;
}
