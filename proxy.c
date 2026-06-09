#include "logging.h"
#include "socket.h"
#include "io.h"
#include "http.h"
#include "header_list.h"
#include "cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <locale.h>

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL INFO
#endif

#define PROXY_HTTP_VERSION HTTP_VERSION(1,0)

static const char *user_agent_hdr = "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";

typedef char date_buf[64];

typedef struct{
    http_request_line* req_line;
    headers_t received_headers;
    header_list* headers;
    size_t content_length;
    void* body;
    bool is_no_cache;
} http_request;

typedef struct{
    http_status_line* status_line;
    headers_t received_headers;
    header_list* headers;
    size_t content_length;
    time_t expires;
    void* body;
    bool is_no_cache;
} http_response;

void    assign_client(int fd);
void    th_cleanup(void* arg);
void*   th_worker(void* arg);

void    th_handle_client_request(void);
void    th_handle_server_response(void);
void    th_handle_client_header(http_header header, char* value);
void    th_handle_server_header(http_header header, char* value);
void    th_send_request_to_server(void);
void    th_send_response_to_client(void);

void    th_try_get_cached_response(void);
void    th_try_cache_response(void);

void    th_readline(void);
void    th_httperror_client(int code, const char* error_message);

#define th_writeline(fmt, ...) do{\
    if(write_buf_httpline_fmt(&send_buf, fmt, ##__VA_ARGS__) == -1){\
        log_fail_errno(error, write_buf_httpline_fmt);\
        th_exit();\
    }\
}while(0)

#define th_writeheader(header_list, header, fmt, value) do{\
    if(header_list_addfmt(header_list, header, fmt, value) == -1){\
        log_fail_errno(error, header_list_add);\
        th_exit();\
    }\
}while(0)

#define th_writeheader_raw(header_list, header_raw, value) do{\
    if(header_list_addraw(header_list, header_raw, value) == -1){\
        log_fail_errno(error, header_list_addraw);\
        th_exit();\
    }\
}while(0)

#define th_exit() pthread_exit(NULL)

void init(void){
    signal(SIGPIPE, SIG_IGN);
    setlocale(LC_TIME, "en-US");
    cache_init();
}

int main(int argc, char** argv){
    log_init(stderr, DEBUG_LEVEL);

    if(argc < 2){
        fatal_tr("Missing port argument.");
        exit(-1);
    }

    char* port = argv[1];
    int listenfd = open_listenfd(port);
    if(listenfd < 0){
        if(listenfd == -1) log_fail_errno(fatal, open_listenfd);
        else log_fail_eai(fatal, open_listenfd, listenfd+1);
        exit(-1);
    }

    init();
    info("Listening on port %s.", port);

    char client_hostname[250], client_port[6];
    struct sockaddr_storage addr;
    socklen_t addr_len;

    while(1){
        addr_len = sizeof(addr);
        int connfd = accept_l(listenfd, (sockaddr*)&addr, &addr_len);
        if(connfd == -1){
            log_fail_errno(warn, accept_l);
            continue;
        }

        int rc = getnameinfo((sockaddr*)&addr, addr_len, 
            client_hostname, sizeof(client_hostname), 
            client_port, sizeof(client_port), NI_NUMERICSERV);
        if(rc != 0){
            if(rc == EAI_SYSTEM) log_fail_errno(warn, getnameinfo);
            else log_fail_eai(warn, getnameinfo, rc);
            strcpy(client_hostname, "???");
            strcpy(client_port, "???");
        }

        info("New request (%s:%s) -> fd=%d", client_hostname, client_port, connfd);
        assign_client(connfd);
    }

    return 0;
}

void assign_client(int fd){
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if(pthread_create(&tid, &attr, th_worker, (void*)fd) == -1){
        close(fd);
        info("Could not create thread to handle client fd=%d", fd);
    }
}

__thread int clientfd = -1;
__thread int serverfd = -1;
__thread read_buf rb;
__thread heap_buf recv_buf;
__thread heap_buf send_buf;
__thread http_request request;
__thread http_response response;

void th_cleanup(void* _){
    if(clientfd != -1) close(clientfd);
    if(serverfd != -1) close(serverfd);

    if(request.req_line) free(request.req_line);
    if(request.headers) header_list_free(request.headers);
    if(request.body) free(request.body);

    if(response.status_line) free(response.status_line);
    if(response.headers) header_list_free(response.headers);
    if(response.body) free(response.body);

    heap_buf_free(&recv_buf);
    heap_buf_free(&send_buf);
}

void* th_worker(void* arg){
    pthread_cleanup_push(th_cleanup, NULL);

    clientfd = (int)arg;
    th_handle_client_request();
    th_try_get_cached_response();

    th_send_request_to_server();
    th_handle_server_response();
    th_send_response_to_client();
    info("Successfully sent response to client fd=%d", clientfd);

    th_try_cache_response();

    pthread_cleanup_pop(1);
    return NULL;
}

void th_handle_client_request(void){
    read_buf_init(&rb, clientfd);
    heap_buf_init(&recv_buf);
    heap_buf_init(&send_buf);
    request.headers = header_list_create();
    
    th_readline();
    http_request_line* req_line = request.req_line = http_parse_request_line(recv_buf.buf);
    if(!req_line || !req_line->hostname)
        th_httperror_client(400, "Bad request line");
    if(req_line->version.major != 1)
        th_httperror_client(505, NULL);
    if(req_line->method == HTTP_UNDEF)
        th_httperror_client(501, NULL);
    
    debug_tr("%s %s %s", http_str_method(req_line->method), req_line->uri, PROXY_HTTP_VERSION);
    th_writeline("%s %s %s", http_str_method(req_line->method), req_line->uri, PROXY_HTTP_VERSION);
    
    http_header header;
    char *header_raw, *value;
    while(1){
        th_readline();
        if(strcmp(recv_buf.buf, "") == 0) break;
      
        if(http_parse_header(recv_buf.buf, &header_raw, &value, &header) == -1)
            th_httperror_client(400, "Invalid header");
        
        debug_tr("-> [%s: %s]", header_raw, value);
        if(header == HDR_UNDEF) th_writeheader_raw(request.headers, header_raw, value);
        else th_handle_client_header(header, value);
    }
    
    bool body_required =
        req_line->method == HTTP_POST ||
        req_line->method == HTTP_PUT;
    
    if(body_required){
        if(!(request.received_headers & HDR_CONTENT_LENGTH))
            th_httperror_client(400, "Missing Content-Length header");
    }
    
    if(request.content_length > 0){
        request.body = malloc(request.content_length);
        if(!request.body){
            error_tr("Could not allocate %zu bytes for client body", request.content_length);
            th_exit();
        }
        
        ssize_t nread = read_buf_n(&rb, request.body, request.content_length);
        
        if(nread == -1){
            log_fail_errno(error, read_buf_n);
            th_exit();
        }
        if(nread < request.content_length){
            debug_tr("Socket fd=%d closed by client prematurely", clientfd);
            th_exit();
        }
        
        th_writeheader(request.headers, HDR_CONTENT_LENGTH, "%zu", request.content_length);
    }

    th_writeheader(request.headers, HDR_USER_AGENT, "%s", user_agent_hdr);
    th_writeheader(request.headers, HDR_HOST, "%s", req_line->hostname);
    th_writeheader(request.headers, HDR_CONNECTION, "%s", "close");
    th_writeheader(request.headers, HDR_PROXY_CONNECTION, "%s", "close");
}

void th_handle_server_response(void){
    read_buf_init(&rb, serverfd);
    heap_buf_reset(&recv_buf);
    heap_buf_reset(&send_buf);
    response.headers = header_list_create();

    th_readline();
    http_status_line* status_line = response.status_line = http_parse_status_line(recv_buf.buf);
    if(!status_line || status_line->version.major != 1)
        th_httperror_client(502, "Invalid status line");
    
    status_line->version.minor = 0;
    debug_tr("%s %d %s", PROXY_HTTP_VERSION, status_line->code, status_line->reason_phrase);
    th_writeline("%s %d %s", PROXY_HTTP_VERSION, status_line->code, status_line->reason_phrase);

    char *header_raw, *value;
    http_header header;
    while(1){
        th_readline();
        if(strcmp(recv_buf.buf, "") == 0) break;

        if(http_parse_header(recv_buf.buf, &header_raw, &value, &header) == -1)
            th_httperror_client(502, "Invalid header");

        debug_tr("<- [%s: %s]", header_raw, value);
        if(header == HDR_UNDEF) th_writeheader_raw(response.headers, header_raw, value);
        else th_handle_server_header(header, value);
    }
    
    bool no_body =
        request.req_line->method == HTTP_HEAD ||
        status_line->code/100 == 1 ||
        status_line->code == 204 ||
        status_line->code == 304;
    
    if(!no_body){
        if(response.content_length > 0){
            response.body = malloc(response.content_length);
            if(!response.body){
                error_tr("Could not allocate %zu bytes for response body", response.content_length);
                th_httperror_client(500, "Proxy error");
            }
            
            ssize_t nread = read_buf_n(&rb, response.body, response.content_length);
            if(nread == -1){
                log_fail_errno(error, read_buf_n);
                th_httperror_client(502, NULL);
            }
            if(nread < response.content_length){
                warn_tr("Read %zi bytes instead of %zu", nread, response.content_length);
                response.content_length = nread;
            }
        }
        else{
            if(read_buf_until_close(&rb, &recv_buf) == -1){
                log_fail_errno(error, read_buf_until_close);
                th_httperror_client(502, NULL);
            }
            
            response.content_length = recv_buf.size;
            response.body = realloc(recv_buf.buf, recv_buf.size); // Shrink block
            heap_buf_init(&recv_buf);
        }
        th_writeheader(response.headers, HDR_CONTENT_LENGTH, "%zu", response.content_length);
        
        if(!(response.received_headers & HDR_DATE)){
            char date[64];
            time_t dt = time(NULL);
            if(http_str_date(dt, date, sizeof(date)) == -1) log_fail_errno(warn, http_str_date);
            else th_writeheader(response.headers, HDR_DATE, "%s", date);
        }
    }
}

void th_handle_client_header(http_header header, char* value){
    switch(header){
        case HDR_DATE:{
            date_buf date;
            if(http_reformat_date(value, NULL, date, sizeof(date)) == -1) return;
            th_writeheader(request.headers, header, "%s", date);
            break;
        }

        case HDR_CONTENT_LENGTH:{
            char* endptr;
            size_t content_length = strtoull(value, &endptr, 10);
            if(*endptr != '\0') return;

            request.content_length = content_length;
            break;
        }

        case HDR_PRAGMA:
            th_writeheader(request.headers, header, "%s", value);
            request.is_no_cache = (http_search_header_param(value, "no-cache") == 0);
            break;

        case HDR_USER_AGENT:
        case HDR_HOST:
        case HDR_CONNECTION:
        case HDR_PROXY_CONNECTION:
            return;

        default:
            th_writeheader(request.headers, header, "%s", value);
            break;
    }

    request.received_headers |= header;
}

void th_handle_server_header(http_header header, char* value){
    switch(header){
        case HDR_DATE:{
            date_buf date;
            if(http_reformat_date(value, NULL, date, sizeof(date)) == -1) return;
            th_writeheader(response.headers, header, "%s", date);
            break;
        }

        case HDR_EXPIRES:{
            date_buf date;
            if(http_reformat_date(value, &response.expires, date, sizeof(date)) == -1) return;
            th_writeheader(response.headers, header, "%s", date);
            break;
        }

        case HDR_CONTENT_LENGTH:{
            char* endptr;
            size_t content_length = strtoull(value, &endptr, 10);
            if(*endptr != '\0') return;

            response.content_length = content_length;
            break;
        }

        case HDR_PRAGMA:
            th_writeheader(response.headers, header, "%s", value);
            response.is_no_cache = (http_search_header_param(value, "no-cache") == 0);
            break;

        default:
            th_writeheader(response.headers, header, "%s", value);
            break;
    }

    response.received_headers |= header;
}

void th_send_response_to_client(void){
    if(header_list_write_buf(&send_buf, response.headers) == -1){
        log_fail_errno(error, header_list_write_buf);
        th_exit();
    }

    int rc = write_message(clientfd,
        send_buf.buf, send_buf.size,
        response.body, response.content_length);

    if(rc == -1){
        log_fail_errno(error, write_n);
        error_tr("Could not send all response to client fd=%d", clientfd);
        th_exit();
    }
}

void th_send_request_to_server(void){
    serverfd = open_clientfd(request.req_line->hostname, request.req_line->port);
    if(serverfd < 0){
        if(serverfd == -1) log_fail_errno(error, open_clientfd);
        else log_fail_eai(error, open_clientfd, serverfd+1);
        th_httperror_client(502, NULL);
    }

    if(header_list_write_buf(&send_buf, request.headers) == -1){
        log_fail_errno(error, header_list_write_buf);
        th_httperror_client(500, "Proxy error");
    }

    int rc = write_message(serverfd, 
        send_buf.buf, send_buf.size, 
        request.body, request.content_length);

    if(rc == -1){
        log_fail_errno(error, write_n);
        error_tr("Could not send all request (fd=%d) to server", clientfd);
        th_httperror_client(502, "Sending error");
    }
}

void th_try_get_cached_response(void){
    if(request.is_no_cache) return;

    http_method method = request.req_line->method;
    if(method != HTTP_HEAD && method != HTTP_GET) return;
    if(method == HTTP_GET && (request.received_headers & HDR_IF_MODIFIED_SINCE)) return;

    cache_key key = {
        .hostname = request.req_line->hostname,
        .port = request.req_line->port,
        .uri = request.req_line->uri,
    };

    int rc = cache_try_send_response(clientfd, &key);
    if(rc == 0){
        info("Successfully sent cached response to client fd=%d (%s:%s %s)", clientfd, key.hostname, key.port, key.uri);
        th_exit();
    }
    if(rc == ECACHE_ERRNO){
        log_fail_errno(error, cache_try_send_response);
        th_httperror_client(500, "Proxy error");
    }
}

void th_try_cache_response(void){
    if(request.is_no_cache || response.is_no_cache) return;

    http_method method = request.req_line->method;
    if(method != HTTP_HEAD && method != HTTP_GET) return;
    
    cache_key key = {
        .hostname = request.req_line->hostname,
        .port = request.req_line->port,
        .uri = request.req_line->uri,
    };

    cache_response cache_res = {
        .status_line = response.status_line,
        .headers = response.headers,
        .body = response.body,
        .content_length = response.content_length,
        .received_headers = response.received_headers,
        .expires = response.expires,
    };

    if(response.status_line->code == 200){
        int rc = cache_store_response(&key, &cache_res);
        if(rc == 0){
            debug("Successfully stored response from (%s:%s %s)", key.hostname, key.port, key.uri);
            response.status_line = NULL;
            response.headers = NULL;
            response.body = NULL;
            return;
        }
        if(rc == ECACHE_ERRNO){
            log_fail_errno(error, cache_store_response);
            th_exit();
        }
    }
    else if(response.status_line->code == 304){
        int rc = cache_update_response_headers(&key, &cache_res);
        if(rc == 0){
            debug("Successfully updated cached response headers (%s:%s %s)", key.hostname, key.port, key.uri);
            response.headers = NULL;
            return;
        }
        if(rc == ECACHE_ERRNO){
            log_fail_errno(error, cache_update_response_headers);
            th_exit();
        }
    }
}

void th_readline(void){
    if(read_buf_httpline(&rb, &recv_buf, READBUF_NOLIMIT) == -1){
        if(errno != 0) log_fail_errno(error, read_buf_httpline);
        else log_fail_fmt(debug, read_buf_httpline, "EOF (fd=%d)", rb.fd);
        th_exit();
    }
}

void th_httperror_client(int code, const char* error_message){
    if(!error_message) error_message = http_returncode_reason(code);
    info("Error: %s %d %s", PROXY_HTTP_VERSION, code, error_message);

    char* end = "\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    char buf[128];
    int size = snprintf(buf, sizeof(buf), "%s %d %s", PROXY_HTTP_VERSION, code, error_message);
    if(size >= sizeof(buf)) size = sizeof(buf)-1;

    write_n(clientfd, buf, size);
    write_n(clientfd, end, strlen(end));
    th_exit();
}