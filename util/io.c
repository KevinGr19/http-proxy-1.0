#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#include "io.h"
#include "logging.h"
#include "util.h"

#define CRLF "\r\n"
#define CRLF_LEN 2

void read_buf_init(read_buf* rb, int fd){
    *rb = (read_buf){
        .fd = fd,
        .curr = rb->buf,
        .rem_bytes = 0,
    };
}

void heap_buf_init(heap_buf* hb){
    *hb = (heap_buf){
        .buf = NULL,
        .size = 0,
        .capacity = 0,
    };
}

void heap_buf_reset(heap_buf* hb){
    hb->size = 0;
}

void heap_buf_free(heap_buf* hb){
    if(hb->buf){
        free(hb->buf);
        hb->buf = NULL;
    }
    hb->size = 0;
    hb->capacity = 0;
}

static ssize_t read_l(int fd, void* dst, size_t n) {EINTR_LOOP(int, -1, read, fd, dst, n)}

static ssize_t read_buf_consume(read_buf* rb, void* dst, size_t n){
    ssize_t nread = rb->rem_bytes >= n ? n : rb->rem_bytes;
    memcpy(dst, rb->curr, nread);
    rb->curr += nread;
    rb->rem_bytes -= nread;
    return nread;
}

static int heap_buf_prepare(heap_buf* hb, size_t size){
    if(hb->capacity < size){
        size_t new_capacity = size + READBUF_SIZE - (size%READBUF_SIZE);
        char* buf = realloc(hb->buf, new_capacity);
        if(!buf) return -1;

        hb->buf = buf;
        hb->capacity = new_capacity;
    }
    return 0;
}

ssize_t read_buf_n(read_buf* rb, void* dst, size_t n){
    ssize_t nread = read_buf_consume(rb, dst, n);
    if(nread == n) return n;
    rb->curr = rb->buf;

    char* _dst = (char*)dst + nread;
    ssize_t nleft = n-nread;
    while(nleft > 0){
        nread = read_l(rb->fd, rb->buf, READBUF_SIZE);
        if(nread == -1) return -1;
        if(nread == 0) break;

        if(nread > nleft){
            memcpy(_dst, rb->buf, nleft);
            rb->curr += nleft;
            rb->rem_bytes = nread-nleft;
            nleft = 0;
            break;
        } else {
            memcpy(_dst, rb->buf, nread);
            _dst += nread;
            nleft -= nread;
        }
    }
    return (n-nleft);
}

int read_buf_until_close(read_buf* rb, heap_buf* dst){
    if(heap_buf_prepare(dst, READBUF_SIZE) == -1) return -1;

    ssize_t nread = read_buf_consume(rb, dst->buf, READBUF_SIZE);
    rb->curr = rb->buf;
    dst->size = nread;

    while(1){
        nread = read_l(rb->fd, rb->buf, READBUF_SIZE);
        if(nread == -1){
            if(errno == ECONNRESET) break;
            return -1;
        }
        if(nread == 0) break;

        if(heap_buf_prepare(dst, dst->size + nread) == -1) return -1;
        memcpy(&dst->buf[dst->size], rb->buf, nread);
        dst->size += nread;
    }
    return 0;
}

int read_buf_httpline(read_buf* rb, heap_buf* dst, size_t maxlen){
    return read_buf_until(rb, CRLF, CRLF_LEN, dst, maxlen);
}

int read_buf_until(read_buf* rb, const char* end_seq, size_t end_seq_len, heap_buf* dst, size_t maxlen){
    int rc = -1;
    size_t n = 0;
    dst->size = 0;
    
    if(heap_buf_prepare(dst, 1) == -1)
        return -1;

    while(n < maxlen-1){
        if(heap_buf_prepare(dst, n+rb->rem_bytes) == -1)
            return -1;

        // max bytes written = rb->rem_bytes
        size_t ncopy = 0;
        while(rb->rem_bytes >= end_seq_len){
            if(n+ncopy == maxlen-1 || strncmp(&rb->curr[ncopy], end_seq, end_seq_len) == 0){
                memcpy(&dst->buf[n], rb->curr, ncopy);
                rb->curr += ncopy+end_seq_len;
                rb->rem_bytes -= end_seq_len;
                n += ncopy;
                rc = 0;
                goto end;
            }
            ncopy++;
            rb->rem_bytes--;
        }
        
        memcpy(&dst->buf[n], rb->curr, ncopy);
        n += ncopy;

        memmove(rb->buf, &rb->curr[ncopy], rb->rem_bytes);
        rb->curr = rb->buf;

        while(rb->rem_bytes < end_seq_len){
            ssize_t nread = read_l(rb->fd, rb->buf + rb->rem_bytes, READBUF_SIZE-rb->rem_bytes);
            if(nread == -1) return -1;
            if(nread == 0) goto end;
            rb->rem_bytes += nread;
        }
    }

    end:
    dst->buf[n++] = '\0';
    dst->size = n;
    return rc;
}

ssize_t write_n(int fd, void* src, size_t n) {
    ssize_t nleft = n;
    while(nleft > 0){
        ssize_t nwrite = write(fd, src, nleft);
        if(nwrite == -1){
            if(errno != EINTR) return -1;
            errno = 0;
            continue;
        }
        if(nwrite == 0) break;
        nleft -= nwrite;
    }

    return (n-nleft);
}

int write_buf_n(heap_buf* dst, void* data, size_t n){
    size_t new_size = dst->size + n;
    if(heap_buf_prepare(dst, new_size) == -1) return -1;

    memcpy(&dst->buf[dst->size], data, n);
    dst->size = new_size;
    return 0;
}

int write_buf_httpline(heap_buf* dst, const char* line){
    size_t line_len = strlen(line);
    return write_buf_n(dst, (void*)line, line_len) == -1 || write_buf_CRLF(dst) == -1
        ? -1 : 0;
}

int write_buf_httpline_fmt(heap_buf* dst, const char* format, ...){
    va_list args;
    va_start(args, format);
    int size = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if(size < 0) return -1;

    char buf[size+1];
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    return write_buf_n(dst, buf, size) == -1 || write_buf_CRLF(dst) == -1
        ? -1 : 0;
}

int write_buf_CRLF(heap_buf* dst){
    return write_buf_n(dst, CRLF, CRLF_LEN);
}

int write_message(int fd, void* prebody, size_t prebody_size, 
    void* body, size_t body_size)
{
    if(prebody != NULL && prebody_size > 0)
        if(write_n(fd, prebody, prebody_size) != prebody_size) goto send_error;

    if(body != NULL && body_size > 0)
        if(write_n(fd, body, body_size) != body_size) goto send_error;
        
    return 0;

    send_error:
    if(errno == 0) errno = ECONNRESET;
    return -1;
}