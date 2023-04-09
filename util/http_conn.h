#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include "sock.h"
#include "epoll_manage.h"
#include "lst_timer.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/uio.h>

class util_timer;
class http_conn {
public:
    static int m_epfd;
    static int m_user_cnt;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 2048;
    static const int FILEPATH_LEN = 200;

    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
    enum LINE_STATE {LINE_OK = 0, LINE_BAD, LINE_OPEN};
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};

    http_conn() = default;
    ~http_conn() = default;
    void process(); //解析http请求，封装响应信息
    void init(int fd, const sockaddr_in & addr);
    void close_conn();
    bool read();
    bool write();

    util_timer* timer;//定时器

    int m_sockfd; //这个HTTP连接的socket
    sockaddr_in m_saddr; //通信的地址信息

    char m_read_buf[READ_BUFFER_SIZE];
    
private:
    
    int m_read_idx; //已经读入的下一个位置

    char m_file_dir[FILEPATH_LEN];

    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;

    void init_stat();
    HTTP_CODE process_read();//解析HTTP请求
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    LINE_STATE parse_line();
    bool process_write(HTTP_CODE ret);

    char* get_line() {return &m_read_buf[m_start_line_idx];}//获取一行数据

    int m_checked_idx; // 当前分析的字符在读缓冲区的位置
    int m_start_line_idx; // 当前解析行起始位置

    CHECK_STATE m_check_stat; //主状态机状态

    char* m_url;
    char* m_version;
    char* m_host;
    bool m_keep_alive;
    METHOD m_method;

    int m_content_length; 


    char* m_file_address;                   // 客户请求的目标文件被mmap到内存中的起始位置
    struct stat m_file_stat;                // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec m_iv[2];                   // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iv_count;
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
};

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char* doc_root = "/home/mirror/Documents/webserver/resources";

int http_conn::m_epfd = -1;
int http_conn::m_user_cnt = 0;


void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST) {//请求不完整，继续读
        epoll_mod(m_epfd, m_sockfd, EPOLLIN);//由于有ONESHOT，重新注册
        return;
    }

    bool write_ret = process_write(read_ret);
    if(!write_ret) {
        close_conn();
    }
    epoll_mod(m_epfd, m_sockfd, EPOLLOUT);
}

void http_conn::init(int fd, const sockaddr_in & addr) {
    m_sockfd = fd;
    m_saddr = addr;
    sock_reuseaddr(m_sockfd);

    epoll_add(m_epfd, m_sockfd, true);
    ++m_user_cnt;

    init_stat();
}

void http_conn::close_conn() {
    if(m_sockfd != -1) {
        epoll_rm(m_epfd, m_sockfd);
        m_sockfd = -1;
        --m_user_cnt;
    }
}

bool http_conn::read() {
    if(m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    int bytes_read = 0;
    while(true) {
        bytes_read = recv(m_sockfd, &m_read_buf[m_read_idx], READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                //没数据了
                break;
            }
            return false;
        }
        else if(bytes_read == 0) {
            //对方关闭
            return false;
        }
        m_read_idx += bytes_read;
    }
    printf("request read!\n");
    //printf("request read:\n%s", m_read_buf);
    return true;
}

bool http_conn::write() {
    int temp = 0;
    int bytes_have_send = 0;    // 已经发送的字节
    int bytes_to_send = m_write_idx;// 将要发送的字节 （m_write_idx）写缓冲区中待发送的字节数
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        epoll_mod( m_epfd, m_sockfd, EPOLLIN ); 
        init_stat();
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                epoll_mod( m_epfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if ( bytes_to_send <= bytes_have_send ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if(m_keep_alive) {
                init_stat();
                epoll_mod( m_epfd, m_sockfd, EPOLLIN );
                return true;
            } else {
                epoll_mod( m_epfd, m_sockfd, EPOLLIN );
                return false;
            } 
        }
    }
}

//主状态机
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATE line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while(((m_check_stat == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || (line_status = parse_line()) == LINE_OK) {
        //解析到请求体了(???)，或者解析到一行完整的数据了

        text = get_line();
        m_start_line_idx = m_checked_idx;
        //printf("line: %s\n", text);

        switch (m_check_stat)
        {
        case CHECK_STATE_REQUESTLINE:
            ret = parse_request_line(text);
            if(ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }
            break;
        case CHECK_STATE_HEADER:
            ret = parse_headers(text);
            if(ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }
            else if(ret == GET_REQUEST) {
                //请求完成，解析具体内容
                return do_request();
            }
            break;
        case CHECK_STATE_CONTENT:
            ret = parse_content(text);
            if(ret == GET_REQUEST) {
                //请求完成，解析具体内容
                return do_request();
            }
            line_status = LINE_OPEN;
            break;
        default:
            return INTERNAL_ERROR;
            break;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request() {
    strcpy( m_file_dir, doc_root );
    int len = strlen( doc_root );
    strncpy( m_file_dir + len, m_url, FILEPATH_LEN - len - 1 );
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( m_file_dir, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( m_file_dir, O_RDONLY );
    // 创建内存映射
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}

void http_conn::unmap() {
    if( m_file_dir )
    {
        munmap( m_file_dir, m_file_stat.st_size );
        m_file_address = 0;
    }
}

bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}
// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ... ) {
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) &&
    add_content_type() && 
    add_linger() && 
    add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_keep_alive == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
    m_url = strpbrk(text, " \t");

    *m_url++ = '\0';

    char* method = text;
    if(strcasecmp(method, "GET") == 0) {
        m_method = GET;
    }
    else return BAD_REQUEST;

    m_version = strpbrk(m_url, " \t");
    if(!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if(strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    if(strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if(!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    m_check_stat = CHECK_STATE_HEADER;
 
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 ) {
            m_check_stat = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_keep_alive = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
        //printf( "oops! unknown header %s\n", text );
    }
    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    if ( m_read_idx >= ( m_content_length + m_checked_idx ) )
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::LINE_STATE http_conn::parse_line() {
    char tmp;
    for(; m_checked_idx < m_read_idx; ++m_checked_idx) {
        tmp = m_read_buf[m_checked_idx];
        if(tmp == '\r') {
            if(m_checked_idx + 1 == m_read_idx) {
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_idx + 1] == '\n') {
                //也就是把\r\n变成\0
                m_read_buf[m_checked_idx] = '\0';
                ++m_checked_idx;
                m_read_buf[m_checked_idx] = '\0';
                ++m_checked_idx;
                return LINE_OK;
            }
            else return LINE_BAD;
        }
        else if(tmp == '\n') {
            if((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) { //正好卡在\r那里(但是为什么能出现这个情况呢??)
                m_read_buf[m_checked_idx - 1] = '0';
                m_read_buf[m_checked_idx++] = '0';
                return LINE_OK;
            }
            else return LINE_BAD;
        } 
    }
    return LINE_OPEN;
}
 
void http_conn::init_stat() {
    m_check_stat = CHECK_STATE_REQUESTLINE; //初始状态分析请求行
    m_checked_idx = 0;
    m_start_line_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_keep_alive = false;
    m_host = 0;
    bzero(m_read_buf, READ_BUFFER_SIZE); 
    bzero(m_write_buf, WRITE_BUFFER_SIZE); 
    bzero(m_file_dir, FILEPATH_LEN); 
    m_content_length = 0;   
}


#endif