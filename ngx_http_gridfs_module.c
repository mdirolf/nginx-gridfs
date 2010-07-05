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
/*
 * TODO range support http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html#sec14.35
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "mongo-c-driver/src/mongo.h"
#include "mongo-c-driver/src/gridfs.h"

static void* ngx_http_gridfs_create_loc_conf(ngx_conf_t* directive);

static char* ngx_http_gridfs_merge_loc_conf(ngx_conf_t* directive, void* parent, void* child);

static char* ngx_http_gridfs_db(ngx_conf_t* directive, ngx_command_t* command, void* gridfs_conf);

static char* ngx_http_gridfs_type(ngx_conf_t* directive, ngx_command_t* command, void* gridfs_conf);

static ngx_int_t ngx_http_gridfs_handler(ngx_http_request_t* request);

static void ngx_http_gridfs_cleanup(void* data);

typedef struct {
    ngx_str_t gridfs_db;
    ngx_str_t gridfs_root_collection;
    ngx_str_t gridfs_field;
    ngx_uint_t gridfs_type;  
    ngx_str_t mongod_host;
    ngx_int_t mongod_port;
    ngx_str_t mongod_user;
    ngx_str_t mongod_pass;
    mongo_connection* mongod_conn;
} ngx_http_gridfs_loc_conf_t;

typedef struct {
    mongo_cursor ** cursors;
    ngx_uint_t numchunks;
} ngx_http_gridfs_cleanup_t;

static ngx_int_t ngx_http_gridfs_mongod_connect(ngx_conf_t* directive, ngx_http_gridfs_loc_conf_t* gridfs_conf);

static ngx_command_t ngx_http_gridfs_commands[] = {

    {
        ngx_string("gridfs_db"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_http_gridfs_db,
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

    {
        ngx_string("gridfs_field"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_gridfs_loc_conf_t, gridfs_field),
        NULL
    },

    {
        ngx_string("gridfs_type"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_http_gridfs_type,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_gridfs_loc_conf_t, gridfs_type),
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
        ngx_string("mongod_port"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_gridfs_loc_conf_t, mongod_port),
        NULL
    },

    {
        ngx_string("mongod_user"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_gridfs_loc_conf_t, mongod_user),
        NULL
    },

    {
        ngx_string("mongod_pass"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_gridfs_loc_conf_t, mongod_pass),
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
        ngx_conf_log_error(NGX_LOG_EMERG, directive, 0,
                           "Failed to allocate memory for GridFS Location Config.");
        return NGX_CONF_ERROR;
    }

    gridfs_conf->mongod_conn = ngx_pcalloc(directive->pool, sizeof(mongo_connection));
    if (gridfs_conf->mongod_conn == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, directive, 0,
                           "Failed to allocate memory for Mongo Connection.");
        return NGX_CONF_ERROR;
    }

    gridfs_conf->gridfs_db.data = NULL;
    gridfs_conf->gridfs_root_collection.data = NULL;
    gridfs_conf->gridfs_field.data = NULL;
    gridfs_conf->gridfs_type = NGX_CONF_UNSET_UINT;
    gridfs_conf->mongod_host.data = NULL;
    gridfs_conf->mongod_port = NGX_CONF_UNSET;
    gridfs_conf->mongod_user.data = NULL;
    gridfs_conf->mongod_pass.data = NULL;
    gridfs_conf->mongod_conn->connected = 0;
    return gridfs_conf;
}

static char* ngx_http_gridfs_merge_loc_conf(ngx_conf_t* directive, void* void_parent, void* void_child) {
    ngx_http_gridfs_loc_conf_t* parent = void_parent;
    ngx_http_gridfs_loc_conf_t* child = void_child;

    ngx_conf_merge_str_value(child->gridfs_db, parent->gridfs_db, NULL);
    ngx_conf_merge_str_value(child->gridfs_root_collection, parent->gridfs_root_collection, "fs");
    ngx_conf_merge_str_value(child->gridfs_field, parent->gridfs_field, "_id");
    ngx_conf_merge_uint_value(child->gridfs_type, parent->gridfs_type, bson_oid);
    ngx_conf_merge_str_value(child->mongod_host, parent->mongod_host, "127.0.0.1");
    ngx_conf_merge_value(child->mongod_port, parent->mongod_port, 27017);
    ngx_conf_merge_str_value(child->mongod_user, parent->mongod_user, NULL);
    ngx_conf_merge_str_value(child->mongod_pass, parent->mongod_pass, NULL);
    ngx_conf_merge_ptr_value(child->mongod_conn, parent->mongod_conn, NGX_CONF_UNSET_PTR);

    /* Connect to mongo once the directives are aquired */
    if (child->gridfs_db.data != NULL 
	&& (child->mongod_conn != NULL) && !child->mongod_conn->connected) {
      if (ngx_http_gridfs_mongod_connect(directive, child) == NGX_ERROR) {
	return NGX_CONF_ERROR;
      }
    }

    /* Currently only support for "_id" and "filename" */
    if (child->gridfs_field.data != NULL
        && ngx_strcmp(child->gridfs_field.data, "filename") != 0
        && ngx_strcmp(child->gridfs_field.data, "_id") != 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, directive, 0,
                           "Unsupported Field: %s", child->gridfs_field.data);
        return NGX_CONF_ERROR;
    }
    if (child->gridfs_field.data != NULL
        && ngx_strcmp(child->gridfs_field.data, "filename") == 0
        && child->gridfs_type != bson_string) {
        ngx_conf_log_error(NGX_LOG_EMERG, directive, 0,
                           "Field: filename, must be of Type: string");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK; 
}

static ngx_int_t ngx_http_gridfs_mongod_connect(ngx_conf_t* directive, ngx_http_gridfs_loc_conf_t* gridfs_conf) {
    mongo_connection_options options;
    bson empty;
    bson_bool_t error;
    char* test;

    if (gridfs_conf->mongod_conn->connected) {
        ngx_conf_log_error(NGX_LOG_ERR, directive, 0,
			 "Mongo Connection is duplicate");
        return NGX_ERROR;
    }

    /* Connect to a mongod */
    ngx_cpystrn( (u_char*)options.host, 
		 gridfs_conf->mongod_host.data, 
		 gridfs_conf->mongod_host.len + 1 );
    options.port = gridfs_conf->mongod_port;
    switch (mongo_connect( gridfs_conf->mongod_conn, &options )) {
        case mongo_conn_success:
            break;
        case mongo_conn_bad_arg:
	    ngx_conf_log_error(NGX_LOG_ERR, directive, 0,
			       "Mongo Exception: Bad Arguments");
            return NGX_ERROR;
        case mongo_conn_no_socket:
            ngx_conf_log_error(NGX_LOG_ERR, directive, 0,
			       "Mongo Exception: No Socket");
            return NGX_ERROR;
        case mongo_conn_fail:
            ngx_conf_log_error(NGX_LOG_ERR, directive, 0,
			       "Mongo Exception: Connection Failure %s:%i;",
			       options.host,options.port/*
							  gridfs_conf->mongod_port*/);
            return NGX_ERROR;
        case mongo_conn_not_master:
            ngx_conf_log_error(NGX_LOG_ERR, directive, 0,
			       "Mongo Exception: Not Master");
            return NGX_ERROR;
        default:
            ngx_conf_log_error(NGX_LOG_ERR, directive, 0,
			       "Mongo Exception: Unknown Error");
            return NGX_ERROR;
    }
    
    /* Authenticate with the user and password */
    if (gridfs_conf->mongod_user.data != NULL && gridfs_conf->mongod_pass.data != NULL) {
        if (!mongo_cmd_authenticate(gridfs_conf->mongod_conn, 
				   (const char*)gridfs_conf->gridfs_db.data, 
				   (const char*)gridfs_conf->mongod_user.data, 
				   (const char*)gridfs_conf->mongod_pass.data)) {
	    ngx_conf_log_error(NGX_LOG_ERR, directive, 0,
			       "Invalid mongo user/pass: %s/%s", 
			       gridfs_conf->mongod_user.data, 
			       gridfs_conf->mongod_pass.data);
	    return NGX_ERROR;
        }
    }
    
    /* Run a test command to test authentication. */
    test = (char*)malloc( gridfs_conf->gridfs_db.len + sizeof(".test"));
    ngx_cpystrn((u_char*)test, (u_char*)gridfs_conf->gridfs_db.data, gridfs_conf->gridfs_db.len+1);
    ngx_cpystrn((u_char*)(test+gridfs_conf->gridfs_db.len),(u_char*)".test", sizeof(".test"));
    bson_empty(&empty);
    mongo_find(gridfs_conf->mongod_conn, test, &empty, NULL, 0, 0, 0);
    error =  mongo_cmd_get_last_error(gridfs_conf->mongod_conn, (char*)gridfs_conf->gridfs_db.data, NULL);
    free(test);
    if (error) {
      ngx_conf_log_error(NGX_LOG_ERR, directive, 0, "Authentication Required");
      return NGX_ERROR;
    }

    return NGX_OK;
}

static char* ngx_http_gridfs_db(ngx_conf_t* directive, ngx_command_t* command, void* void_conf) {
    ngx_http_core_loc_conf_t* core_conf;

    core_conf = ngx_http_conf_get_module_loc_conf(directive, ngx_http_core_module);
    core_conf-> handler = ngx_http_gridfs_handler;

    return ngx_conf_set_str_slot(directive, command, void_conf);
}

static char* ngx_http_gridfs_type(ngx_conf_t* directive, ngx_command_t* command, void* void_conf) {
    ngx_http_gridfs_loc_conf_t* gridfs_conf = void_conf;
    ngx_str_t* value;

    value = directive->args->elts;

    if (value[1].len == 0) {
        gridfs_conf->gridfs_type = NGX_CONF_UNSET_UINT;
    }
    else if (ngx_strcasecmp(value[1].data, (u_char *)"objectid") == 0) {
        gridfs_conf->gridfs_type = bson_oid;
    }
    else if (ngx_strcasecmp(value[1].data, (u_char *)"string") == 0) {
        gridfs_conf->gridfs_type = bson_string;
    }
    else if (ngx_strcasecmp(value[1].data, (u_char *)"int") == 0) {
        gridfs_conf->gridfs_type = bson_string;
    }
    else {
        /* Currently only support for "objectid", "string", and "int" */
        ngx_conf_log_error(NGX_LOG_EMERG, directive, 0,
                           "Unsupported Type: %s", (char *)value[1].data);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char h_digit(char hex)
{
    return (hex >= '0' && hex <= '9') ? hex - '0': ngx_tolower(hex)-'a'+10;
}

static int htoi(char* h)
{
    char ok[] = "0123456789AaBbCcDdEeFf";

    if (ngx_strchr(ok, h[0]) == NULL || ngx_strchr(ok,h[1]) == NULL) { return -1; }
    return h_digit(h[0])*16 + h_digit(h[1]);
}

static int url_decode(char * filename)
{
    char * read = filename;
    char * write = filename;
    char hex[3];
    int c;

    hex[2] = '\0';
    while (*read != '\0'){
        if (*read == '%') {
            hex[0] = *(++read);
            if (hex[0] == '\0') return 0;
            hex[1] = *(++read);
            if (hex[1] == '\0') return 0;
            c = htoi(hex);
            if (c == -1) return 0;
            *write = (char)c;
        }
        else *write = *read;
        read++;
        write++;
    }
    *write = '\0';
    return 1;
}

static ngx_int_t ngx_http_gridfs_handler(ngx_http_request_t* request) {
    ngx_http_gridfs_loc_conf_t* gridfs_conf;
    ngx_http_core_loc_conf_t* core_conf;
    ngx_buf_t* buffer;
    ngx_chain_t out;
    ngx_str_t location_name;
    ngx_str_t full_uri;
    char* value;
    gridfs gfs;
    gridfile gfile;
    gridfs_offset length;
    ngx_uint_t chunksize;
    ngx_uint_t numchunks;
    char* contenttype;
    ngx_uint_t i;
    ngx_int_t rc = NGX_OK;
    bson query;
    bson_buffer buf;
    bson_oid_t oid;
    mongo_cursor ** cursors;
    gridfs_offset chunk_len;
    const char * chunk_data;
    bson_iterator it;
    bson chunk;
    ngx_pool_cleanup_t* gridfs_cln;
    ngx_http_gridfs_cleanup_t* gridfs_clndata;

    gridfs_conf = ngx_http_get_module_loc_conf(request, ngx_http_gridfs_module);
    core_conf = ngx_http_get_module_loc_conf(request, ngx_http_core_module);

    location_name = core_conf->name;
    full_uri = request->uri;

    gridfs_cln = ngx_pool_cleanup_add(request->pool, sizeof(ngx_http_gridfs_cleanup_t));
    if (gridfs_cln == NULL) {
      return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    gridfs_cln->handler = ngx_http_gridfs_cleanup;
    gridfs_clndata = gridfs_cln->data;

    /* defensive */
    if (full_uri.len < location_name.len) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Invalid location name or uri.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Extract the value from the uri */
    value = (char*)malloc(sizeof(char) * (full_uri.len - location_name.len + 1));
    if (value == NULL) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Failed to allocate memory for value buffer.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    memcpy(value, full_uri.data + location_name.len, full_uri.len - location_name.len);
    value[full_uri.len - location_name.len] = '\0';

    /* URL Decoding */
    if (!url_decode(value)) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Malformed request.");
        free(value);
        return NGX_HTTP_BAD_REQUEST;
    }

    /* Find the GridFile */
    gridfs_init(gridfs_conf->mongod_conn,
                (const char*)gridfs_conf->gridfs_db.data,
                (const char*)gridfs_conf->gridfs_root_collection.data,
                &gfs);
    bson_buffer_init(&buf);
    switch (gridfs_conf->gridfs_type) {
    case  bson_oid:
        bson_oid_from_string(&oid, value);
        bson_append_oid(&buf, (char*)gridfs_conf->gridfs_field.data, &oid);
        break;
    case bson_int:
      bson_append_int(&buf, (char*)gridfs_conf->gridfs_field.data, ngx_atoi((u_char*)value, strlen(value)));
        break;
    case bson_string:
        bson_append_string(&buf, (char*)gridfs_conf->gridfs_field.data, value);
        break;
    }
    bson_from_buffer(&query, &buf);
    if(!gridfs_find_query(&gfs, &query, &gfile)){
        bson_destroy(&query);
        free(value);
        return NGX_HTTP_NOT_FOUND;
    }
    bson_destroy(&query);
    free(value);

    /* Get information about the file */
    length = gridfile_get_contentlength(&gfile);
    chunksize = gridfile_get_chunksize(&gfile);
    numchunks = gridfile_get_numchunks(&gfile);
    contenttype = (char*)gridfile_get_contenttype(&gfile);

    /* Set the headers */
    request->headers_out.status = NGX_HTTP_OK;
    request->headers_out.content_length_n = length;
    if (contenttype != NULL) {
        request->headers_out.content_type.len = strlen(contenttype);
        request->headers_out.content_type.data = (u_char*)contenttype;
    }
    else ngx_http_set_content_type(request);

    /* Determine if content is gzipped, set headers accordingly */
    if ( gridfile_get_boolean(&gfile,"gzipped") ) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0, gridfile_get_field(&gfile,"gzipped") );
        request->headers_out.content_encoding = ngx_list_push(&request->headers_out.headers);
        if (request->headers_out.content_encoding == NULL) {
            return NGX_ERROR;
        }
        request->headers_out.content_encoding->hash = 1;
        request->headers_out.content_encoding->key.len = sizeof("Content-Encoding") - 1;
        request->headers_out.content_encoding->key.data = (u_char *) "Content-Encoding";
        request->headers_out.content_encoding->value.len = sizeof("gzip") - 1;
        request->headers_out.content_encoding->value.data = (u_char *) "gzip";
    }

    ngx_http_send_header(request);

    if (numchunks == 0) {
        /* Allocate space for the response buffer */
        buffer = ngx_pcalloc(request->pool, sizeof(ngx_buf_t));
        if (buffer == NULL) {
            ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                          "Failed to allocate response buffer");
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
	
	buffer->pos = NULL;
	buffer->last = NULL;
	buffer->memory = 1;
	buffer->last_buf = 1;
	out.buf = buffer;
	out.next = NULL;
	return ngx_http_output_filter(request, &out);
    }
    
    cursors = (mongo_cursor **)ngx_pcalloc(request->pool, sizeof(mongo_cursor *) * numchunks);

    /* Read and serve chunk by chunk */
    for (i = 0; i < numchunks; i++) {

        /* Allocate space for the response buffer */
        buffer = ngx_pcalloc(request->pool, sizeof(ngx_buf_t));
        if (buffer == NULL) {
            ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                          "Failed to allocate response buffer");
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

	/* Fetch the chunk from mongo */
	cursors[i] = gridfile_get_chunks(&gfile, i, 1);
	mongo_cursor_next(cursors[i]);
	chunk = cursors[i]->current;
	bson_find(&it, &chunk, "data");
	chunk_len = bson_iterator_bin_len( &it );
	chunk_data = bson_iterator_bin_data( &it );

        /* Set up the buffer chain */
        buffer->pos = (u_char*)chunk_data;
        buffer->last = (u_char*)chunk_data + chunk_len;
        buffer->memory = 1;
        buffer->last_buf = (i == numchunks-1);
        out.buf = buffer;
        out.next = NULL;

        /* Serve the Chunk */
        rc = ngx_http_output_filter(request, &out);

        /* TODO: More Codes to Catch? */
        if (rc == NGX_ERROR) {
            return NGX_ERROR;
        }
    }

    gridfs_clndata->cursors = cursors;
    gridfs_clndata->numchunks = numchunks;

    return rc;
}

static void ngx_http_gridfs_cleanup(void* data) {
    ngx_http_gridfs_cleanup_t* gridfs_clndata;
    ngx_uint_t i;

    gridfs_clndata = data;

    for (i = 0; i < gridfs_clndata->numchunks; i++) {
        mongo_cursor_destroy(gridfs_clndata->cursors[i]);
    }
}
