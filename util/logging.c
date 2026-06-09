#include <stdarg.h>
#include "logging.h"

#define LOG(level)\
{\
    if(!is_init){\
        fprintf(stderr, "log " #level " error: not initialized\n");\
        return;\
    }\
    if(_level > level) return;\
    \
    va_list args;\
    va_start(args, format);\
    fprintf(_stream, "[%5s] ", #level);\
    vfprintf(_stream, format, args);\
    fprintf(_stream, "\n");\
    va_end(args);\
}

static char is_init = 0;
static FILE* _stream;
static log_level _level;

void log_init(FILE* stream, int level){
    log_set_stream(stream);
    log_set_level(level);
    is_init = 1;
}

void log_set_stream(FILE* stream){
    _stream = stream ? stream : stderr;
    setvbuf(_stream, NULL, _IOLBF, 0);
}

void log_set_level(log_level level){
    _level = level;
}

void debug  (const char* format, ...) LOG(DEBUG)
void info   (const char* format, ...) LOG(INFO)
void warn   (const char* format, ...) LOG(WARN)
void error  (const char* format, ...) LOG(ERROR)
void fatal  (const char* format, ...) LOG(FATAL)