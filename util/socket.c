#include <unistd.h>
#include <errno.h>
#include <stdbool.h>

#include "socket.h"
#include "util.h"

#define LISTENQ 1024

static int connect_l(int fd, const sockaddr* addr, socklen_t len) {EINTR_LOOP(int, -1, connect, fd, addr, len)}

int accept_l(int fd, sockaddr* restrict addr, socklen_t* restrict addr_len) {EINTR_LOOP(int, -1, accept, fd, addr, addr_len)}

static int open_peerfd(char* hostname, char* port, bool is_server){
    struct addrinfo *listp, *curr;
    struct addrinfo hints = {
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV,
    };
    if(is_server) hints.ai_flags |= AI_PASSIVE;
    
    int rc = getaddrinfo(hostname, port, &hints, &listp);
    if(rc < 0) return rc-1;
    
    int sockfd;
    int optval = 1;
    for(curr = listp; curr; curr = curr->ai_next){
        if((sockfd = socket(curr->ai_family, curr->ai_socktype, curr->ai_protocol)) == -1)
            continue;
        
        if(is_server){
            if(
                setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == 0 &&
                bind(sockfd, curr->ai_addr, curr->ai_addrlen) == 0
            ) break;
        }
        else
            if(connect_l(sockfd, curr->ai_addr, curr->ai_addrlen) == 0) break;
        
        close(sockfd);
    }
    
    freeaddrinfo(listp);
    return curr ? sockfd : -1;
}

int open_clientfd(char* hostname, char* port){
    return open_peerfd(hostname, port, false);
}

int open_listenfd(char* port){
    int sockfd = open_peerfd(NULL, port, true);
    if(sockfd < 0) return sockfd;
    if(listen(sockfd, LISTENQ) == 0) return sockfd;
    
    close(sockfd);
    return -1;
}