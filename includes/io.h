#pragma once

#include <unistd.h>

#define READBUF_SIZE 4096
#define READBUF_NOLIMIT ((size_t)-1)

typedef struct{
    char buf[READBUF_SIZE];
    char* curr;
    size_t rem_bytes;
    int fd;
} read_buf;

typedef struct{
    char* buf;
    size_t size;
    size_t capacity;
} heap_buf;

void read_buf_init(read_buf* rb, int fd);
void heap_buf_init(heap_buf* hb);
void heap_buf_reset(heap_buf* hb);
void heap_buf_free(heap_buf* hb);

ssize_t read_buf_n(read_buf* rb, void* dst, size_t n);
int read_buf_until_close(read_buf* rb, heap_buf* dst);
int read_buf_httpline(read_buf* rb, heap_buf* dst, size_t maxlen);
int read_buf_until(read_buf* rb, const char* end_seq, size_t end_seq_len, 
    heap_buf* dst, size_t maxlen);
    
ssize_t write_n(int fd, void* src, size_t n);
int write_buf_n(heap_buf* dst, void* data, size_t n);
int write_buf_httpline(heap_buf* dst, const char* line);
int write_buf_httpline_fmt(heap_buf* dst, const char* format, ...);
int write_buf_CRLF(heap_buf* dst);

int write_message(int fd, void* prebody, size_t prebody_size, 
    void* body, size_t body_size);