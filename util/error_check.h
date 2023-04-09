#ifndef ERROR_CHECK_H
#define ERROR_CHECK_H

#include <cstdio>
#include <stdlib.h>
#include <errno.h>

inline void ARGC_CHECK(int argc, int need_argc, const char* msg) {
    if(argc != need_argc) {
        printf("%s\n", msg);
        exit(-1);
    }
}

inline void ERROR_CHK(int ret, int error_ret, const char* msg) {
    if(ret == error_ret) {
        perror(msg);
        exit(-1);
    }
}

inline void ERROR_CHK(int ret, int error_ret, const char* msg, int except) {
    if(ret == error_ret && errno != except) {
        perror(msg);
        exit(-1);
    }
}

#endif