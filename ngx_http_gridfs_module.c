/*
 * Copyright (C) 2009 Michael Dirolf
 */

static char* ngx_http_gridfs(ngx_conf_t* directive, ngx_command_t* command, void* gridfs_conf);

static nginx_int_t ngx_http_gridfs_handler(ngx_http_request_t* request);

typedef struct {
    ngx_str_t mongod_host;
    ngx_str_t mongod_port;
    ngx_str_t gridfs_db;
    ngx_str_t gridfs_root_collection;
} ngx_http_gridfs_loc_conf_t;

static ngx_command_t ngx_http_gridfs_commands[] = {
    {
        ngx_string("gridfs"),
        NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
        ngx_http_gridfs,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    }

    {
        ngx_string("mongod_host"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_gridfs_loc_conf_t, mongod_host),
        NULL
    },

    {
        ngx_string("mongod_port"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_gridfs_loc_conf_t, mongod_port),
        NULL
    },

    {
        ngx_string("gridfs_db"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_gridfs_loc_conf_t, gridfs_db),
        NULL
    },

    {
        ngx_string("gridfs_root_collection"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_gridfs_loc_conf_t, gridfs_root_collection),
        NULL
    },

    ngx_null_command
};

static ngx_http_module_t ngx_http_gridfs_module_ctx = {
    NULL, /* preconfiguration */
    NULL, /* postconfiguration */
    NULL, /* create main configuration */
    NULL, /* init main configuration */
    NULL, /* create server configuration */
    NULL, /* init serever configuration */
    ngx_http_gridfs_create_loc_conf,
    ngx_http_gridfs_merge_loc_conf
};

static void* ngx_http_gridfs_create_loc_conf(ngx_conf_t* directive) {
    ngx_http_gridfs_loc_conf_t* gridfs_conf;

    gridfs_conf = ngx_pcalloc(directive->pool, sizeof(ngx_http_gridfs_loc_conf_t));
    if (gridfs_conf == NULL) {
        return NGX_CONF_ERROR;
    }
    gridfs_conf->mongodb_host = NGX_CONF_UNSET;
    gridfs_conf->mongodb_port = NGX_CONF_UNSET;
    gridfs_conf->gridfs_db = NGX_CONF_UNSET;
    gridfs_conf->gridfs_root_collection = NGX_CONF_UNSET;
    return conf;
}

static char* ngx_http_gridfs_merge_loc_conf(ngx_conf_t* directive, void* parent, void* child) {
    ngx_conf_merge_str_value(child->mongodb_host, parent->mongodb_host, ngx_string("127.0.0.1"));
    ngx_conf_merge_str_value(child->mongodb_port, parent->mongodb_port, ngx_string("27017"));
    ngx_conf_merge_str_value(child->gridfs_root_collection, parent->gridfs_root_collection, ngx_string("fs"));

    /* TODO requiring a gridfs_db setting - should we provide a default instead? */
    /* ngx_conf_merge_str_value(child->gridfs_db, parent->gridfs_db, ngx_string("gridfs")); */
    if (child->gridfs_db.data == NULL) {
        if (parent->gridfs_db.data) {
            child->gridfs_db.len = parent->gridfs_db.len;
            child->gridfs_db.data = parent->gridfs_db.data;
        }
        else {
            ngx_conf_log_error(NGX_LOG_EMERG, directive, 0,
                               "must provide a gridfs_db setting to use the GridFS module");
            return NGX_CONF_ERROR;
        }
    }
}

static char* ngx_http_gridfs(ngx_conf_t* directive, ngx_command_t* command, void* gridfs_conf) {
    ngx_http_core_loc_conf_t* core_conf;

    core_conf = ngx_http_conf_get_module_loc_conf(directive, ngx_http_core_module);
    core_conf-> handler = ngx_http_gridfs_handler;

    return NGX_CONF_OK;
}

static nginx_int_t ngx_http_gridfs_handler(ngx_http_request_t* request) {

}
