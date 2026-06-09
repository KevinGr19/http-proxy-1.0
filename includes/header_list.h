#pragma once

#include "io.h"
#include "http.h"

typedef struct header_list header_list;

header_list* header_list_create(void);
int header_list_addfmt(header_list* list, http_header header, const char* format, ...);
int header_list_addraw(header_list* list, char* header_raw, char* value);
int header_list_update(header_list* list, header_list* update);
int header_list_write_buf(heap_buf* dst, header_list* list);
void header_list_free(header_list* list);