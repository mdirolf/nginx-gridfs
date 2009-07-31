typedef struct {
    int error_code;

    int length;
    const char* data;
    const char* mimetype;
} gridfile_t;

#ifdef __cplusplus
extern "C"
#endif
gridfile_t get_gridfile(const unsigned char* mongod_host, const unsigned char* gridfs_db, const unsigned char* gridfs_root_collection, const unsigned char* filename);
