#pragma once

#include <sys/socket.h>
#define __USE_XOPEN2K
#include <netdb.h>

typedef struct sockaddr sockaddr;

int open_listenfd(char* port);
int open_clientfd(char* hostname, char* port);
int accept_l(int fd, sockaddr* restrict addr, socklen_t* restrict addr_len);