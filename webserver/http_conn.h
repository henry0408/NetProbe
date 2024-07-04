#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include "locker.h"


// 读缓冲区的大小
#define READ_BUFFER_SIZE 2048
// 写缓冲区的大小 
#define WRITE_BUFFER_SIZE 1024

//文件路径长度
#define FILENAME_LEN 200

#define TIMESLOT 5                  //定时器时长

class sort_timer_lst;
class util_timer;

class http_conn
{
private:
    int m_sockfd;                           //该任务（连接）的socket fd
    sockaddr_in m_address;                  //该任务（连接）的地址
    char m_read_buf[ READ_BUFFER_SIZE ];    // 读缓冲区
    int m_read_idx;                         // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置（读到的字节数

    char m_write_buf[ WRITE_BUFFER_SIZE ];  //写缓冲区
    int m_write_idx;                         // 标识写缓冲区中已经写入的数据的最后一个字节的下一个位置（写进去的字节数

public:
    static int m_epollfd;                       // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
    static int m_user_count;                    // 统计连接进来的客户端的数量
    static sort_timer_lst m_timer_lst;          // 共享一个升序的定时器链表

    util_timer* timer;              // 定时器

    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };

    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

    /*
        服务器处理HTTP请求的可能(最终)结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST=0, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    
    int m_checked_idx;                      // 当前正在解析的字符在读缓冲区中的位置，最后不断移动一直到当前解析行的最后一位
    int m_start_line;                       // 当前正在解析的行的起始位置
    CHECK_STATE m_check_state;              // 主状态机当前所处的状态

    /* HTTP请求第一行可以获取到的：GET /index.html HTTP/1.1 */
    METHOD m_method;                        // 请求方法
    char *m_url;                            // 客户请求的目标文件的文件名
    char *m_version;                        // HTTP协议版本号，我们仅支持HTTP1.1

    char url_name[100];                            // 客户请求的目标文件的文件名
    char version_name[100];                        // HTTP协议版本号，我们仅支持HTTP1.1

    /* HTTP请求头可以获取到的 */
    /*
    Host: 192.168.126.141:10000
    Connection: keep-alive
    Content-length: xxx
    ...
    */
    char *m_host;                           //主机名
    char host_name[100];
    bool keepConnect;                       //是否一直连接
    int m_content_length;                   //如果有请求体，从包头的关键字可以查看请求体长度

    char m_real_file[ FILENAME_LEN ];       //HTTP GET要获取的文件的path
    struct stat m_file_stat;                // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    char* m_file_address;                   // 客户请求的目标文件被mmap到内存中的起始位置

    // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    struct iovec m_iv[2];                   //被写内存块（正常情况应该有两块，分别是客户端请求的文件的映射m_file_address，以及回复的报文m_write_buf
    int m_iv_count;                         //被写内存块的数量
    int bytes_to_send;                      // 将要发送的数据的字节数
    int bytes_have_send;                    // 已经发送的字节数


private:
    //char* getline();    //从m_read_buf中，获取当前行的首地址

public:
    http_conn(){}
    ~http_conn(){}

    //客户端自己的任务函数
    void process();

    //将client_sockfd添加到epoll实例
    void init(int client_sockfd, sockaddr_in& client_addr);

    //初始化状态信息
    void state_init();

    //客户端关闭连接
    void close_conn();

    //非阻塞，一次性的写
    bool read();

    //非阻塞，一次性的读
    bool write();

    //从读到read的buffer中解析HTTP请求
    HTTP_CODE process_read();

    //根据服务器处理HTTP请求的结果，把决定返回给客户端的内容写入写缓冲区，作为HTTP响应
    bool process_write(HTTP_CODE read_ret);

    // 下面这一组函数被process_read调用以分析HTTP请求
    HTTP_CODE parse_request_line( char* text ); //分析请求首行
    HTTP_CODE parse_headers( char* text );      //解析请求头部
    HTTP_CODE parse_content( char* text );      //解析请求体

    //下面的函数用于提取某一行(根据\r\n)
    LINE_STATUS parse_line(char* line);

    //获取请求包头后，进行处理
    HTTP_CODE do_request();

    /* -----------------下面这一组函数被process_write调用以填充HTTP应答。---------------- */
    //取消内存映射（删除要发送给客户端的文件的信息）
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

/* 添加文件描述符到epoll实例 */
void addfd( int epollfd, int fd, bool one_shot, bool if_ET );

// 从epoll中移除监听的文件描述符
void removefd( int epollfd, int fd );

// 修改epoll实例中的文件描述符，用于重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev);

#endif