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
/*
 * TODO range support http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html#sec14.35
 * TODO single persistent connection
 */
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "gridfs_c_helpers.h"

static void* ngx_http_gridfs_create_loc_conf(ngx_conf_t* directive);

static char* ngx_http_gridfs_merge_loc_conf(ngx_conf_t* directive, void* parent, void* child);

static char* ngx_http_gridfs(ngx_conf_t* directive, ngx_command_t* command, void* gridfs_conf);

static ngx_int_t ngx_http_gridfs_handler(ngx_http_request_t* request);

typedef struct {
    ngx_str_t mongod_host;
    ngx_str_t gridfs_db;
    ngx_str_t gridfs_root_collection;
    ngx_flag_t enable;
} ngx_http_gridfs_loc_conf_t;

static ngx_command_t ngx_http_gridfs_commands[] = {
    /* TODO since we require gridfs_db setting, should that just enable the
     * module as well? */
    {
        ngx_string("gridfs"),
        NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
        ngx_http_gridfs,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },

    {
        ngx_string("mongod_host"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_gridfs_loc_conf_t, mongod_host),
        NULL
    },

    {
        ngx_string("gridfs_db"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_gridfs_loc_conf_t, gridfs_db),
        NULL
    },

    {
        ngx_string("gridfs_root_collection"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
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

ngx_module_t ngx_http_gridfs_module = {
    NGX_MODULE_V1,
    &ngx_http_gridfs_module_ctx,
    ngx_http_gridfs_commands,
    NGX_HTTP_MODULE,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NGX_MODULE_V1_PADDING
};

static void* ngx_http_gridfs_create_loc_conf(ngx_conf_t* directive) {
    ngx_http_gridfs_loc_conf_t* gridfs_conf;

    gridfs_conf = ngx_pcalloc(directive->pool, sizeof(ngx_http_gridfs_loc_conf_t));
    if (gridfs_conf == NULL) {
        return NGX_CONF_ERROR;
    }
    /*    gridfs_conf->mongod_host = NGX_CONF_UNSET;
    gridfs_conf->gridfs_db = NGX_CONF_UNSET;
    gridfs_conf->gridfs_root_collection = NGX_CONF_UNSET;*/
    return gridfs_conf;
}

static char* ngx_http_gridfs_merge_loc_conf(ngx_conf_t* directive, void* void_parent, void* void_child) {
    ngx_http_gridfs_loc_conf_t* parent = void_parent;
    ngx_http_gridfs_loc_conf_t* child = void_child;

    /* TODO do we need to do more error checking here? */
    ngx_conf_merge_str_value(child->mongod_host, parent->mongod_host, "127.0.0.1:27017");
    ngx_conf_merge_str_value(child->gridfs_root_collection, parent->gridfs_root_collection, "fs");
    ngx_conf_merge_value(child->enable, parent->enable, 0);

    /* TODO requiring a gridfs_db setting - should we provide a default instead? */
    /* ngx_conf_merge_str_value(child->gridfs_db, parent->gridfs_db, ngx_string("gridfs")); */
    if (child->gridfs_db.data == NULL) {
        if (parent->gridfs_db.data) {
            child->gridfs_db.len = parent->gridfs_db.len;
            child->gridfs_db.data = parent->gridfs_db.data;
        } else if (child->enable) {
            ngx_conf_log_error(NGX_LOG_EMERG, directive, 0,
                               "Must provide a gridfs_db setting to use the GridFS module");
            return NGX_CONF_ERROR;
        } else { /* TODO what do we do here? */
        }
    }

    return NGX_CONF_OK;
}

static char* ngx_http_gridfs(ngx_conf_t* directive, ngx_command_t* command, void* void_conf) {
    ngx_http_core_loc_conf_t* core_conf;
    ngx_http_gridfs_loc_conf_t* gridfs_conf = void_conf;

    core_conf = ngx_http_conf_get_module_loc_conf(directive, ngx_http_core_module);
    core_conf-> handler = ngx_http_gridfs_handler;

    gridfs_conf->enable = 1;

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_gridfs_handler(ngx_http_request_t* request) {
    ngx_http_gridfs_loc_conf_t* gridfs_conf;
    ngx_http_core_loc_conf_t* core_conf;
    ngx_buf_t* buffer;
    ngx_chain_t out;
    ngx_str_t location_name;
    ngx_str_t full_uri;
    char* filename;

    gridfile_t gridfile;

    gridfs_conf = ngx_http_get_module_loc_conf(request, ngx_http_gridfs_module);
    core_conf = ngx_http_get_module_loc_conf(request, ngx_http_core_module);

    location_name = core_conf->name;
    full_uri = request->uri;
    /* defensive */
    if (full_uri.len < location_name.len) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Invalid location name or uri");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    filename = (char*)malloc(sizeof(char) * (full_uri.len - location_name.len + 1));
    if (filename == NULL) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Failed to allocate filename buffer");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    memcpy(filename, full_uri.data + location_name.len, full_uri.len - location_name.len);
    filename[full_uri.len - location_name.len] = '\0';

    /* TODO url decode filename */

    gridfile = get_gridfile((const char*)gridfs_conf->mongod_host.data,
                            (const char*)gridfs_conf->gridfs_db.data,
                            (const char*)gridfs_conf->gridfs_root_collection.data,
                            filename);

    free(filename);

    if (gridfile.error_code == 1) {
        /* TODO log what exception mongo is throwing */
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Mongo exception");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    if (gridfile.error_code == 2) {
        return NGX_HTTP_NOT_FOUND;
    }

    request->headers_out.status = NGX_HTTP_OK;
    request->headers_out.content_length_n = gridfile.length;
    ngx_http_send_header(request);

    buffer = ngx_pcalloc(request->pool, sizeof(ngx_buf_t));
    if (buffer == NULL) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Failed to allocate response buffer");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    buffer->pos = (u_char*)gridfile.data;
    buffer->last = (u_char*)gridfile.data + gridfile.length;
    buffer->memory = 1;
    buffer->last_buf = 1;

    out.buf = buffer;
    out.next = NULL;

    return ngx_http_output_filter(request, &out);
}
