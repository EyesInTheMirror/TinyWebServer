#ifndef SOCK_H
#define SOCK_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "error_check.h"

void sock_reuseaddr(int lfd) {
    int reuse = 1;
    int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)); //在bind前设置端口复用
    ERROR_CHK(ret, -1, "setsockopt");
}

int listen_init(const char* addr, const char* port, bool any) {
    int ret = 0;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    ERROR_CHK(lfd, -1, "socket");

    sock_reuseaddr(lfd);

    int i_port = atoi(port);
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(i_port);
    if(any) {
        saddr.sin_addr.s_addr = INADDR_ANY;
    }
    else {
        ret = inet_pton(AF_INET, addr, &saddr.sin_addr.s_addr);
        ERROR_CHK(ret, -1, "pton");
    }
    ret = bind(lfd, (struct sockaddr*)&saddr, sizeof(saddr));
    ERROR_CHK(ret, -1, "bind");
    
    ret = listen(lfd, 5); //第二个参数控制请求队列长度，accept后ESTABLISHED状态队列+1，新连接到达在SYN_RCVD状态队列-1
    ERROR_CHK(ret, -1, "listen");

    return lfd;
}

#endif