#include <cstdio>
#include <stdlib.h>

#include "./util/sig.h"
#include "./util/thread_pool.h"
#include "./util/locker.h"
#include "./util/http_conn.h"
#include "./util/error_check.h"
#include "./util/sock.h"
#include "./util/epoll_manage.h"

#include <sys/epoll.h>

#include "./util/lst_timer.h"
#include <assert.h>

static int pipefd[2];
static sort_timer_lst timer_lst;
const unsigned int TIMESLOT = 5;
static int epfd = 0;

void timer_handler()
{
    // 定时处理任务，实际上就是调用tick()函数
    timer_lst.tick();
    // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
    alarm(TIMESLOT);
}
// 定时器回调函数，它删除非活动连接socket上的注册事件，并关闭之。
void cb_func( http_conn* user_data )
{
    assert( user_data );
    epoll_rm(epfd, user_data->m_sockfd);
    printf( "close fd %d\n", user_data->m_sockfd );
}

void sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}


const int MAX_FD = 65535; //最大套接字个数
const int MAX_EVENTS = 10000; //一次监听的最大事件数量

int main(int argc, char* argv[]) {
    ARGC_CHECK(argc, 2 , "wrong format!");
    catch_sig(SIGPIPE, SIG_IGN); //SIGPIPE默认终止程序，改成忽略
    int ret;

    //线程池的创建
    thread_pool<http_conn> *pool = nullptr;
    try {
        pool = new thread_pool<http_conn>();
    }
    catch(...) {
        printf("error constructing pool!\n");
        exit(-1);
    }

    http_conn *users = new http_conn[MAX_FD]; //存放客户端信息

    int lfd = listen_init(nullptr, argv[1], true);

    epfd = epoll_init(lfd);

    // 创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert( ret != -1 );
    set_nonblock( pipefd[1] );
    epoll_add( epfd, pipefd[0], false);
    // 设置信号处理函数
    catch_sig( SIGALRM , sig_handler);
    catch_sig( SIGTERM , sig_handler);
    bool stop_server = false;

    struct epoll_event events[MAX_EVENTS];
    http_conn::m_epfd = epfd;
    http_conn::m_user_cnt = 0;

    bool timeout = false;
    alarm(TIMESLOT);

    while(!stop_server) {
        int num = epoll_wait(epfd, events, MAX_EVENTS, -1);
        ERROR_CHK(num, -1, "epoll_wait", EINTR);

        for(int i = 0; i < num; ++i) {
            int sfd = events[i].data.fd;
            unsigned int ev = events[i].events;
            if(sfd == lfd) {
                struct sockaddr_in clientaddr;
                socklen_t addrlen = sizeof(clientaddr);
                int clientfd = accept(lfd, (sockaddr*)&clientaddr, &addrlen);
                ERROR_CHK(clientfd, -1, "accept");

                if(http_conn::m_user_cnt >= MAX_FD) { //服务器正忙，无法处理用户请求
                    close(clientfd);
                    continue;
                }

                users[clientfd].init(clientfd, clientaddr);

                // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中
                util_timer* timer = new util_timer;
                timer->user_data = &users[clientfd];
                timer->cb_func = cb_func;
                time_t cur = time( NULL );
                timer->expire = cur + 3 * TIMESLOT;
                users[clientfd].timer = timer;
                timer_lst.add_timer( timer );
            }
            else if(ev & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //异常断开
                users[sfd].close_conn();
            }
            else if(sfd == pipefd[0] && ev & EPOLLIN) {
                // 处理信号
                int sig;
                char signals[1024];
                ret = recv( pipefd[0], signals, sizeof( signals ), 0 );
                if(ret > 0) {
                    for( int i = 0; i < ret; ++i ) {
                        switch( signals[i] )  {
                            case SIGALRM:
                            {
                                // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                                // 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务。
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                                break;
                            }
                        }
                    }
                }
            }
            else if(ev & EPOLLIN) {
                util_timer* timer = users[sfd].timer;
                if(users[sfd].read()) {

                    // 如果某个客户端上有数据可读，则我们要调整该连接对应的定时器，以延迟该连接被关闭的时间。
                    if( timer ) {
                        time_t cur = time( NULL );
                        timer->expire = cur + 3 * TIMESLOT;
                        printf( "adjust timer once\n" );
                        timer_lst.adjust_timer( timer );
                    }

                    pool->append(&users[sfd]);
                }
                else {
                    cb_func( &users[sfd] );
                    if( timer )
                    {
                        timer_lst.del_timer( timer );
                    }
                    users[sfd].close_conn();
                }
            }
            else if(ev & EPOLLOUT) {
                if(!users[sfd].write()) {
                    users[sfd].close_conn();
                }
            }
            else {
                printf("unknown event!\n");
            }
        }

        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if( timeout ) {
            timer_handler();
            timeout = false;
        }
    }

    close(epfd);
    close(lfd);
    delete[] users;
    delete pool;

    close( pipefd[1] );
    close( pipefd[0] );

    return 0;
}