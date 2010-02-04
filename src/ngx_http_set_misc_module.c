#define DDEBUG 1
#include "ddebug.h"

#include <ndk.h>

#define NGX_UNESCAPE_URI_COMPONENT  0


typedef enum {
    ngx_http_set_misc_distribution_modula,
    ngx_http_set_misc_distribution_random /* XXX not used */
} ngx_http_set_misc_distribution_t;

typedef struct {
    ngx_str_t            var;
    ngx_uint_t           hash;
} ngx_http_set_if_empty_data_t;


static ndk_upstream_list_t *
ngx_http_set_misc_get_upstream_list(u_char *data, size_t len);

static ngx_uint_t ngx_http_set_misc_apply_distribution(ngx_log_t *log, ngx_uint_t hash,
        ndk_upstream_list_t *ul, ngx_http_set_misc_distribution_t type);

static char * ngx_http_set_if_empty (ngx_conf_t *cf, ngx_command_t *cmd,
        void *conf);

static char * ngx_http_set_hashed_upstream(ngx_conf_t *cf,
        ngx_command_t *cmd, void *conf);

static uintptr_t ngx_http_set_misc_escape_sql_str(u_char *dst, u_char *src,
        size_t size);

static ngx_int_t ngx_http_set_misc_unescape_uri(ngx_http_request_t *r,
        ngx_str_t *res, ngx_http_variable_value_t *v);

static ngx_int_t ngx_http_set_misc_quote_sql_value(ngx_http_request_t *r,
        ngx_str_t *res, ngx_http_variable_value_t *v);

static ngx_int_t ngx_http_set_misc_set_if_empty(ngx_http_request_t *r,
        ngx_str_t *res, ngx_http_variable_value_t *v, void *data);

static ngx_int_t ngx_http_set_misc_set_hashed_upstream(ngx_http_request_t *r,
        ngx_str_t *res, ngx_http_variable_value_t *v, void *data);


static  ndk_set_var_t  ngx_http_set_misc_unescape_uri_filter = {
    NDK_SET_VAR_VALUE,
    ngx_http_set_misc_unescape_uri,
    1,
    NULL
};

static  ndk_set_var_t  ngx_http_set_misc_quote_sql_value_filter = {
    NDK_SET_VAR_VALUE,
    ngx_http_set_misc_quote_sql_value,
    1,
    NULL
};


static ngx_command_t  ngx_http_set_misc_commands[] = {
    {
        ngx_string ("set_unescape_uri"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF
            |NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE2,
        ndk_set_var_value,
        0,
        0,
        &ngx_http_set_misc_unescape_uri_filter
    },
    {
        ngx_string ("set_quote_sql_value"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF
            |NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE2,
        ndk_set_var_value,
        0,
        0,
        &ngx_http_set_misc_quote_sql_value_filter
    },
    {
        ngx_string ("set_if_empty"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF
            |NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE2,
        ngx_http_set_if_empty,
        0,
        0,
        NULL
    },
    {
        ngx_string("set_hashed_upstream"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF
            |NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE3,
        ngx_http_set_hashed_upstream,
        0,
        0,
        NULL
    },

    ngx_null_command
};


static ngx_http_module_t  ngx_http_set_misc_module_ctx = {
    NULL,                                 /* preconfiguration */
    NULL,                                 /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL,                                  /* merge location configuration */
};


ngx_module_t  ngx_http_set_misc_module = {
    NGX_MODULE_V1,
    &ngx_http_set_misc_module_ctx,          // module context
    ngx_http_set_misc_commands,             // module directives
    NGX_HTTP_MODULE,                        // module type
    NULL,                                   // init master
    NULL,                                   // init module
    NULL,                                   // init process
    NULL,                                   // init thread
    NULL,                                   // exit thread
    NULL,                                   // exit process
    NULL,                                   // exit master
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_set_misc_unescape_uri(ngx_http_request_t *r,
        ngx_str_t *res, ngx_http_variable_value_t *v)
{
    size_t                   len;
    u_char                  *p;
    u_char                  *src, *dst;

    /* the unescaped string can only be smaller */
    len = v->len;

    p = ngx_palloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    src = v->data; dst = p;

    ngx_unescape_uri(&dst, &src, v->len, NGX_UNESCAPE_URI_COMPONENT);

    if (src != v->data + v->len) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "set_unescape_uri: input data not consumed completely");
        return NGX_ERROR;
    }

    res->data = p;
    res->len = dst - p;

    return NGX_OK;
}


static ngx_int_t
ngx_http_set_misc_quote_sql_value(ngx_http_request_t *r,
        ngx_str_t *res, ngx_http_variable_value_t *v)
{
    size_t                   len;
    u_char                  *p;
    size_t                   escape;

    if (v->not_found) {
        res->data = (u_char *) "null";
        res->len = sizeof("null") - 1;
        return NGX_OK;
    }

    if (v->len == 0) {
        res->data = (u_char *)"''";
        res->len = sizeof("''") - 1;
        return NGX_OK;
    }

    escape = ngx_http_set_misc_escape_sql_str(NULL, v->data, v->len);

    len = sizeof("''") - 1
        + v->len
        + escape;

    p = ngx_palloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    res->data = p;
    res->len = len;

    *p++ = '\'';

    if (escape == 0) {
        p = ngx_copy(p, v->data, v->len);

    } else {
        p = (u_char *) ngx_http_set_misc_escape_sql_str(p, v->data, v->len);
    }

    *p++ = '\'';

    if (p != res->data + res->len) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "set_quote_sql_value: buffer error");
        return NGX_ERROR;
    }

    return NGX_OK;
}


static uintptr_t
ngx_http_set_misc_escape_sql_str(u_char *dst, u_char *src,
        size_t size)
{
    ngx_uint_t               n;

    if (dst == NULL) {
        /* find the number of chars to be escaped */
        n = 0;
        while (size) {
            /* the highest bit of all the UTF-8 chars
             * is always 1 */
            if ((*src & 0x80) == 0) {
                switch (*src) {
                    case '\r':
                    case '\n':
                    case '\\':
                    case '\'':
                    case '"':
                    case '\032':
                        n++;
                        break;
                    default:
                        break;
                }
            }
            src++;
            size--;
        }

        return (uintptr_t) n;
    }

    while (size) {
        if ((*src & 0x80) == 0) {
            switch (*src) {
                case '\r':
                    *dst++ = '\\';
                    *dst++ = 'r';
                    break;

                case '\n':
                    *dst++ = '\\';
                    *dst++ = 'n';
                    break;

                case '\\':
                    *dst++ = '\\';
                    *dst++ = '\\';
                    break;

                case '\'':
                    *dst++ = '\\';
                    *dst++ = '\'';
                    break;

                case '"':
                    *dst++ = '\\';
                    *dst++ = '"';
                    break;

                case '\032':
                    *dst++ = '\\';
                    *dst++ = *src;
                    break;

                default:
                    *dst++ = *src;
                    break;
            }
        } else {
            *dst++ = *src;
        }
        src++;
        size--;
    } /* while (size) */

    return (uintptr_t) dst;
}


static ngx_int_t
ngx_http_set_misc_set_if_empty(ngx_http_request_t *r,
        ngx_str_t *res, ngx_http_variable_value_t *v, void *data)
{
    ngx_http_set_if_empty_data_t      *var_data = data;

    ngx_http_variable_value_t   *orig_v;

    orig_v = ngx_http_get_variable(r, &var_data->var, var_data->hash, 0);
    if (orig_v == NULL) {
        return NGX_ERROR;
    }

    if (orig_v->not_found || orig_v->len == 0) {
        dd("variable %.*s not found", var_data->var.len,
                var_data->var.data);
        res->data = v->data;
        res->len = v->len;

    } else {
        res->data = orig_v->data;
        res->len = orig_v->len;
    }

    return NGX_OK;
}


static char *
ngx_http_set_if_empty(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t               *value;
    ndk_set_var_t            filter;
    ngx_uint_t               hash;
    size_t                   len;
    u_char                  *lowcase;
    ngx_str_t                var;

    ngx_http_set_if_empty_data_t        *data;

    value = cf->args->elts;

    data = ngx_palloc(cf->pool, sizeof(ngx_http_set_if_empty_data_t));
    if (data == NULL) {
        return NGX_CONF_ERROR;
    }

    len = value[1].len - 1;

    lowcase = ngx_pnalloc(cf->pool, len);
    if (lowcase == NULL) {
        return NGX_CONF_ERROR;
    }

    hash = ngx_hash_strlow(lowcase, value[1].data + 1, len);

    var.len = len;
    var.data = lowcase;

    data->var = var;
    data->hash = hash;

    filter.type = NDK_SET_VAR_VALUE_DATA;
    filter.func = ngx_http_set_misc_set_if_empty;
    filter.size = 1;
    filter.data = data;

    return  ndk_set_var_value_core(cf, &value[1], &value[2], &filter);
}


static char *
ngx_http_set_hashed_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t               *value;
    ndk_set_var_t            filter;
    ngx_uint_t               n;
    ngx_str_t               *var;
    ngx_str_t               *ulname;
    ndk_upstream_list_t     *ul;
    ngx_str_t               *v;

    value = cf->args->elts;

    var = &value[1];
    ulname = &value[2];

    n = ngx_http_script_variables_count(ulname);

    if (n) {
        /* upstream list name contains variables */
        v = ulname;
        filter.size = 2;
        filter.data = NULL;

    } else {
        ul = ngx_http_set_misc_get_upstream_list(ulname->data, ulname->len);
        if (ul == NULL) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                    "set_hashed_upstream: upstream list \"%V\" "
                    "not defined yet", ulname);
            return NGX_CONF_ERROR;
        }

        v = &value[3];
        filter.size = 1;
        filter.data = ul;
    }

    filter.type = NDK_SET_VAR_VALUE_DATA;
    filter.func = ngx_http_set_misc_set_hashed_upstream;

    return  ndk_set_var_value_core(cf, var, v, &filter);
}


static ngx_int_t
ngx_http_set_misc_set_hashed_upstream(ngx_http_request_t *r,
        ngx_str_t *res, ngx_http_variable_value_t *v, void *data)
{
    ngx_str_t                  **u;
    ndk_upstream_list_t         *ul = data;
    ngx_str_t                    ulname;
    ngx_uint_t                   hash, index;
    ngx_http_variable_value_t   *key;

    if (ul == NULL) {
        ulname.data = v->data;
        ulname.len = v->len;

        ul = ngx_http_set_misc_get_upstream_list(ulname.data, ulname.len);

        key = v + 1;
    } else {
        key = v;
    }

    if (ul->nelts == 0) {
        res->data = NULL;
        res->len = 0;
        return NGX_OK;
    }

    u = ul->elts;

    if (ul->nelts == 1) {
        res->data = u[0]->data;
        res->len = u[0]->len;
        return NGX_OK;
    }

    hash = ngx_hash_key_lc(key->data, key->len);

    index = ngx_http_set_misc_apply_distribution(r->connection->log, hash, ul,
            ngx_http_set_misc_distribution_modula);

    res->data = u[index]->data;
    res->len = u[index]->len;

    return NGX_OK;
}


static ngx_uint_t
ngx_http_set_misc_apply_distribution(ngx_log_t *log, ngx_uint_t hash,
        ndk_upstream_list_t *ul, ngx_http_set_misc_distribution_t type)
{
    switch (type) {
    case ngx_http_set_misc_distribution_modula:
        return hash % ul->nelts;

    default:
        ngx_log_error(NGX_LOG_ERR, log, 0, "apply_distribution: "
                "unknown distribution: %d", type);

        return 0;
    }

    /* impossible to reach here */
    return 0;
}


static ndk_upstream_list_t *
ngx_http_set_misc_get_upstream_list(u_char *data, size_t len)
{
    ndk_upstream_list_t         *ul, *ule;

    if (ndk_upstreams == NULL) {
        return NULL;
    }

    ul = ndk_upstreams->elts;
    ule = ul + ndk_upstreams->nelts;

    for (; ul < ule; ul++) {
        if (ul->name.len == len &&
                ngx_strncasecmp(ul->name.data, data, len) == 0)
        {
            return ul;
        }
    }

    return NULL;
}
