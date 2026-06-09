#define _PROXY_HTTP_KEEP_XMACRO

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#define __USE_XOPEN
#define __USE_POSIX
#define __USE_MISC 
#include <time.h>

#include "http.h"
#include "logging.h"
#include "util.h"

static size_t split(char* str, char** word_buf, size_t max_words);
static int parse_version(char* word, int* major, int* minor);

http_method http_parse_method(const char* str){
    #define METHOD(method) if(strcasecmp(str, #method) == 0) return HTTP_##method;
    LIST_OF_METHODS
    #undef METHOD
    return HTTP_UNDEF;
}

const char* http_str_method(http_method method){
    switch(method){
        #define METHOD(method) case HTTP_##method: return #method;
        LIST_OF_METHODS
        #undef METHOD
        default: return "UNDEF";
    }
}

static http_header parse_header(char* str){
    #define HEADER(header, name, _) if(strcasecmp(str, name) == 0) return HDR_##header;
    LIST_OF_HEADERS
    #undef HEADER
    return HDR_UNDEF;
}

const char* http_str_header(http_header header){
    switch(header){
        #define HEADER(header, name, _) case HDR_##header: return name;
        LIST_OF_HEADERS
        #undef HEADER
        default: return "UNDEF";
    }
}

const char* http_returncode_reason(int code){
    switch(code){
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Moved Temporarily";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 505: return "HTTP version not supported";
        default: return "No reason";
    }
}

http_request_line* http_parse_request_line(char* str){
    char* delim;
    char* words[3];
    size_t nwords = split(str, words, 3);
    if(nwords < 2) return NULL;

    http_method method = http_parse_method(words[0]);
    http_version version;
    if(nwords < 3){
        // HTTP/0.9 - Simple-Request
        version.major = 0;
        version.minor = 9;
    }
    else if(parse_version(words[2], &version.major, &version.minor) == -1) return NULL;

    char* url = words[1];
    char* uri;
    size_t hostname_len, uri_len;

    delim = strstr(url, "://");
    if(delim) url = delim+3;

    delim = strchr(url, '/');
    if(delim){
        hostname_len = delim == url ? 0 : 1 + (size_t)delim - (size_t)url;
        uri_len = strlen(delim)+1;
        uri = delim;
    } else {
        hostname_len = strlen(url)+1;
        uri_len = 2;
        uri = "/";
    }

    http_request_line* out = malloc(sizeof(http_request_line) + hostname_len + uri_len);
    if(!out){
        errno = ENOMEM;
        return NULL;
    }

    out->hostname = NULL;
    out->port = HTTP_DEFAULT_PORT;

    char* buf = (char*)out + sizeof(http_request_line);
    if(hostname_len > 0){
        memcpy(buf, url, hostname_len-1);
        buf[hostname_len-1] = '\0';
        out->hostname = buf;
        
        delim = strrchr(out->hostname, ':');
        if(delim && *(delim+1) != '\0'){
            *delim++ = '\0';
            out->port = delim;
        }
        buf += hostname_len;
    }
    memcpy(buf, uri, uri_len-1);
    buf[uri_len-1] = '\0';
    out->uri = buf;

    out->method = method;
    out->version = version;
    return out;
}

http_status_line* http_parse_status_line(char* str){
    char *delim, *endptr;

    http_version version;
    delim = strchr(str, ' ');
    if(!delim) return NULL;
    *delim++ = '\0';
    if(parse_version(str, &version.major, &version.minor) == -1) return NULL;

    str = delim;
    delim = strchr(str, ' ');
    if(!delim) return NULL;
    *delim++ = '\0';
    int code = strtod(str, &endptr);
    if(*endptr != '\0') return NULL;

    char* reason = delim;
    size_t reason_len = strlen(reason)+1;

    http_status_line* out = malloc(sizeof(http_status_line) + reason_len);
    if(!out){
        errno = ENOMEM;
        return NULL;
    }

    *out = (http_status_line){
        .version = version,
        .code = code,
    };

    char* buf = (char*)out + sizeof(http_status_line);
    memcpy(buf, reason, reason_len);
    out->reason_phrase = buf;
    
    return out;
}

int http_parse_header(char* str, char** header_str, char** value, http_header* header){
    char* curr = str;
    char *start, *end;

    while(1){
        if(*curr == '\0') return -1;
        if(*curr == ' ') curr++;
        else break;
    }
    start = curr;
    end = ++curr;

    while(1){
        if(*curr == '\0') return -1;
        if(*curr == ':') break;
        if(*curr != ' ') end = curr+1;
        curr++;
    }
    *end = '\0';
    *header_str = start;
    *header = parse_header(start);

    curr++;
    while(1){
        if(*curr == '\0'){
            *value = curr;
            return 0;
        }
        if(*curr == ' ') curr++;
        else break;
    }
    start = curr;
    end = ++curr;

    while(1){
        if(*curr == '\0') break;
        if(*curr != ' ') end = curr+1;
        curr++;
    }

    *end = '\0';
    *value = start;
    return 0;
}

int http_search_header_param(const char* header_value, const char* param){
    size_t param_len = strlen(param);
    const char* start = header_value;
    const char* curr = header_value;
    const char* end;

    while(1){
        // Find param start
        while(*curr == ' ') curr++;
        if(*curr == '\0') return -1;
        if(*curr == ','){
            curr++;
            continue;
        }

        // Find param boundaries
        start = curr;
        while(*curr != '=' && *curr != ' ' && *curr != ',' && *curr != '\0') curr++;
        end = curr;
        
        size_t slen = (size_t)end - (size_t)start;
        if(param_len == slen && strncasecmp(param, start, slen) == 0) return 0;
        if(*curr == '\0') return -1;

        // Skip to next param
        bool in_quotes = false;
        while(1){
            while((in_quotes || *curr != ',') && *curr != '"' && *curr != '\0') curr++;
            if(*curr == '\0') return -1;
            if(*curr == '"'){
                in_quotes = !in_quotes;
                curr++;
                continue;
            }
            break;
        }
        start = ++curr;
    }
    return -1;
}

static const char* DATE_FMT_RFC1123 = "%a, %d %b %Y %T GMT";
static const char* DATE_FMT_RFC850 = "%A, %d-%b-%y %T GMT";
static const char* DATE_FMT_ASCTIME = "%a %b %e %T %Y";

int http_parse_date(const char* str, time_t* out){
    struct tm tm;
    if(!(
        strptime(str, DATE_FMT_RFC1123, &tm) ||
        strptime(str, DATE_FMT_RFC850, &tm) ||
        strptime(str, DATE_FMT_ASCTIME, &tm)
    )) return -1;

    *out = timegm(&tm);
    return *out == (time_t)-1 ? -1 : 0;
}

int http_str_date(time_t dt, char* buf, size_t maxlen){
    struct tm tm;
    if(gmtime_r(&dt, &tm) == NULL) return -1;

    size_t len = strftime(buf, maxlen, DATE_FMT_RFC1123, &tm);
    return len > 0 ? 0 : -1;
}

int http_reformat_date(const char* str, time_t* dt, char* buf, size_t maxlen){
    time_t _dt;
    if(!dt) dt = &_dt;

    if(http_parse_date(str, dt) == -1) return -1;
    if(http_str_date(*dt, buf, maxlen) == -1){
        log_fail_errno(warn, http_str_date);
        return -1;
    }
    return 0;
}

static size_t split(char* str, char** word_buf, size_t max_words){
    size_t words = 0;
    char* curr = str;
    char* end = str+strlen(str);
    bool in_quotes = false;

    while(words < max_words){
        while(curr < end && (*curr != ' ' || in_quotes) && *curr != '"') curr++;

        if(*curr == '"') in_quotes = !in_quotes;
        if(curr != str){
            *curr = '\0';
            word_buf[words++] = str;
        }
        str = ++curr;

        if(curr >= end) return words;
    }
    
    return words;
}

static int parse_version(char* word, int* major, int* minor){
    if(strncasecmp(word, "HTTP/", 5) != 0) return -1;
    char* version = word + 5;

    char* delim = strchr(version, '.'); 
    if(!delim || *(delim+1) == '\0') return -1;
    *delim++ = '\0';

    char* endptr;
    *major = strtod(version, &endptr);
    if(*endptr != '\0') return -1;

    *minor = strtod(delim, &endptr);
    if(*endptr != '\0') return -1;

    return 0;
}