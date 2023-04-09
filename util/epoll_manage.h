#ifndef EPOLL_MANAGE_H
#define EPOLL_MANAGE_H

#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include "error_check.h"

void set_nonblock(int fd) {
    int flag = fcntl(fd, F_GETFL);
    ERROR_CHK(flag, -1, "getfl");
    flag |= O_NONBLOCK;
    int ret = fcntl(fd, F_SETFL, flag);
    ERROR_CHK(ret, -1, "setfl");
}

void epoll_add(int epfd, int fd, bool oneshot) {
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP; //加上挂起的监控
    if(oneshot) {
        event.events |= EPOLLONESHOT; //只触发一次，避免多个线程同时操作socket，但是必须线程每次重置，不然这辈子就只触发一次了
    }
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
    ERROR_CHK(ret, -1, "epoll_ctl");

    set_nonblock(fd);
}

int epoll_init(int listen_fd) {
    int epfd = epoll_create(233);
    ERROR_CHK(epfd, -1, "epoll_create");
    epoll_add(epfd, listen_fd, false);
    return epfd;
}

void epoll_rm(int epfd, int fd) {
    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, 0);
    ERROR_CHK(ret, -1, "epoll_ctl");
    ret = close(fd);
    ERROR_CHK(ret, -1, "close");
}

void epoll_mod(int epfd, int fd, int new_event) {
    struct epoll_event event;
    event.data.fd = fd;
    event.events = new_event | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event);
}

#endif