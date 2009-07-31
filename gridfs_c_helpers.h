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
