#pragma once

#include "http.h"
#include "header_list.h"

#include <time.h>

#define ECACHE_ERRNO -1
#define ECACHE_REFUSED -2
#define ECACHE_NOENTRY -3

typedef struct{
    http_status_line* status_line;
    header_list* headers;
    void* body;
    size_t content_length;
    headers_t received_headers;
    time_t expires;
} cache_response;

typedef struct{
    const char* hostname;
    const char* port;
    const char* uri;
} cache_key;

void cache_init(void);
int cache_try_send_response(int fd, cache_key* key, http_method method);
int cache_store_response(cache_key* key, cache_response* response);
int cache_update_response_headers(cache_key* key, cache_response* response);