#include "http_conn.h"
#include "lst_timer.h"

#include <string>
#include <iostream>
using namespace std;

//设置非阻塞
int setnonblocking( int fd ) {
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

/* 添加监听的文件描述符到epoll实例 */
void addfd( int epollfd, int fd, bool one_shot, bool if_ET )
{
    struct epoll_event connect_event;

    connect_event.data.fd = fd;
    //如果使用了one-Shot模式
    if(one_shot) 
    {
        // 防止同一个socket通信被不同的线程处理
        connect_event.events |= EPOLLONESHOT;
    }

    if(if_ET == true)
    {
        connect_event.events = EPOLLIN | EPOLLET |EPOLLRDHUP;
    }
    else    //对于listen fd，应该不使用ET模式，而是用LT模式
    {
        //一个是有可读事件，一个是用来处理对端关闭
        connect_event.events = EPOLLIN | EPOLLRDHUP;
    }
    
    int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &connect_event);
    
    //由于使用ET模式，需要设置文件描述符非阻塞：
    setnonblocking(fd); 
}

// 从epoll中移除监听的文件描述符
void removefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close(fd);
}

// 修改epoll实例中的文件描述符，并且重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;    //ET模式
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}


/*对于类中静态变量，必须类内声明，类外初始化。在编译阶段分配内存。*/
int http_conn::m_epollfd = -1;       // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
int http_conn::m_user_count = 0;    // 统计连接进来的客户端的数量
sort_timer_lst http_conn::m_timer_lst;  //共享的定时器链表

//客户端初始化连接
void http_conn::init(int client_sockfd, sockaddr_in& client_addr)
{
    m_sockfd = client_sockfd;
    m_address = client_addr;

    // 端口复用
    int reuse = 1;
    setsockopt( m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );

    //添加当前连接的客户端的socket fd到epoll实例中，使用ET模式
    addfd( m_epollfd, m_sockfd, true, true );

    //客户端数量+1
    m_user_count++;

    //初始化解析状态
    state_init();

    printf("The No.%d user. sock_fd = %d\n", m_user_count, m_sockfd);


    //添加当前定时器到定时器链表
    // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中
    util_timer* new_timer = new util_timer;
    new_timer->user_data = this;
    time_t curr_time = time(NULL);
    new_timer->expire = curr_time + 3 * TIMESLOT;
    this->timer = new_timer;
    m_timer_lst.add_timer(new_timer); 
}

void http_conn::state_init()
{
    //用于read
    m_checked_idx = 0;                      // 当前正在解析的字符在读缓冲区中的位置
    m_start_line = 0;                       // 当前正在解析的行的起始位置
    m_check_state = CHECK_STATE_REQUESTLINE;              // 主状态机当前所处的状态，最初为解析首行
    bzero(m_read_buf, sizeof(m_read_buf));
    m_read_idx = 0;

    //解析请求首行
    m_method = GET;         // 默认请求方式为GET
    m_url = 0;              
    m_version = 0;

    //解析请求头
    keepConnect = false;                            // 默认不保持链接  Connection : keep-alive保持连接
    m_content_length = 0;
    m_host = 0;
    bzero(url_name,sizeof(url_name));                            // 客户请求的目标文件的文件名
    bzero(version_name,sizeof(version_name));
    bzero(host_name,sizeof(host_name));
    bzero(m_real_file, FILENAME_LEN);

    //用于write
    bzero(m_write_buf, sizeof(m_write_buf));
    m_write_idx = 0;
    m_iv_count = 0;

    bytes_to_send = 0;
    bytes_have_send = 0;

}

//客户端关闭连接
void http_conn::close_conn()
{
    //如果客户端fd存在
    if(m_sockfd != -1) {
        //从epoll实例中删除这个监听的fd
        removefd(m_epollfd, m_sockfd);
        m_user_count--; // 关闭一个连接，将客户总数量-1
        printf("closing fd: %d, user_count is: %d\n", m_sockfd, m_user_count);

        m_sockfd = -1;  //让这个fd = -1
    }
}

//ET模式，所以需要非阻塞，一次性的读完客户端发来的数据，保存到m_read_buf中。
bool http_conn::read()
{
    //任务活跃，如果定时器存在，先更新定时器超时时间
    if(timer) {             
        time_t curr_time = time( NULL );
        timer->expire = curr_time + 3 * TIMESLOT;
        m_timer_lst.adjust_timer( timer );
    }

    if( m_read_idx >= READ_BUFFER_SIZE ) {
        return false;
    }
    int bytes_read = 0;
    while(true) {
        // 从m_read_buf + m_read_idx索引处开始保存数据，大小是READ_BUFFER_SIZE - m_read_idx
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 );
        if (bytes_read == -1) {
            if( errno == EAGAIN || errno == EWOULDBLOCK ) {
                // 没有数据
                break;
            }
            return false;   
        } else if (bytes_read == 0) {   // 对方关闭连接
            return false;
        }
        //printf("reading...\n");
        m_read_idx += bytes_read;
        //printf("m_read_idx: %d\n",m_read_idx);
    }
    //printf("read: %s\n", m_read_buf);
    return true;
}

// char* http_conn::getline()
// {
//     return m_read_buf + m_start_line;
// }


/*------------------------下面这一组函数被process_read调用以解析read buffer中的HTTP请求----------------------------------*/

//解析请求首行：GET /index.html HTTP/1.1，我们要获得url：/index.html， method（GET等），版本号（HTTP/1.1）
//参数：text为一行数据
http_conn::HTTP_CODE http_conn::parse_request_line( char* text )
{
    m_url = strpbrk(text, " \t"); // 判断第二个参数中的字符哪个在text中最先出现
    if (! m_url) { 
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';    // 置位空字符，字符串结束符，把GET取出来
    char* method = text;
    if ( strcasecmp(method, "GET") == 0 ) { // 忽略大小写比较
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    m_version = strpbrk( m_url, " \t" );
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if (strcasecmp( m_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }
    /**
     * http://192.168.126.141:10000/index.html
    */
    if (strncasecmp(m_url, "http://", 7) == 0 ) {   
        m_url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr( m_url, '/' );
    }
    if ( !m_url || m_url[0] != '/' ) {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER; // 检查状态变成检查头
    return NO_REQUEST;
}

//解析（部分）请求头部
/*
    Host: 192.168.126.141:10000
    Connection: keep-alive
    Content-length: xxx
*/
http_conn::HTTP_CODE http_conn::parse_headers( char* text )   
{
    //printf("parse_headers...\n");
    // 遇到空行，表示HTTP头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 ) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } 
    else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) 
    {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            keepConnect = true;
        }
        else
        {
            keepConnect = false;
        }
    } 
    else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) 
    {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } 
    else if ( strncasecmp( text, "Host:", 5 ) == 0 ) 
    {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;

        //printf("HHHHost: %s\n",m_host);
        sprintf(host_name,"%s",m_host);
    } 
    else 
    {
        printf( "oop! unknow header %s\n", text );
    }
    return NO_REQUEST;
}

//解析请求体
//我们没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_content( char* text )   
{
    if ( m_read_idx >= ( m_content_length + m_checked_idx ) )
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//下面的函数用于提取某一行(根据\r\n)，将当前行数据保存到my_line中。最后的m_checked_idx为读完一行后的index
http_conn::LINE_STATUS http_conn::parse_line(char* my_line)
{
    char temp;
    //一个一个字节从read得到的m_read_buf中读取获取到的数据
    for ( ; m_checked_idx < m_read_idx; ++m_checked_idx ) {
        temp = m_read_buf[ m_checked_idx ];
        if ( temp == '\r' ) 
        {
            if ( ( m_checked_idx + 1 ) == m_read_idx ) 
            {
                return LINE_OPEN;
            }
            //读到\r\n
            else if ( m_read_buf[ m_checked_idx + 1 ] == '\n' ) 
            {
                m_read_buf[ m_checked_idx++ ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';

                char* line = m_read_buf + m_start_line;
                sprintf(my_line, "%s", line);
                m_start_line = m_checked_idx;
                //printf( "got 1 http line: %s\n", line );
                return LINE_OK;
            }
            return LINE_BAD;
        }

        //注意：这种情况是因为上一次没有读到\n导致是LINE_OPEN状态
        else if( temp == '\n' )  
        {
            if( ( m_checked_idx > 1) && ( m_read_buf[ m_checked_idx - 1 ] == '\r' ) ) 
            {
                m_read_buf[ m_checked_idx-1 ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                char* line = m_read_buf + m_start_line;
                sprintf(my_line, "%s", line);
                m_start_line = m_checked_idx;
                //printf( "got 1 http line: %s\n", line );
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 当得到一个完整、正确的HTTP请求时，我们就分析客户端request的目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将文件内容映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
    //printf("do request......\n");
    // 服务器的根目录
    char doc_root[100] = "/home/hangyu/niuke/webserver/resources";
    strcpy(m_real_file,doc_root);
    strcat(m_real_file,url_name);   //m_real_file应该保存文件的路径：/home/hangyu/niuke/webserver/resources/index.html

    printf("m_real_file: %s\n",m_real_file);

     // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        printf("no resources\n");
        return NO_RESOURCE;
    }

    // 判断访问权限
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        printf("forbidden request\n");
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        printf("dir, not a file\n");
        return BAD_REQUEST;
    }

    //printf("m_real_file: %s\n",m_real_file);

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    // 创建内存映射
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );

    // //从内存映射读出数据：
    // char buf[1000];
    // strcpy(buf,(char*)m_file_address);
    // printf("%s\n", buf);

    close( fd );
    return FILE_REQUEST;
}


// 对内存映射区执行munmap操作
void http_conn::unmap() 
{
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}

//最后的函数（调用上面的函数），解析读取的报文，最后解析的结果作为元素保存在http_conn类中
http_conn::HTTP_CODE http_conn::process_read()
{
    //初始为没有读到一行
    LINE_STATUS line_status = LINE_OPEN;
    //初始状态为没有请求
    HTTP_CODE status = NO_REQUEST;

    //循环获取一行

    //如果主状态在解析请求体，且读到了完整一行时
    //或者提取到一行时
    //while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || ((line_status = parse_line()) == LINE_OK))
    char line[1024]; //用于保存获取到的一行
    while ((line_status = parse_line(line)) == LINE_OK)
    {
        switch ( m_check_state ) {
            //如果正在解析首行
            case CHECK_STATE_REQUESTLINE: {
                //调用解析首行的函数，如果成功则会进入下一个state（在函数中实现）
                status = parse_request_line(line);
                if ( status == BAD_REQUEST ) {
                    return BAD_REQUEST;
                }
                else
                {
                    printf("Current state is: %d\n",m_check_state);
                    printf("url: %s\n", m_url);
                    sprintf(url_name,"%s",m_url);
                    
                    printf("version: %s\n", m_version);
                    sprintf(version_name,"%s",m_version);
                }
                break;
            }
            //如果正在解析请求头
            case CHECK_STATE_HEADER: {
                //调用解析请求头的函数
                status = parse_headers(line);
                if ( status == BAD_REQUEST ) 
                {
                    return BAD_REQUEST;
                } 
                //如果HTTP请求只有请求头，且获取了一个完整的请求头（该行只有一个\r\n），可以处理具体的请求信息
                else if ( status == GET_REQUEST ) 
                {
                    printf("Current state is: %d\n",m_check_state);
                    printf("url_name: %s\n", url_name);
                    printf("version_name: %s\n", version_name);
                    printf("keepConnect: %d\n",keepConnect);
                    printf("m_content_length: %d\n", m_content_length);
                    printf("host_name: %s\n",host_name);
                    return do_request();
                }
                break;
            }
            //如果正在解析请求体
            case CHECK_STATE_CONTENT: {
                status = parse_content(line);
                if ( status == GET_REQUEST ) {
                    return do_request();
                }
                //注意读取完请求体后，说明当前HTTP请求获取完成，行状态返回到最初的状态。
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

/*---------------------------------------下面代码用于向客户端返回信息---------------------------------------------*/


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


// 往写缓冲m_write_buf中写入待发送的数据，参数是要发送的数据，是一个可变参数，下面的几个add函数都调用了此函数
bool http_conn::add_response( const char* format, ... ) {
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;   //用于处理可变参数，相当于指向一个列表的指针
    va_start( arg_list, format );
    //声明一个va_list类型的变量arg_list，并用va_start宏初始化它，准备处理变长参数。format是第一个参数，后面的参数将被存入arg_list中。

    // 使用vsnprintf函数将格式化后的字符串写入到m_write_buf缓冲区中，写入位置从m_write_idx开始。
    // WRITE_BUFFER_SIZE - 1 - m_write_idx是剩余缓冲区的大小，确保不会溢出。
    // format是格式字符串，arg_list是变长参数列表。vsnprintf返回实际写入的字符长度。
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

// 添加response首行
bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

// 添加response中的content length部分
bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

//添加response中的content type部分（目前只有html文件）
bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

//添加response中的Connection是否长连接部分
bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( keepConnect == true ) ? "keep-alive" : "close" );
}

//添加response header后面的空行
bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

// 添加response header（我们只添加了header中的4个部分）
bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}


//添加response content（回复体）
bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

//根据服务器处理HTTP请求的结果，把决定返回给客户端的内容封装到m_iv[]数组
//正常会有两部分：1）response报文：m_write_buf 2）客户端请求的网页/静态资源：m_file_address
bool http_conn::process_write( http_conn::HTTP_CODE read_ret )
{
    switch (read_ret)
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

            //拼装1）客户端请求的file，2）response header（m_write_buf）
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;

            bytes_to_send = m_write_idx + m_file_stat.st_size;  //一共要发送多少字节的数据

            return true;
        default:
            return false;
    }

    //如果没有请求文件/请求失败/没有文件...，只需要返回response header（m_write_buf）即可
    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

//非阻塞，一次性的写。把上面的m_iv[]中的两个部分（或者只有一个部分）发送给客户端（主线程）
bool http_conn::write()
{
    printf("write\n");
    // 如果write说明任务活跃，先更新超时时间
    if(timer) {             
        time_t curr_time = time( NULL );
        timer->expire = curr_time + 3 * TIMESLOT;
        m_timer_lst.adjust_timer( timer );
    }


    int temp = 0;
    if ( bytes_to_send == 0 ) {
        // 要发送的字节为0，说明这一次响应结束了，改为重新监听读状态。
        modfd( m_epollfd, m_sockfd, EPOLLIN ); 
        state_init();
        return true;
    }

    while(1) {
        // 分散写，把m_iv[]中的内容写给客户端
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            //释放映射（请求的文件的内存的地址）
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0)
        {
            // 没有数据要发送了
            unmap();    ////释放映射（请求的文件的内存的地址）
            modfd(m_epollfd, m_sockfd, EPOLLIN);    //修改为监听读事件

            //如果长连接
            if (keepConnect)
            {
                state_init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}


//线程池的工作线程执行的函数，处理HTTP请求
void http_conn::process()
{
    printf("=======parse request, create response.=======\n");
    

    //解析HTTP请求
    printf("=============process_reading=============\n");
    HTTP_CODE read_ret = process_read();    //HTTP_CODE为报文结果
    if ( read_ret == NO_REQUEST ) {
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        return;
    }

    // 生成响应
    bool write_ret = process_write( read_ret );
    if ( !write_ret ) {
        close_conn();
        if(timer) m_timer_lst.del_timer(timer);  // 移除其对应的定时器
    }
    modfd( m_epollfd, m_sockfd, EPOLLOUT);      //这次监听OUT，看什么时候可以写给客户端
}


