#include "threadpool.h"
#include "http_conn.h"
#include "lst_timer.h"

#define MAX_FD 65536                // 最大的客户端的个数
#define MAX_EVENT_NUMBER 10000      // 监听的最大的事件数量



//添加信号捕捉
void addsig(int sig, void( handler )(int)){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    sigfillset( &sa.sa_mask );  //将信号集中的所有的标志位置为1
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

/*----------------------用于定时器的函数-------------------------------*/

static int pipefd[2];               // 管道文件描述符 0为读，1为写，用于定时器写入&主线程读取信号
// 向管道写数据的信号捕捉回调函数
void sig_to_pipe(int sig){
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 ); //向管道中写入信号
    errno = save_errno;
}

// 设置文件描述符为非阻塞
void set_nonblocking(int fd){
    int flag = fcntl(fd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);
}
/*-------------------------------------------------------------*/


int main(int argc, char const *argv[])
{
    /* code */
    if( argc <= 1 ) {
        printf( "usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    int port = atoi( argv[1] ); //把命令行输入的port转换为int类型
    printf("port using is: %d\n", port);
    addsig( SIGPIPE, SIG_IGN );

    //初始化线程池，因为是模板类，模板对应一个http_conn任务
    threadpool< http_conn >* pool = NULL;
    try {
        pool = new threadpool<http_conn>;
    } catch( ... ) {
        return 1;
    }

    //定义一个数组，数组中每个元素都是连接进来的客户端的任务信息
    http_conn* users = new http_conn[ MAX_FD ];

    //主线程
    int listenfd = socket( AF_INET, SOCK_STREAM, 0 );
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // 端口复用
    int ret;
    int reuse = 1;
    ret = setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    if(ret != 0)
    {
        printf("setsockopt error\n");
        exit(-1);
    }
    //绑定
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    if(ret != 0)
    {
        printf("bind error\n");
        exit(-1);
    }
    else
    {
        printf("bind success\n");
    }
    //监听
    ret = listen( listenfd, 5 );
    if(!ret)
    {
        printf("listening...\n");
    }
    // 创建epoll对象，和事件数组，添加
    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );            //5没有意义，只要大于0即可

    // 添加listenfd到epoll对象中，不设置oneshot，不使用ET模式
    addfd( epollfd, listenfd, false, false );


    // 创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);  //一次性创建两个（一对）socket
    assert( ret != -1 );
    set_nonblocking( pipefd[1] );               // 管道写端设置为非阻塞
    addfd(epollfd, pipefd[0], false, false );   // epoll检测管道读端：管道中有没有可以读取的数据
    // 设置信号处理函数
    addsig(SIGALRM, sig_to_pipe);               // 定时器信号
    addsig(SIGTERM, sig_to_pipe);               // SIGTERM 关闭服务器

    bool timeout = false;                       // 定时器周期已到
    bool stop_server = false;
    alarm(TIMESLOT);                            // 定时产生SIGALRM信号


    //所有任务共享同一个epoll实例，是static的
    http_conn::m_epollfd = epollfd;

    while(!stop_server) {
        
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );   //发生事件的文件描述符，内核会自动写入events中
        
        if ( ( number < 0 ) && ( errno != EINTR ) ) {
            printf( "epoll failure\n" );
            break;
        }

        //有number个监听的描述符发生事件
        for ( int i = 0; i < number; i++ ) {
            int sockfd = events[i].data.fd;
            if( sockfd == listenfd ) 
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                if ( connfd < 0 ) {
                    printf( "errno is: %d\n", errno );
                    continue;
                } 
                // 目前连接数满了
                if( http_conn::m_user_count >= MAX_FD ) {
                    close(connfd);
                    continue;
                }
                // 将新客户端数据初始化，放到数组中
                users[connfd].init( connfd, client_address);

            }
            //如果客户端关闭：不管是正常关闭/错误关闭
            else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) ) 
            {
                users[sockfd].close_conn(); //从epoll实例中删除这个监听的fd，并把这个fd设为-1
                //找到这个定时器（users[sockfd].timer），并从链表（共享的m_timer_lst）中移除
                http_conn::m_timer_lst.del_timer(users[sockfd].timer);
            }
            //如果客户端发来数据
            else if(events[i].events & EPOLLIN) 
            {
                //如果主线程成功读取客户端发来的数据
                if(users[sockfd].read()) {
                    //交给工作线程处理
                    pool->append(users + sockfd);
                } 
                //否则关闭这个任务
                else {
                    users[sockfd].close_conn();
                    http_conn::m_timer_lst.del_timer(users[sockfd].timer);  // 移除其对应的定时器
                }
            }
            //如果工作线程要发数据
            else if( events[i].events & EPOLLOUT ) 
            {
                //主线程向客户端发送数据
                if( !users[sockfd].write() ) {
                    users[sockfd].close_conn();
                }
            }


            //定时器发来信号：如果是管道读端收到数据且是可读事件，说明有定时器超时
            else if(sockfd == pipefd[0] && (events[i].events & EPOLLIN))
            {  
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if(ret == -1){
                    continue;
                }else if(ret == 0){
                    continue;
                }else{
                    for(int i = 0; i < ret; ++i)
                    {
                        switch (signals[i]) // 字符ASCII码
                        {
                            case SIGALRM:
                                // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                                // 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的IO任务。
                                timeout = true;
                                break;
                            case SIGTERM:
                                stop_server = true;
                        }
                    }
                }
            }
        }
        // 最后处理定时器事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if(timeout) {   //如果有定时器超时
            // 定时处理任务，实际上就是调用tick()函数
            http_conn::m_timer_lst.tick();
            // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
            alarm(TIMESLOT);
            timeout = false;    // 重置timeout
        }
    }

    close(epollfd);
    close( listenfd );
    delete [] users;
    delete pool;

    return 0;
}
