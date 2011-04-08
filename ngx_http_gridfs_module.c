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
#include <signal.h>
#include <stdio.h>

#define MONGO_MAX_RETRIES_PER_REQUEST 1
#define MONGO_RECONNECT_WAITTIME 500 //ms
#define TRUE 1
#define FALSE 0

/* Parse config directive */
static char * ngx_http_mongo(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy);

/* Parse config directive */
static char* ngx_http_gridfs(ngx_conf_t* directive, ngx_command_t* command, void* gridfs_conf);

static void* ngx_http_gridfs_create_main_conf(ngx_conf_t* directive);

static void* ngx_http_gridfs_create_loc_conf(ngx_conf_t* directive);

static char* ngx_http_gridfs_merge_loc_conf(ngx_conf_t* directive, void* parent, void* child);

static ngx_int_t ngx_http_gridfs_init_worker(ngx_cycle_t* cycle);

static ngx_int_t ngx_http_gridfs_handler(ngx_http_request_t* request);

static void ngx_http_gridfs_cleanup(void* data);

typedef struct {
    ngx_str_t db;
    ngx_str_t root_collection;
    ngx_str_t field;
    ngx_uint_t type;
    ngx_str_t user;
    ngx_str_t pass;
    ngx_str_t mongo;
    ngx_array_t* mongods; /* ngx_http_mongod_server_t */
    ngx_str_t replset; /* Name of the replica set, if connecting. */
} ngx_http_gridfs_loc_conf_t;

typedef struct {
    ngx_str_t db;
    ngx_str_t user;
    ngx_str_t pass;
} ngx_http_mongo_auth_t;

typedef struct {
    ngx_str_t name;
    mongo_connection conn;
    ngx_array_t *auths; /* ngx_http_mongo_auth_t */
} ngx_http_mongo_connection_t;

/* Maybe we should store a list of addresses instead. */
typedef struct {
    ngx_str_t host;
    in_port_t port;
} ngx_http_mongod_server_t;

typedef struct {
    ngx_array_t loc_confs; /* ngx_http_gridfs_loc_conf_t */
} ngx_http_gridfs_main_conf_t;

typedef struct {
    mongo_cursor ** cursors;
    ngx_uint_t numchunks;
} ngx_http_gridfs_cleanup_t;

/* Array specifying how to handle configuration directives. */
static ngx_command_t ngx_http_gridfs_commands[] = {

    {
        ngx_string("mongo"),
        NGX_HTTP_LOC_CONF | NGX_CONF_1MORE,
        ngx_http_mongo,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },

    {
        ngx_string("gridfs"),
        NGX_HTTP_LOC_CONF | NGX_CONF_1MORE,
        ngx_http_gridfs,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },

    ngx_null_command
};

/* Module context. */
static ngx_http_module_t ngx_http_gridfs_module_ctx = {
    NULL, /* preconfiguration */
    NULL, /* postconfiguration */
    ngx_http_gridfs_create_main_conf,
    NULL, /* init main configuration */
    NULL, /* create server configuration */
    NULL, /* init serever configuration */
    ngx_http_gridfs_create_loc_conf,
    ngx_http_gridfs_merge_loc_conf
};

/* Module definition. */
ngx_module_t ngx_http_gridfs_module = {
    NGX_MODULE_V1,
    &ngx_http_gridfs_module_ctx,
    ngx_http_gridfs_commands,
    NGX_HTTP_MODULE,
    NULL,
    NULL,
    ngx_http_gridfs_init_worker,
    NULL,
    NULL,
    NULL,
    NULL,
    NGX_MODULE_V1_PADDING
};

ngx_array_t ngx_http_mongo_connections;

/* Parse the 'mongo' directive. */
static char * ngx_http_mongo(ngx_conf_t *cf, ngx_command_t *cmd, void *void_conf) {
    ngx_str_t *value;
    ngx_url_t u;
    ngx_uint_t i;
    ngx_uint_t start;
    ngx_http_mongod_server_t *mongod_server;
    ngx_http_gridfs_loc_conf_t *gridfs_loc_conf;

    gridfs_loc_conf = void_conf;

    value = cf->args->elts;
    gridfs_loc_conf->mongo = value[1];
    gridfs_loc_conf->mongods = ngx_array_create(cf->pool, 7,
                                                sizeof(ngx_http_mongod_server_t));
    if (gridfs_loc_conf->mongods == NULL) {
        return NULL;
    }

    /* If nelts is greater than 3, then the user has specified more than one
     * setting in the 'mongo' directive. So we assume that we're connecting
     * to a replica set and that the first string of the directive is the replica
     * set name. We also start looking for host-port pairs at position 2; otherwise,
     * we start at position 1.
     */
    if( cf->args->nelts >= 3 ) {
        gridfs_loc_conf->replset.len = strlen( (char *)(value + 1)->data );
        gridfs_loc_conf->replset.data = ngx_pstrdup( cf->pool, value + 1 );
        start = 2;
    } else
        start = 1;

    for (i = start; i < cf->args->nelts; i++) {

        ngx_memzero(&u, sizeof(ngx_url_t));

        u.url = value[i];
        u.default_port = 27017;

        if (ngx_parse_url(cf->pool, &u) != NGX_OK) {
            if (u.err) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "%s in mongo \"%V\"", u.err, &u.url);
            }
            return NGX_CONF_ERROR;
        }

        mongod_server = ngx_array_push(gridfs_loc_conf->mongods);
        mongod_server->host = u.host;
        mongod_server->port = u.port;

    }

    return NGX_CONF_OK;
}

/* Parse the 'gridfs' directive. */
static char* ngx_http_gridfs(ngx_conf_t* cf, ngx_command_t* command, void* void_conf) {
    ngx_http_gridfs_loc_conf_t *gridfs_loc_conf = void_conf;
    ngx_http_core_loc_conf_t* core_conf;
    ngx_str_t *value, type;
    volatile ngx_uint_t i;

    core_conf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    core_conf-> handler = ngx_http_gridfs_handler;

    value = cf->args->elts;
    gridfs_loc_conf->db = value[1];

    /* Parse the parameters */
    for (i = 2; i < cf->args->nelts; i++) {
        if (ngx_strncmp(value[i].data, "root_collection=", 16) == 0) { 
            gridfs_loc_conf->root_collection.data = (u_char *) &value[i].data[16];
            gridfs_loc_conf->root_collection.len = ngx_strlen(&value[i].data[16]);
            continue;
        }

        if (ngx_strncmp(value[i].data, "field=", 6) == 0) {
            gridfs_loc_conf->field.data = (u_char *) &value[i].data[6];
            gridfs_loc_conf->field.len = ngx_strlen(&value[i].data[6]);

            /* Currently only support for "_id" and "filename" */
            if (gridfs_loc_conf->field.data != NULL
                && ngx_strcmp(gridfs_loc_conf->field.data, "filename") != 0
                && ngx_strcmp(gridfs_loc_conf->field.data, "_id") != 0) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "Unsupported Field: %s", gridfs_loc_conf->field.data);
                return NGX_CONF_ERROR;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "type=", 5) == 0) { 
            type = (ngx_str_t) ngx_string(&value[i].data[5]);

            /* Currently only support for "objectid", "string", and "int" */
            if (type.len == 0) {
                gridfs_loc_conf->type = NGX_CONF_UNSET_UINT;
            } else if (ngx_strcasecmp(type.data, (u_char *)"objectid") == 0) {
                gridfs_loc_conf->type = bson_oid;
            } else if (ngx_strcasecmp(type.data, (u_char *)"string") == 0) {
                gridfs_loc_conf->type = bson_string;
            } else if (ngx_strcasecmp(type.data, (u_char *)"int") == 0) {
                gridfs_loc_conf->type = bson_int;
            } else {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "Unsupported Type: %s", (char *)value[i].data);
                return NGX_CONF_ERROR;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "user=", 5) == 0) { 
            gridfs_loc_conf->user.data = (u_char *) &value[i].data[5];
            gridfs_loc_conf->user.len = ngx_strlen(&value[i].data[5]);
            continue;
        }

        if (ngx_strncmp(value[i].data, "pass=", 5) == 0) {
            gridfs_loc_conf->pass.data = (u_char *) &value[i].data[5];
            gridfs_loc_conf->pass.len = ngx_strlen(&value[i].data[5]);
            continue;
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid parameter \"%V\"", &value[i]);
        return NGX_CONF_ERROR;
    }

    if (gridfs_loc_conf->field.data != NULL
        && ngx_strcmp(gridfs_loc_conf->field.data, "filename") == 0
        && gridfs_loc_conf->type != bson_string) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "Field: filename, must be of Type: string");
        return NGX_CONF_ERROR;
    }

    if ((gridfs_loc_conf->user.data == NULL || gridfs_loc_conf->user.len == 0)
        && !(gridfs_loc_conf->pass.data == NULL || gridfs_loc_conf->pass.len == 0)) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "Password without username");
        return NGX_CONF_ERROR;
    }

    if (!(gridfs_loc_conf->user.data == NULL || gridfs_loc_conf->user.len == 0)
        && (gridfs_loc_conf->pass.data == NULL || gridfs_loc_conf->pass.len == 0)) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "Username without password");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static void *ngx_http_gridfs_create_main_conf(ngx_conf_t *cf) {
    ngx_http_gridfs_main_conf_t  *gridfs_main_conf;

    gridfs_main_conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_gridfs_main_conf_t));
    if (gridfs_main_conf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&gridfs_main_conf->loc_confs, cf->pool, 4,
                       sizeof(ngx_http_gridfs_loc_conf_t *))
        != NGX_OK) {
        return NULL;
    }

    return gridfs_main_conf;
}

static void* ngx_http_gridfs_create_loc_conf(ngx_conf_t* directive) {
    ngx_http_gridfs_loc_conf_t* gridfs_conf;

    gridfs_conf = ngx_pcalloc(directive->pool, sizeof(ngx_http_gridfs_loc_conf_t));
    if (gridfs_conf == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, directive, 0,
                           "Failed to allocate memory for GridFS Location Config.");
        return NGX_CONF_ERROR;
    }

    gridfs_conf->db.data = NULL;
    gridfs_conf->db.len = 0;
    gridfs_conf->root_collection.data = NULL;
    gridfs_conf->root_collection.len = 0;
    gridfs_conf->field.data = NULL;
    gridfs_conf->field.len = 0;
    gridfs_conf->type = NGX_CONF_UNSET_UINT;
    gridfs_conf->user.data = NULL;
    gridfs_conf->user.len = 0;
    gridfs_conf->pass.data = NULL;
    gridfs_conf->pass.len = 0;
    gridfs_conf->mongo.data = NULL;
    gridfs_conf->mongo.len = 0;
    gridfs_conf->mongods = NGX_CONF_UNSET_PTR;

    return gridfs_conf;
}

static char* ngx_http_gridfs_merge_loc_conf(ngx_conf_t* cf, void* void_parent, void* void_child) {
    ngx_http_gridfs_loc_conf_t *parent = void_parent;
    ngx_http_gridfs_loc_conf_t *child = void_child;
    ngx_http_gridfs_main_conf_t *gridfs_main_conf = ngx_http_conf_get_module_main_conf(cf, ngx_http_gridfs_module);
    ngx_http_gridfs_loc_conf_t **gridfs_loc_conf;
    ngx_http_mongod_server_t *mongod_server;

    ngx_conf_merge_str_value(child->db, parent->db, NULL);
    ngx_conf_merge_str_value(child->root_collection, parent->root_collection, "fs");
    ngx_conf_merge_str_value(child->field, parent->field, "_id");
    ngx_conf_merge_uint_value(child->type, parent->type, bson_oid);
    ngx_conf_merge_str_value(child->user, parent->user, NULL);
    ngx_conf_merge_str_value(child->pass, parent->pass, NULL);
    ngx_conf_merge_str_value(child->mongo, parent->mongo, "127.0.0.1:27017");

    if (child->mongods == NGX_CONF_UNSET_PTR) {
        if (parent->mongods != NGX_CONF_UNSET_PTR) {
            child->mongods = parent->mongods;
        } else {
            child->mongods = ngx_array_create(cf->pool, 4,
                                              sizeof(ngx_http_mongod_server_t));
            mongod_server = ngx_array_push(child->mongods);
            mongod_server->host.data = (u_char *)"127.0.0.1";
            mongod_server->host.len = sizeof("127.0.0.1") - 1;
            mongod_server->port = 27017;
        }
    }

    // Add the local gridfs conf to the main gridfs conf
    if (child->db.data) {
        gridfs_loc_conf = ngx_array_push(&gridfs_main_conf->loc_confs);
        *gridfs_loc_conf = child;
    }

    return NGX_CONF_OK;
}

ngx_http_mongo_connection_t* ngx_http_get_mongo_connection( ngx_str_t name ) {
    ngx_http_mongo_connection_t *mongo_conns;
    ngx_uint_t i;

    mongo_conns = ngx_http_mongo_connections.elts;

    for ( i = 0; i < ngx_http_mongo_connections.nelts; i++ ) {
        if ( name.len == mongo_conns[i].name.len
             && ngx_strncmp(name.data, mongo_conns[i].name.data, name.len) == 0 ) {
            return &mongo_conns[i];
        }
    }

    return NULL;
}

static ngx_int_t ngx_http_mongo_authenticate(ngx_log_t *log, ngx_http_gridfs_loc_conf_t *gridfs_loc_conf) {
    ngx_http_mongo_connection_t* mongo_conn;
    ngx_http_mongo_auth_t *mongo_auth;
    bson empty;
    char *test;
    int error;

    mongo_conn = ngx_http_get_mongo_connection( gridfs_loc_conf->mongo );
    if (mongo_conn == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                  "Mongo Connection not found: \"%V\"", &gridfs_loc_conf->mongo);
    }

    // Authenticate
    if (gridfs_loc_conf->user.data != NULL && gridfs_loc_conf->pass.data != NULL) {
        if (!mongo_cmd_authenticate( &mongo_conn->conn, 
                                     (const char*)gridfs_loc_conf->db.data, 
                                     (const char*)gridfs_loc_conf->user.data, 
                                     (const char*)gridfs_loc_conf->pass.data )) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Invalid mongo user/pass: %s/%s", 
                          gridfs_loc_conf->user.data, 
                          gridfs_loc_conf->pass.data);
            return NGX_ERROR;
        }

        mongo_auth = ngx_array_push(mongo_conn->auths);
        mongo_auth->db = gridfs_loc_conf->db;
        mongo_auth->user = gridfs_loc_conf->user;
        mongo_auth->pass = gridfs_loc_conf->pass;
    }

    // Run a test command to test authentication.
    test = (char*)malloc( gridfs_loc_conf->db.len + sizeof(".test"));
    ngx_cpystrn((u_char*)test, (u_char*)gridfs_loc_conf->db.data, gridfs_loc_conf->db.len+1);
    ngx_cpystrn((u_char*)(test+gridfs_loc_conf->db.len),(u_char*)".test", sizeof(".test"));
    bson_empty(&empty);
    mongo_find(&mongo_conn->conn, test, &empty, NULL, 0, 0, 0);
    error =  mongo_cmd_get_last_error(&mongo_conn->conn, (char*)gridfs_loc_conf->db.data, NULL);
    free(test);
    if (error) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "Authentication Required");
        return NGX_ERROR;
    }

    return NGX_OK;
}

static ngx_int_t ngx_http_mongo_add_connection(ngx_cycle_t* cycle, ngx_http_gridfs_loc_conf_t* gridfs_loc_conf) {
    ngx_http_mongo_connection_t* mongo_conn;
    mongo_conn_return status;
    ngx_http_mongod_server_t *mongods;
    volatile ngx_uint_t i;
    u_char host[255];

    mongods = gridfs_loc_conf->mongods->elts;

    mongo_conn = ngx_http_get_mongo_connection( gridfs_loc_conf->mongo );
    if (mongo_conn != NULL) {
        return NGX_OK;
    }

    mongo_conn = ngx_array_push(&ngx_http_mongo_connections);
    if (mongo_conn == NULL) {
        return NGX_ERROR;
    }

    mongo_conn->name = gridfs_loc_conf->mongo;
    mongo_conn->auths = ngx_array_create(cycle->pool, 4, sizeof(ngx_http_mongo_auth_t));

    if ( gridfs_loc_conf->mongods->nelts == 1 ) {
        ngx_cpystrn( host, mongods[0].host.data, mongods[0].host.len + 1 );
        status = mongo_connect( &mongo_conn->conn, (const char*)host, mongods[0].port );
    } else if ( gridfs_loc_conf->mongods->nelts >= 2 && gridfs_loc_conf->mongods->nelts < 9 ) {

        /* Initiate replica set connection. */
        mongo_replset_init_conn( &mongo_conn->conn, (const char *)gridfs_loc_conf->replset.data );

        /* Add replica set seeds. */
        for( i=0; i<gridfs_loc_conf->mongods->nelts; ++i ) {
            ngx_cpystrn( host, mongods[i].host.data, mongods[i].host.len + 1 );
            mongo_replset_add_seed( &mongo_conn->conn, (const char *)host, mongods[i].port );
        }
        status = mongo_replset_connect( &mongo_conn->conn );
    } else {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Nginx Exception: Too many strings provided in 'mongo' directive.");
        return NGX_ERROR;
    }

    switch (status) {
        case mongo_conn_success:
            break;
        case mongo_conn_bad_arg:
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Exception: Bad Arguments");
            return NGX_ERROR;
        case mongo_conn_no_socket:
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Exception: No Socket");
            return NGX_ERROR;
        case mongo_conn_fail:
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Exception: Connection Failure.");
            return NGX_ERROR;
        case mongo_conn_not_master:
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Exception: Not Master");
            return NGX_ERROR;
        case mongo_conn_bad_set_name:
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Exception: Replica set name %s does not match.", gridfs_loc_conf->replset.data);
            return NGX_ERROR;
        case mongo_conn_cannot_find_primary:
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Exception: Cannot connect to primary node.");
            return NGX_ERROR;
        default:
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "Mongo Exception: Unknown Error");
            return NGX_ERROR;
    }

    return NGX_OK;
}

static ngx_int_t ngx_http_gridfs_init_worker(ngx_cycle_t* cycle) {
    ngx_http_gridfs_main_conf_t* gridfs_main_conf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_gridfs_module);
    ngx_http_gridfs_loc_conf_t** gridfs_loc_confs;
    ngx_uint_t i;

    signal(SIGPIPE, SIG_IGN);

    gridfs_loc_confs = gridfs_main_conf->loc_confs.elts;

    ngx_array_init(&ngx_http_mongo_connections, cycle->pool, 4, sizeof(ngx_http_mongo_connection_t));

    for (i = 0; i < gridfs_main_conf->loc_confs.nelts; i++) {
        if (ngx_http_mongo_add_connection(cycle, gridfs_loc_confs[i]) == NGX_ERROR) {
            return NGX_ERROR;
        }
        if (ngx_http_mongo_authenticate(cycle->log, gridfs_loc_confs[i]) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

static ngx_int_t ngx_http_mongo_reconnect(ngx_log_t *log, ngx_http_mongo_connection_t *mongo_conn) {
    volatile mongo_conn_return status = mongo_conn_fail;

    MONGO_TRY_GENERIC(&mongo_conn->conn) {
        if(&mongo_conn->conn.connected) { mongo_disconnect(&mongo_conn->conn); }
        ngx_msleep(MONGO_RECONNECT_WAITTIME);
        status = mongo_reconnect(&mongo_conn->conn);
    } MONGO_CATCH_GENERIC(&mongo_conn->conn) { 
        status = mongo_conn_fail;
    }

    switch (status) {
        case mongo_conn_success:
            break;
        case mongo_conn_bad_arg:
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Mongo Exception: Bad Arguments");
            return NGX_ERROR;
        case mongo_conn_no_socket:
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Mongo Exception: No Socket");
            return NGX_ERROR;
        case mongo_conn_fail:
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Mongo Exception: Connection Failure %s:%i;",
                          mongo_conn->conn.primary->host,
                          mongo_conn->conn.primary->port);
            return NGX_ERROR;
        case mongo_conn_not_master:
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Mongo Exception: Not Master");
            return NGX_ERROR;
        default:
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Mongo Exception: Unknown Error");
            return NGX_ERROR;
    }
    
    return NGX_OK;
}

static ngx_int_t ngx_http_mongo_reauth(ngx_log_t *log, ngx_http_mongo_connection_t *mongo_conn) {
    ngx_http_mongo_auth_t *auths;
    volatile ngx_uint_t i, success = 0;
    auths = mongo_conn->auths->elts;

    for (i = 0; i < mongo_conn->auths->nelts; i++) {
        MONGO_TRY_GENERIC(&mongo_conn->conn)  {
            success = mongo_cmd_authenticate( &mongo_conn->conn, 
                                              (const char*)auths[i].db.data, 
                                              (const char*)auths[i].user.data, 
                                              (const char*)auths[i].pass.data );
        } MONGO_CATCH_GENERIC(&mongo_conn->conn) { 
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Mongo connection error, during reauth");
            return NGX_ERROR;
        }
        if (!success) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "Invalid mongo user/pass: %s/%s, during reauth", 
                          auths[i].user.data, 
                          auths[i].pass.data);   
            return NGX_ERROR;
        }
    }
    
    return NGX_OK;
}

static char h_digit(char hex) {
    return (hex >= '0' && hex <= '9') ? hex - '0': ngx_tolower(hex)-'a'+10;
}

static int htoi(char* h) {
    char ok[] = "0123456789AaBbCcDdEeFf";

    if (ngx_strchr(ok, h[0]) == NULL || ngx_strchr(ok,h[1]) == NULL) { return -1; }
    return h_digit(h[0])*16 + h_digit(h[1]);
}

static int url_decode(char * filename) {
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
    ngx_http_mongo_connection_t *mongo_conn;
    gridfs gfs;
    gridfile gfile;
    gridfs_offset length;
    ngx_uint_t chunksize;
    ngx_uint_t numchunks;
    char* contenttype;
    volatile ngx_uint_t i;
    volatile ngx_int_t found = 0;
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
    volatile ngx_uint_t e = FALSE; 
    volatile ngx_uint_t ecounter = 0;

    gridfs_conf = ngx_http_get_module_loc_conf(request, ngx_http_gridfs_module);
    core_conf = ngx_http_get_module_loc_conf(request, ngx_http_core_module);

    // ---------- ENSURE MONGO CONNECTION ---------- //

    mongo_conn = ngx_http_get_mongo_connection( gridfs_conf->mongo );
    if (mongo_conn == NULL) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Mongo Connection not found: \"%V\"", &gridfs_conf->mongo);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    
    if ( !(&mongo_conn->conn.connected)
         && (ngx_http_mongo_reconnect(request->connection->log, mongo_conn) == NGX_ERROR
             || ngx_http_mongo_reauth(request->connection->log, mongo_conn) == NGX_ERROR)) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Could not connect to mongo: \"%V\"", &gridfs_conf->mongo);
        if(&mongo_conn->conn.connected) { mongo_disconnect(&mongo_conn->conn); }
        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }


    // ---------- RETRIEVE KEY ---------- //

    location_name = core_conf->name;
    full_uri = request->uri;

    if (full_uri.len < location_name.len) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Invalid location name or uri.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    value = (char*)malloc(sizeof(char) * (full_uri.len - location_name.len + 1));
    if (value == NULL) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Failed to allocate memory for value buffer.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    memcpy(value, full_uri.data + location_name.len, full_uri.len - location_name.len);
    value[full_uri.len - location_name.len] = '\0';

    if (!url_decode(value)) {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Malformed request.");
        free(value);
        return NGX_HTTP_BAD_REQUEST;
    }

    // ---------- RETRIEVE GRIDFILE ---------- //

    do {
        MONGO_TRY_GENERIC(&mongo_conn->conn){
            e = FALSE;
            gridfs_init(&mongo_conn->conn,
                        (const char*)gridfs_conf->db.data,
                        (const char*)gridfs_conf->root_collection.data,
                        &gfs);
        } MONGO_CATCH_GENERIC(&mongo_conn->conn) {
            e = TRUE; ecounter++;
            if (ecounter > MONGO_MAX_RETRIES_PER_REQUEST
                || ngx_http_mongo_reconnect(request->connection->log, mongo_conn) == NGX_ERROR
                || ngx_http_mongo_reauth(request->connection->log, mongo_conn) == NGX_ERROR) {
                ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                              "Mongo connection dropped, could not reconnect");
                if(&mongo_conn->conn.connected) { mongo_disconnect(&mongo_conn->conn); }
                return NGX_HTTP_SERVICE_UNAVAILABLE;
            }
        }
    } while (e);

    bson_buffer_init(&buf);
    switch (gridfs_conf->type) {
    case  bson_oid:
        bson_oid_from_string(&oid, value);
        bson_append_oid(&buf, (char*)gridfs_conf->field.data, &oid);
        break;
    case bson_int:
      bson_append_int(&buf, (char*)gridfs_conf->field.data, ngx_atoi((u_char*)value, strlen(value)));
        break;
    case bson_string:
        bson_append_string(&buf, (char*)gridfs_conf->field.data, value);
        break;
    }
    bson_from_buffer(&query, &buf);

    do {
        MONGO_TRY_GENERIC(&mongo_conn->conn){
            e = FALSE;
            found = gridfs_find_query(&gfs, &query, &gfile);
        } MONGO_CATCH_GENERIC(&mongo_conn->conn) {
            e = TRUE; ecounter++;
            if (ecounter > MONGO_MAX_RETRIES_PER_REQUEST 
                || ngx_http_mongo_reconnect(request->connection->log, mongo_conn) == NGX_ERROR
                || ngx_http_mongo_reauth(request->connection->log, mongo_conn) == NGX_ERROR) {
                ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                              "Mongo connection dropped, could not reconnect");
                if(&mongo_conn->conn.connected) { mongo_disconnect(&mongo_conn->conn); }
                bson_destroy(&query);
                free(value);
                return NGX_HTTP_SERVICE_UNAVAILABLE;
            }
        }
    } while (e);
    
    bson_destroy(&query);
    free(value);

    if(!found){
        return NGX_HTTP_NOT_FOUND;
    }

    /* Get information about the file */
    length = gridfile_get_contentlength(&gfile);
    chunksize = gridfile_get_chunksize(&gfile);
    numchunks = gridfile_get_numchunks(&gfile);
    contenttype = (char*)gridfile_get_contenttype(&gfile);

    // ---------- SEND THE HEADERS ---------- //

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

    // ---------- SEND THE BODY ---------- //

    /* Empty file */
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
    if (cursors == NULL) {
      return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    
    ngx_memzero( cursors, sizeof(mongo_cursor *) * numchunks);

    /* Hook in the cleanup function */
    gridfs_cln = ngx_pool_cleanup_add(request->pool, sizeof(ngx_http_gridfs_cleanup_t));
    if (gridfs_cln == NULL) {
      return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    gridfs_cln->handler = ngx_http_gridfs_cleanup;
    gridfs_clndata = gridfs_cln->data;
    gridfs_clndata->cursors = cursors;
    gridfs_clndata->numchunks = numchunks;

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
        do {
            MONGO_TRY_GENERIC(&mongo_conn->conn){
                e = FALSE;
                cursors[i] = gridfile_get_chunks(&gfile, i, 1);
                mongo_cursor_next(cursors[i]);
            } MONGO_CATCH_GENERIC(&mongo_conn->conn) {
                e = TRUE; ecounter++;
                if (ecounter > MONGO_MAX_RETRIES_PER_REQUEST 
                    || ngx_http_mongo_reconnect(request->connection->log, mongo_conn) == NGX_ERROR
                    || ngx_http_mongo_reauth(request->connection->log, mongo_conn) == NGX_ERROR) {
                    ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                                  "Mongo connection dropped, could not reconnect");
                    if(&mongo_conn->conn.connected) { mongo_disconnect(&mongo_conn->conn); }
                    return NGX_HTTP_SERVICE_UNAVAILABLE;
                }
            }
        } while (e);

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

    return rc;
}

static void ngx_http_gridfs_cleanup(void* data) {
    ngx_http_gridfs_cleanup_t* gridfs_clndata;
    volatile ngx_uint_t i;

    gridfs_clndata = data;

    for (i = 0; i < gridfs_clndata->numchunks; i++) {
        mongo_cursor_destroy(gridfs_clndata->cursors[i]);
    }
}
