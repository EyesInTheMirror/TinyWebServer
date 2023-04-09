#ifndef SIG_H
#define SIG_H

#include <signal.h>
#include <string.h>
#include "error_check.h"

void catch_sig(int sig, void(*handler)(int)) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    int ret = sigaction(sig, &sa, NULL);
    ERROR_CHK(ret, -1, "sigaction");
}


#endif