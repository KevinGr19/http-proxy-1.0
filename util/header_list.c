#include "header_list.h"
#include "logging.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>

typedef struct{
    char* str;
    size_t h_len;
} header_node;

struct header_list{
    header_node* headers;
    size_t count;
    size_t capacity;
};

static void header_list_resize(header_list* list, size_t new_capacity);
static int _addfmt(header_list* list, const char* header_raw, const char* format, ...);
static int _addvfmt(header_list* list, const char* header_raw, const char* format, va_list args);

header_list* header_list_create(void){
    header_list* list = malloc(sizeof(header_list));
    if(!list){
        log_fail_errno(warn, malloc);
        return NULL;
    }

    *list = (header_list){
        .headers = NULL,
        .capacity = 0,
    };
    header_list_resize(list, 8);
    return list;
}

int header_list_addfmt(header_list* list, http_header header, const char* format, ...){
    va_list args;
    va_start(args, format);
    int rc = _addvfmt(list, http_str_header(header), format, args);
    va_end(args);
    return rc;
}

int header_list_addraw(header_list* list, char* header_raw, char* value){
    return _addfmt(list, header_raw, "%s", value);
}

int header_list_update(header_list* list, header_list* update){
    for(size_t j = 0; j < update->count; j++){
        header_node* update_h = &update->headers[j];
        if(update_h->h_len == 0){
            warn_tr("Invalid header");
            continue;
        }

        for(size_t i = 0; i < list->count; i++){
            header_node* curr_h = &list->headers[i];
            if(curr_h->h_len != update_h->h_len) continue;
            if(strncasecmp(curr_h->str, update_h->str, curr_h->h_len) != 0) continue;

            free(curr_h->str);
            curr_h->str = update_h->str;
            update_h->str = NULL;
            break;
        }
    }
    header_list_free(update);
    return 0;
}

int header_list_write_buf(heap_buf* dst, header_list* list){
    for(size_t i = 0; i < list->count; i++){
        if(write_buf_httpline(dst, list->headers[i].str) == -1) return -1;
    }
    if(write_buf_CRLF(dst) == -1) return -1;
    return 0;
}

void header_list_free(header_list* list){
    if(list->headers){
        for(size_t i = 0; i < list->count; i++){
            if(list->headers[i].str) free(list->headers[i].str);
        }
        free(list->headers);
    }
    free(list);
}

static void header_list_resize(header_list* list, size_t new_capacity){
    header_node* headers = realloc(list->headers, sizeof(header_node) * new_capacity);
    if(!headers){
        log_fail_errno(fatal, realloc);
        exit(1);
    }

    list->headers = headers;
    list->capacity = new_capacity;
}

static int _addfmt(header_list* list, const char* header_raw, const char* format, ...){
    va_list args;
    va_start(args, format);
    int rc = _addvfmt(list, header_raw, format, args);
    va_end(args);
    return rc;
}

static int _addvfmt(header_list* list, const char* header_raw, const char* format, va_list args){
    int rc;
    
    va_list _args;
    va_copy(_args, args);
    rc = vsnprintf(NULL, 0, format, _args);
    va_end(_args);
    if(rc < 0) return -1;

    size_t header_len = strlen(header_raw);
    size_t value_len = (size_t)rc;
    size_t size = header_len+2+value_len+1;

    char* buf = malloc(size);
    if(!buf) return -1;

    memcpy(buf, header_raw, header_len);
    memcpy(&buf[header_len], ": ", 2);
    char* value_buf = &buf[header_len+2];

    va_copy(_args, args);
    rc = vsnprintf(value_buf, value_len+1, format, _args);
    va_end(_args);
    
    if(list->count >= list->capacity) header_list_resize(list, list->count + 8);
    list->headers[list->count++] = (header_node){.str = buf, .h_len = header_len};
    return 0;
}