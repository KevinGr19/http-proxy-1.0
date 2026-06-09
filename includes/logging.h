#pragma once

#include <stdio.h>
#include <string.h>
#include <errno.h>

typedef enum{
    DEBUG = 0,
    INFO,
    WARN,
    ERROR,
    FATAL,
} log_level;

void log_init(FILE* stream, int level);
void log_set_stream(FILE* stream);
void log_set_level(log_level level);

void debug  (const char* format, ...);
void info   (const char* format, ...);
void warn   (const char* format, ...);
void error  (const char* format, ...);
void fatal  (const char* format, ...);

#define __log_trace_hdr "[%s|%s:%d] "
#define __log_trace_val __func__, __FILE_NAME__, __LINE__

#define debug_tr(format, ...)  debug  (__log_trace_hdr format, __log_trace_val, ##__VA_ARGS__)
#define info_tr(format, ...)   info   (__log_trace_hdr format, __log_trace_val, ##__VA_ARGS__)
#define warn_tr(format, ...)   warn   (__log_trace_hdr format, __log_trace_val, ##__VA_ARGS__)
#define error_tr(format, ...)  error  (__log_trace_hdr format, __log_trace_val, ##__VA_ARGS__)
#define fatal_tr(format, ...)  fatal  (__log_trace_hdr format, __log_trace_val, ##__VA_ARGS__)

#define log_fail_fmt(log_fn, func, fmt, ...) (log_fn)(__log_trace_hdr #func " failed: " fmt, __log_trace_val, ##__VA_ARGS__)
#define log_fail_errno(log_fn, func)            log_fail_fmt(log_fn, func, "%s (errno=%d)", strerror(errno), errno)
#define log_fail_eai(log_fn, func, error_code)  log_fail_fmt(log_fn, func, "%s (gai_error=%d)", gai_strerror(error_code), error_code)
