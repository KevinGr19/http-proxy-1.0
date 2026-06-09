#pragma once

#include <time.h>

#define HTTP_VERSION(major, minor) ("HTTP/" #major "." #minor)
#define HTTP_DEFAULT_PORT "80"

#define LIST_OF_METHODS\
    METHOD(HEAD)\
    METHOD(GET)\
    METHOD(POST)\
    METHOD(PUT)\
    METHOD(DELETE)\
    METHOD(LINK)\
    METHOD(UNLINK)

#define LIST_OF_HEADERS\
    HEADER(DATE,                "Date",                 1 << 1)\
    HEADER(PRAGMA,              "Pragma",               1 << 2)\
    HEADER(AUTHORIZATION,       "Authorization",        1 << 3)\
    HEADER(FROM,                "From",                 1 << 4)\
    HEADER(IF_MODIFIED_SINCE,   "If-Modified-Since",    1 << 5)\
    HEADER(REFERER,             "Referer",              1 << 6)\
    HEADER(USER_AGENT,          "User-Agent",           1 << 7)\
    HEADER(LOCATION,            "Location",             1 << 8)\
    HEADER(SERVER,              "Server",               1 << 9)\
    HEADER(WWW_AUTHENTICATE,    "WWW-Authenticate",     1 << 10)\
    HEADER(ALLOW,               "Allow",                1 << 11)\
    HEADER(CONTENT_ENCODING,    "Content-Encoding",     1 << 12)\
    HEADER(CONTENT_LENGTH,      "Content-Length",       1 << 13)\
    HEADER(CONTENT_TYPE,        "Content-Type",         1 << 14)\
    HEADER(EXPIRES,             "Expires",              1 << 15)\
    HEADER(LAST_MODIFIED,       "Last-Modified",        1 << 16)\
    HEADER(HOST,                "Host",                 1 << 17)\
    HEADER(CONNECTION,          "Connection",           1 << 18)\
    HEADER(PROXY_CONNECTION,    "Proxy-Connection",     1 << 19)

typedef enum{
    HTTP_UNDEF = 0,
    #define METHOD(method) HTTP_##method,
    LIST_OF_METHODS
    #undef METHOD
} http_method;

typedef enum{
    HDR_UNDEF = 0,
    #define HEADER(header, _, enum_val) HDR_##header = enum_val,
    LIST_OF_HEADERS
    #undef HEADER
} http_header;

typedef int headers_t;

typedef struct{
    int major;
    int minor;
} http_version;

typedef struct{
    http_version version;
    http_method method;
    char* hostname;
    char* port;
    char* uri;
} http_request_line;

typedef struct{
    http_version version;
    int code;
    char* reason_phrase;
} http_status_line;

http_method http_parse_method(const char* str);
const char* http_str_method(http_method method);

http_request_line* http_parse_request_line(char* str);
http_status_line* http_parse_status_line(char* str);
const char* http_returncode_reason(int code);

int http_parse_header(char* str, char** header_str, char** value, http_header* header);
const char* http_str_header(http_header header);
int http_search_header_param(const char* header_value, const char* param);

int http_parse_date(const char* str, time_t* out);
int http_str_date(time_t dt, char* buf, size_t maxlen);

#ifndef _PROXY_HTTP_KEEP_XMACRO
#undef LIST_OF_METHODS
#undef LIST_OF_HEADERS
#endif