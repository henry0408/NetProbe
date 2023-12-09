#include "apue.h"
#include <pthread.h>
#include <iostream>
#include <errno.h>
#include <string>
#include <cstring>
#include <sys/resource.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <math.h>
#include "threadpool.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <netdb.h>
#define INET_ADDRSTRLEN 16
#define BUFFER_SIZE 1024

using namespace std;

char* generate_message(int message_length, int sequence_number)		//pktsize, num_index
{
    char* message = (char*)malloc(message_length*sizeof(char));
    sprintf(message,"%d",sequence_number+1);
        //puts(message);
    int flag = 0;
    for(int i = 0; i<message_length;i++)
    {
        if(message[i] == 0)
        {
            //printf("yes\n");
            if(flag == 0)
            {
                message[i] = '#';
                flag = 1;
                //continue;
            }
            
            else if(flag == 1)
            {
                message[i] = '0';
            }
        }
    }        
    message[message_length-1] = 0;
    return message;
}

char* got_sequence_number(char* message)
{
    char* seq_num = (char*)malloc(sizeof(message)*sizeof(char));
    for(int i = 0; i<sizeof(message); i++)
    {
        if(message[i]!='#')
        {
            seq_num[i] = message[i];
        }
        else
        {
            break;
        }
    }
    return seq_num;
}
enum { NS_PER_SECOND = 1000000000 };

void sub_timespec(struct timespec t1, struct timespec t2, struct timespec *td)
{
    td->tv_nsec = t2.tv_nsec - t1.tv_nsec;
    td->tv_sec  = t2.tv_sec - t1.tv_sec;
    if (td->tv_sec > 0 && td->tv_nsec < 0)
    {
        td->tv_nsec += NS_PER_SECOND;
        td->tv_sec--;
    }
    else if (td->tv_sec < 0 && td->tv_nsec > 0)
    {
        td->tv_nsec -= NS_PER_SECOND;
        td->tv_sec++;
    }
}

double calculate_average_value(double* timelist, int pack_num)
{
    double average = 0;
    for(int i = 0; i<sizeof(timelist)/sizeof(double); i++)
    {
        average += timelist[i];
    }
    average = average/pack_num;

    return average;
}

double sum_value(double* timelist)
{
    double sum = 0;
    for(int i = 0; i<sizeof(timelist)/sizeof(double); i++)
    {
        sum += timelist[i];
    }

    return sum;
}


// double getCPUUtilization() {
//     std::ifstream statFile("/proc/stat");
//     std::string line;

//     // Read the first line of the stat file
//     std::getline(statFile, line);

//     // Extract CPU utilization values from the line
//     std::istringstream iss(line);
//     std::string cpuLabel;
//     long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
//     iss >> cpuLabel >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >> guest_nice;

//     // Calculate total CPU time
//     long totalCpuTime = user + nice + system + idle + iowait + irq + softirq + steal;
  
//     // Calculate CPU utilization as a percentage
//     double cpuUtilization = static_cast<double>(totalCpuTime - idle) / totalCpuTime * 100.0;

//     return cpuUtilization;
// }

struct s_info{             //定义一个结构体, 将地址结构跟cfd捆绑  
    struct sockaddr_in cliaddr;
    int stat_disp;
    int connfd;
    int mode;   //send(0) or recv(1)
    int proto;  //udp(0) or tcp(1)
    int pktnum;
    int pktrate;
    int pktsize;
    int connect_num;    //current connected client id (start from 0)

    int current_pool_size;
    int thread_in_use;

    int num_tcp_clients;
    int num_udp_clients;
};


typedef struct {
    void *(*function)(void *);          /* 函数指针，回调函数 */
    void *arg;                          /* 上面函数的参数 */
} threadpool_task_t;                    /* 各子线程任务结构体 */

/* 描述线程池相关信息 */

struct threadpool_t {
    pthread_mutex_t lock;               /* 用于锁住本结构体 */    
    pthread_mutex_t thread_counter;     /* 记录忙状态线程个数de琐 -- busy_thr_num */

    pthread_cond_t queue_not_full;      /* 当任务队列满时，添加任务的线程阻塞，等待此条件变量 */
    pthread_cond_t queue_not_empty;     /* 任务队列里不为空时，通知等待任务的线程 */

    pthread_t *threads;                 /* 存放线程池中每个线程的tid。数组 */
    pthread_t adjust_tid;               /* 存管理线程tid */
    threadpool_task_t *task_queue;      /* 任务队列(数组首地址) */

    int min_thr_num;                    /* 线程池最小线程数 */
    int max_thr_num;                    /* 线程池最大线程数 */
    int live_thr_num;                   /* 当前存活线程个数 */
    int busy_thr_num;                   /* 忙状态线程个数 */
    int wait_exit_thr_num;              /* 要销毁的线程个数 */

    int queue_front;                    /* task_queue队头下标 */
    int queue_rear;                     /* task_queue队尾下标 */
    int queue_size;                     /* task_queue队中实际任务数 */
    int queue_max_size;                 /* task_queue队列可容纳任务数上限 */

    int shutdown;                       /* 标志位，线程池使用状态，true或false */

    int Sleep;

    int udp_num;
    int tcp_num;
};


string para_mode;
int stat_disp = 500;                //display statistics every 500ms (default)
string send_host = "localhost";     //send data to host specified by hostname (localhost default)
int send_port_num = 4180;           //default port number
int recv_port_num = 4180;
string protocol_name = "UDP";       //default protocol
int total_msg_num = 0;                      //send a total of num messages, 0 for infinite. For UDP: Number of message  = Number of packet
int pool_size = 8;

long send_buf_size = 65536;      //Sender ongoing socket buffer size: in Bytes
long recv_buf_size = 65535;

string congestion_control;

// TCP thread
void *tcp_do_work(void *arg)
{
    struct s_info *ts = (struct s_info*)arg; //convert the input parameters from main thread
    char str[INET_ADDRSTRLEN];                  //#define INET_ADDRSTRLEN 16
    
    cout<<ts->mode << ts->proto<<endl;
    
    if(ts->mode == 0 && ts->proto == 1)     //client mode = 0 = send, so server will recv; client proto = 1 = tcp, server will tcp
    {
        cout<<"Receiver (Server) Side Receiving TCP"<<endl;
        //cout<<"Listen Connection..."<<endl;
        
        printf("ts->mode == 0 && ts->proto == 1\n");
        
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(struct sockaddr_in);

        int message_recv = 0;
        int total_messae_recv = 0;
        //double loss_rate = 0;
        int bytes_recv = 0;
        double cum_time_cost = 0;

        double sum_J_i_list = 0;
        //ssize_t ret;
        while(1){
            struct timespec start, finish, delta;
            clock_gettime(CLOCK_REALTIME, &start);
            char buf[ts->pktsize*8];
            //read(newconfd, buf, sizeof(buf));
            ssize_t ret;
            ret = recvfrom(ts->connfd,buf,sizeof(buf),0,(struct sockaddr*)&client_addr, &len);
            if (ret < 0) {
                perror("Failed to receive data");
                exit(EXIT_FAILURE);
            }
            else if (ret == 0)
            {
                
			    break;
            }
            else
            {
                bytes_recv = bytes_recv + ret;
            }
            //printf("read: %s\n",buf);

            clock_gettime(CLOCK_REALTIME, &finish);
            sub_timespec(start, finish, &delta);
            double time_cost = (double)delta.tv_sec + ((double)delta.tv_nsec)/1000000000;

            message_recv +=1;
            total_messae_recv+=1;
            //printf("seq num is: %s\n",seq_num);

            bzero(buf,sizeof(buf));

            double throughput = (bytes_recv*8)/(time_cost*1000000);
            //printf("Rate is %.2f Mbps\n",throughput);

            cum_time_cost = cum_time_cost + time_cost;
            bytes_recv = 0;

            if(cum_time_cost*1000 >= double(stat_disp))
            {
                double Jitter = sum_J_i_list/message_recv;
                sum_J_i_list = 0;
                //loss_rate = (atoi(seq_num) == total_messae_recv)?  0 : 100*(atoi(seq_num)-total_messae_recv)/double(atoi(seq_num));
                // printf("Receiver: Connected id [%d]\n",ts->connect_num);
                //printf("Receiver: Connected id [%d], [Elapsed] %.2f ms, [Pkts] %d, [Rate] %.2f Mbps, [Jitter] %.6f ms, [CPU] %.2f%% \n",ts->connect_num,cum_time_cost*1000, message_recv,throughput, Jitter,cpuUtilization);
                
                cum_time_cost = 0;
                message_recv = 0;
                bytes_recv = 0;
                
            }

        }
        //关闭套接字
        close(ts->connfd);
    }

    //return NULL;	//pthread_exit(0)效果一样
}

// TCP thread
void *tcp_do_work2(void *arg)
{
    struct s_info *ts = (struct s_info*)arg; //convert the input parameters from main thread
    char str[INET_ADDRSTRLEN];                  //#define INET_ADDRSTRLEN 16
    
    cout<<ts->mode << ts->proto<<endl;

    if(ts->mode == 1 && ts->proto == 1)
    {
        cout<<"Server side send TCP"<<endl;
        //读写数据
        double single_iter_pkt_threshold = ts->pktrate*((double)stat_disp/1000);     // in bytes/sec
        //cout<<"single_iter_pkt_threshold "<<single_iter_pkt_threshold<<endl;
        
        int a = 0;      ////p_num_index
        int cum_bytes_sent = 0;
        int cum_packet_number = 0;
        double cum_send_time_cost = 0.0;

        
        while(a<ts->pktnum || ts->pktnum == 0)
        {
            char buf[100];
            int len = read(ts->connfd,buf,sizeof(buf));     //first check if recv "notfinish" from client so that server could send data
                
            if(len == 0)
            {
                close(ts->connfd);
                break;
            }
            else if(len>0)
            {
                //cout<<"recv_buf: "<<buf<<endl;
                struct timespec start, finish, delta;
                struct timespec start1, finish1, delta1;
                clock_gettime(CLOCK_REALTIME, &start);
                clock_gettime(CLOCK_REALTIME, &start1);
                if(cum_bytes_sent<=single_iter_pkt_threshold || single_iter_pkt_threshold == 0)
                {
                    
                    char* message = generate_message(ts->pktsize*8,a);
                    //cout<<message<<endl;
                    char my_message[ts->pktsize*8];
                    strcpy(my_message,message);
                    //printf("sizeof message: %ld\n",sizeof(my_message));

                    int sendto_flag = 1;
                    while(sendto_flag)
                    {
                        ssize_t ret;
                        ret = sendto(ts->connfd,my_message,sizeof(my_message),0,(struct sockaddr *)&ts->cliaddr,sizeof(ts->cliaddr));
                        //ret = write(ts->connfd,my_message,sizeof(my_message));
                        //cout<<"haha"<<endl;
                        if(ret != -1)
                        {
                            sendto_flag = 0;
                        }
                        //sleep(1);
                    }
                    //free(message);
                    a++;
                    cum_packet_number++;
                    cum_bytes_sent = cum_bytes_sent+ts->pktsize;
                }
                clock_gettime(CLOCK_REALTIME, &finish);
                sub_timespec(start, finish, &delta);
                double time_cost = (double)delta.tv_sec + ((double)delta.tv_nsec)/1000000000;
                
                double res = double(double(ts->pktsize)/double(ts->pktrate));
                //printf("res is: %f\n",res);
                double interval = res-time_cost;
                usleep(fabs(interval)*1000000);

                clock_gettime(CLOCK_REALTIME, &finish1);
                sub_timespec(start1, finish1, &delta1);
                double tot_time_cost = (double)delta1.tv_sec + ((double)delta1.tv_nsec)/1000000000;
                cum_send_time_cost = cum_send_time_cost + tot_time_cost;
                //cout<<cum_send_time_cost<<endl;
                
                //cum_send_time_cost = cum_send_time_cost + time_cost;
                //printf("cum_send_time_cost is: %f\n",cum_send_time_cost);
                //break;
                
                if(cum_send_time_cost*1000 >= double(stat_disp))
                {
                    //cout<<cum_send_time_cost*1000<<endl;
                    //cout<<double(stat_disp)<<endl;

                    //printf("Sender: Connected id [%d]\n",ts->connect_num);
                    // printf("Receiver: Connected id [%d]\n",ts->connect_num);
                    cum_send_time_cost = 0;
                    cum_packet_number = 0;
                }
                cum_bytes_sent = 0;
            }
            
        }
        // //关闭套接字
        // close(ts->connfd);

        // char buf[100] = "hahaha";
        // char str[100];

        // //打印客户端的信息
        // printf("======the connect client ip ,port\n");
        // while(1)
        // {
        //     int len = read(ts->connfd,buf,sizeof(buf));
            
        //     if(len == 0)
        //     {
        //         close(ts->connfd);
        //         break;
        //     }
        //     else if(len>0)
        //     {
        //         for(int i = 0;i<len;i++)
        //             buf[i] = toupper(buf[i]);
        //         write(ts->connfd,buf,len);
        //     }
        //     // for(int i = 0;i<strlen(buf);i++)
        //     //     buf[i] = toupper(buf[i]);
        //     // write(ts->connfd,buf,sizeof(buf));
        // }
        
    }
    
    return NULL;	//pthread_exit(0)效果一样
}

// non-persistent response mode, also using TCP
void *response_work2(void *arg)
{
    struct s_info *ts = (struct s_info*)arg; //convert the input parameters from main thread
    char str[INET_ADDRSTRLEN];                  //#define INET_ADDRSTRLEN 16
    
    cout<<ts->mode <<endl;
    cout<<"Server side non-persistent response mode"<<endl;

    int cum_packet_number = 0;
    while(1)
    {
        char buf[100];
        int len = read(ts->connfd,buf,sizeof(buf));     //first check if recv "request" from client so that server could send data
        bzero(buf,sizeof(buf));    
        if(len == 0)
        {
            
            break;
        }
        else if(len>0)
        {
            struct timespec start, finish, delta;
            clock_gettime(CLOCK_REALTIME, &start);
            
            char message[100]="response";
            ssize_t ret;
            //ret = sendto(ts->connfd,my_message,sizeof(my_message),0,(struct sockaddr *)&ts->cliaddr,sizeof(ts->cliaddr));
            ret = write(ts->connfd,message,sizeof(message));
            if (ret < 0)
            {
                perror("write");
                exit(-1);
            }
            cum_packet_number++;
            clock_gettime(CLOCK_REALTIME, &finish);
            sub_timespec(start, finish, &delta);
            double time_cost = (double)delta.tv_sec + ((double)delta.tv_nsec)/1000000000;
        }
    }
    close(ts->connfd);
}

// UDP thread (server receive UDP)
void *udp_do_work(void *arg)
{
    struct s_info *ts = (struct s_info*)arg; //convert the input parameters from main thread
    char str[INET_ADDRSTRLEN];                  //#define INET_ADDRSTRLEN 16

    // cout<<"pthread mode: "<<ts->mode<<endl;
    // cout<<"pthread proto: "<<ts->proto<<endl;
    // cout<<"pthread connfd: "<<ts->connfd<<endl;
    // cout<<"pthread pktnum: "<<ts->pktnum<<endl;
    // cout<<"pthread pktrate: "<<ts->pktrate<<endl;
    // cout<<"pthread pktsize: "<<ts->pktsize<<endl;
    
    //server receive UDP
    if(ts->mode == 0 && ts->proto == 0)
    {
        cout<<"Server Side Receiving UDP: "<<endl;
        cout<<"Listen Connection..."<<endl;
        int sockfd = socket(AF_INET, SOCK_DGRAM,0);
        if(sockfd < 0)
        {
            perror("socket");
            exit(-1);
        }
        //printf("sockfd %d\n",sockfd);

        //set socket buffer size
        long set_socket_buf = setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char *)(&recv_buf_size), sizeof(recv_buf_size));
        if(set_socket_buf < 0)
        {
            perror("set socket buf");
            //cout<<"aaa"<<endl;
        }
        
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        int port_num = send_port_num+ts->connfd;
        //cout<<"server receive UDP port"<<port_num<<endl;
        server_addr.sin_port = htons(port_num);
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);	//根据不同server地址需要修改
        
        int my_bind = bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
        if(my_bind < 0)
        {
            perror("bind");
            close(sockfd);
            exit(-1);
        }
        //读写数据
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(struct sockaddr_in);	
        ssize_t ret;

        int message_recv = 0;
        int total_messae_recv = 0;
        double loss_rate = 0;
        int loss_num = 0;
        int bytes_recv = 0;
        double cum_time_cost = 0;

        int Jitter_max = ts->pktsize;
        // double *inter_arrival_list = (double*)malloc(Jitter_max*sizeof(double));
        // double *J_i_list = (double*)malloc(Jitter_max*sizeof(double));
        double inter_arrival_list[Jitter_max*sizeof(double)];
        double J_i_list[Jitter_max*sizeof(double)];

        double sum_J_i_list = 0;
        
        while(1)
        {
            struct timespec start, finish, delta;
            clock_gettime(CLOCK_REALTIME, &start);
            //sleep(1);

            //char buf[bsize] = "\0";
            //recv_bsize = recv_bsize;
            char buf[ts->pktsize*8];
            
            int ret = recvfrom(sockfd, buf, sizeof(buf),0,(struct sockaddr*)&client_addr, &len);             //ts->connfd == newconfd
            if(ret == 0)
            {
                printf("the client %d closed...\n", ts->connfd);
                break;          //跳出循环,关闭cfd
            }
            // printf("received from %s at PORT %d\n",
            //         inet_ntop(AF_INET, &(*ts).cliaddr.sin_addr, str, sizeof(str)),
            //         ntohs((*ts).cliaddr.sin_port));         //打印客户端信息(IP/PORT)
            //printf("received: %s\n",buf);
            else
            {
                //cout<<buf<<endl;
                bytes_recv = bytes_recv + ret;  //in bytes
            }
            clock_gettime(CLOCK_REALTIME, &finish);
            sub_timespec(start, finish, &delta);
            double time_cost = (double)delta.tv_sec + ((double)delta.tv_nsec)/1000000000;
            //printf("The time receiving data is: %f\n",time_cost);

            //calculate for jitter
            inter_arrival_list[message_recv] = time_cost*1000;

            message_recv +=1;
            total_messae_recv+=1;
            //printf("seq num is: %s\n",seq_num);

            //calculate for jitter
            double D = calculate_average_value(inter_arrival_list,message_recv);
            //printf("D is %f, time_cost is %f\n",D,time_cost*1000);
            
            J_i_list[message_recv-1] = time_cost*1000 - D;
            //printf("J_i_list is %f\n",J_i_list[message_recv-1]);
            sum_J_i_list += J_i_list[message_recv-1];
            
            //printf("message recv: %d\n",message_recv);

            char *seq_num = got_sequence_number(buf);
            char udp_seq_num[ts->pktsize*8];
            strcpy(udp_seq_num,seq_num);
            free(seq_num);

            //cout<<seq_num<<endl;
            
            bzero(buf,sizeof(buf));
            
            double throughput = (bytes_recv*8)/(time_cost*1000000);
            //printf("Rate is %.2f Mbps\n",throughput);

            cum_time_cost = cum_time_cost + time_cost;
            //printf("cum time cost: %f\n",cum_time_cost);
            bytes_recv = 0;

            //std::cout << "CPU utilization: " << cpuUtilization << "%" << std::endl;

            if(cum_time_cost*1000 >= double(stat_disp))
            {
                double Jitter = sum_J_i_list/message_recv;
                sum_J_i_list = 0;
                loss_num = atoi(udp_seq_num)-total_messae_recv;
                loss_rate = (atoi(udp_seq_num) == total_messae_recv)?  0 : double(100*(atoi(udp_seq_num)-total_messae_recv))/double(atoi(udp_seq_num));
                
                printf("Receiver: Connected id [%d], Elapsed [%.2f ms], Pkts [%d], Rate [%.2f Mbps], Jitter [%.6f ms], Loss [%d, %f%%]\n",ts->connect_num,cum_time_cost*1000, message_recv,throughput, Jitter, loss_num,loss_rate);
                //printf("CPU占用率:%f\n",cpu);
                
                cum_time_cost = 0;
                message_recv = 0;
                //loss_num = 0;
                
            }
            //free(seq_num);
            loss_rate = 0;
            loss_num = 0;



            //if client is closed, client will send "finish" to server, if server recv "finish", it will close that thread
            char finish_buf[100];
            len = read(ts->connfd,finish_buf,sizeof(finish_buf));
            if(len == 0)
            {
                //printf("finished\n");
                break;
            }
            cout<<finish_buf<<endl;
        }
        close(sockfd);
        
        close(ts->connfd);
        
    }
}

void sendResponse(int clientSocket, const char* response) {
    if (send(clientSocket, response, strlen(response), 0) < 0) {
        perror("Failed to send response");
        exit(1);
    }
}

void processRequest(int clientSocket) {
    char request[BUFFER_SIZE];
    char* filePath;
    FILE* file;
    char response[BUFFER_SIZE];
    memset(request, 0, sizeof(request));

    // Receive the request from the client
    if (recv(clientSocket, request, BUFFER_SIZE - 1, 0) < 0) {
        perror("Failed to receive request");
        exit(1);
    }

    // Extract the file path from the request
    strtok(request, " ");
    filePath = strtok(NULL, " ");
    if (filePath[0] == '/')
        filePath++;

    // Open the requested file
    file = fopen(filePath, "r");
    if (file == NULL) {
        // File not found, send 404 response
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n"
                                        "Content-Length: 0\r\n"
                                        "Connection: close\r\n"
                                        "\r\n");
        sendResponse(clientSocket, response);
        return;
    }

    // Read and send the file content as the response

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    memset(response, 0, sizeof(response));
    snprintf(response, BUFFER_SIZE, "HTTP/1.1 200 OK\r\n"
                                    "Content-Length: %ld\r\n"
                                    "Connection: close\r\n"
                                    "\r\n",
            file_size);
    sendResponse(clientSocket, response);

    // fseek(file, 0, SEEK_SET);
    // memset(response, 0, sizeof(response));
    // while (fgets(response, BUFFER_SIZE, file) != NULL) {
    //     sendResponse(clientSocket, response);
    //     memset(response, 0, sizeof(response));
    // }
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(clientSocket, buffer, bytes_read, 0);
    }

    // Close the file
    fclose(file);
}

void *http_work(void *arg)
{
    struct s_info *ts = (struct s_info*)arg; //convert the input parameters from main thread
    int serverSocket, clientSocket;
    struct sockaddr_in serverAddr, clientAddr;
    socklen_t clientAddrLen;

    // Create socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        perror("Failed to create socket");
        exit(1);
    }

    // Set server address information
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(8080);

    // Bind the socket to the server address
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Failed to bind socket");
        exit(1);
    }

    // Listen for incoming connections
    if (listen(serverSocket, 5) < 0) {
        perror("Failed to listen for connections");
        exit(1);
    }

    printf("Server listening on port %d\n", 8080);

    //while (1) {
        // Accept a client connection
        clientAddrLen = sizeof(clientAddr);
        clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket < 0) {
            perror("Failed to accept client connection");
            exit(1);
        }

        printf("Client connected: %s:%d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));

        // Process the client's request
        processRequest(clientSocket);

        // Close the client socket
        close(clientSocket);
        printf("Client disconnected\n");
    //}

    // Close the server socket
    close(serverSocket);
    close(ts->connfd);
}

void *http_work2(void *arg)
{
    struct s_info *ts = (struct s_info*)arg; //convert the input parameters from main thread
    // Create socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0)
    {
        perror("Failed to create socket");
        exit(1);
    }

    // Set server address information
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(4433);

    // Bind socket to the server address
    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("Failed to bind socket");
        exit(1);
    }

    // Listen for incoming connections
    if (listen(serverSocket, 5) < 0)
    {
        perror("Failed to listen for connections");
        exit(1);
    }

    //printf("Server listening on port %d\n", SERVER_PORT);

    // Accept incoming connections
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
    if (clientSocket < 0)
    {
        perror("Failed to accept connection");
        exit(1);
    }

    // Initialize SSL
    SSL_library_init();
    // SSL_CTX* sslContext = SSL_CTX_new(TLS_server_method());
    SSL_CTX *sslContext = SSL_CTX_new(SSLv23_server_method());
    SSL *ssl = SSL_new(sslContext);
    SSL_set_fd(ssl, clientSocket);

    // Load the server certificate and private key
    if (SSL_use_certificate_file(ssl, "domain.crt", SSL_FILETYPE_PEM) <= 0)
    {
        fprintf(stderr, "Failed to load server certificate\n");
        exit(1);
    }
    if (SSL_use_PrivateKey_file(ssl, "domain.key", SSL_FILETYPE_PEM) <= 0)
    {
        fprintf(stderr, "Failed to load server private key\n");
        exit(1);
    }

    // Enable client certificate verification
    SSL_CTX_set_verify(sslContext, SSL_VERIFY_PEER, NULL);
    SSL_CTX_load_verify_locations(sslContext, "rootCA.crt", NULL);
    // Set the server certificate verification callback
    SSL_CTX_set_verify_depth(sslContext, 4);
    SSL_CTX_set_cert_verify_callback(sslContext, NULL, NULL);

    // Perform SSL handshake
    if (SSL_accept(ssl) <= 0)
    {
        fprintf(stderr, "Failed to perform SSL handshake\n");
        exit(1);
    }

    // Perform data exchange over the SSL/TLS connection
    printf("Client connected: %s:%d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));

    // Process the client's request
    processRequest(clientSocket);

    // Close the client socket
    close(clientSocket);
    printf("Client disconnected\n");

    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(sslContext);
    //close(clientSocket);
    close(serverSocket);
    close(ts->connfd);
}


// UDP thread (server send UDP)
void *udp_do_work2(void *arg)
{
    struct s_info *ts = (struct s_info*)arg; //convert the input parameters from main thread
    char str[INET_ADDRSTRLEN];                  //#define INET_ADDRSTRLEN 16

    //server send UDP
    if(ts->mode == 1 && ts->proto == 0)
    {
        cout<<"server send UDP"<<endl;
        int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);

        // //set socket buffer size
        // long set_socket_buf = setsockopt(udp_sock, SOL_SOCKET, SO_SNDBUF, (char *)(&send_buf_size), sizeof(send_buf_size));
        // if(set_socket_buf < 0)
        // {
        //     perror("set socket buf");
        //     //cout<<"aaa"<<endl;
        // }

        //向服务器（特定的IP和端口）发起请求
        struct hostent *pHost = NULL;
        string my_send_host;
        pHost = gethostbyname(send_host.c_str());
        if (pHost != NULL) {
            my_send_host = inet_ntoa(*((struct in_addr *)(pHost->h_addr_list[0])));
            cout<<my_send_host<<endl;
        }

        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));  //每个字节都用0填充
        serv_addr.sin_family = AF_INET;  //使用IPv4地址
        serv_addr.sin_addr.s_addr = inet_addr(my_send_host.c_str());  //具体的IP地址
        int port_num = send_port_num+ts->connfd;
        // cout<<"port_num: "<<port_num<<endl;
        serv_addr.sin_port = htons(port_num);  //端口

        //set socket buffer size
        long set_socket_buf = setsockopt(udp_sock, SOL_SOCKET, SO_SNDBUF, (char *)(&send_buf_size), sizeof(send_buf_size));
        if(set_socket_buf < 0)
        {
            perror("set socket buf");
            //cout<<"aaa"<<endl;
        }

        //读写数据
        double single_iter_pkt_threshold = ts->pktrate*((double)stat_disp/1000);     // in bytes/sec
        // cout<<"single_iter_pkt_threshold "<<single_iter_pkt_threshold<<endl;
        //E.g., stat = 1 s, pktrate = 10000 Bytes. Then each 1s, we totally allow 10 MB data to send (as “single_iter_pkt_threshold”)

        ssize_t ret;
        int a = 0;      //p_num_index
        int cum_bytes_sent = 0;
        int cum_packet_number = 0;
        double cum_send_time_cost = 0;

        // cout<<"ts->pktnum: "<<ts->pktnum<<endl;
        while(a<ts->pktnum || ts->pktnum == 0)      //while(p_num_index<para_pktnum || para_pktnum == 0)
        {
            struct timespec start, finish, delta;
            struct timespec start1, finish1, delta1;
            clock_gettime(CLOCK_REALTIME, &start1);
            clock_gettime(CLOCK_REALTIME, &start);
            if(cum_bytes_sent<=single_iter_pkt_threshold || single_iter_pkt_threshold == 0)
            {
                
                char* message = generate_message(ts->pktsize*8,a);
                // cout<<"ts->pktsize*8: "<<ts->pktsize*8<<endl;
                char my_message[ts->pktsize*8];
                strcpy(my_message,message);
                //printf("sizeof message: %ld\n",sizeof(my_message));

                int sendto_flag = 1;
                while(sendto_flag)
                {
                    ret = sendto(udp_sock,my_message,sizeof(my_message),0,(struct sockaddr*)&serv_addr, sizeof(serv_addr));
                    if(ret != -1)
                    {
                        sendto_flag = 0;
                    }
                    if(ret < 0)
                    {
                        if(errno == EINTR ||errno == EAGAIN ||errno == EWOULDBLOCK)
                            continue;
                        //printf("wenti\n");
                        printf("%s", strerror(errno));
                        return (void *)-1;	//pthread_exit(0)效果一样;
                    }
                }
                a++;
                cum_packet_number++;
                
                cum_bytes_sent = cum_bytes_sent+ts->pktsize;  //in bytes
            }
            clock_gettime(CLOCK_REALTIME, &finish);
            sub_timespec(start, finish, &delta);
            double time_cost = (double)delta.tv_sec + ((double)delta.tv_nsec)/1000000000;
            
            double res = double(double(ts->pktsize)/double(ts->pktrate));
            //printf("res is: %f\n",res);
            double interval = res-time_cost;
            usleep(fabs(interval)*1000000);
            
            //time_cost = 0;
            clock_gettime(CLOCK_REALTIME, &finish1);
            sub_timespec(start1, finish1, &delta1);
            double tot_time_cost = (double)delta1.tv_sec + ((double)delta1.tv_nsec)/1000000000;
            cum_send_time_cost = cum_send_time_cost + tot_time_cost;
            
            if(cum_send_time_cost*1000 >= double(stat_disp))
            {
                // printf("Sender: [Elapsed] %.2f ms, [Pkts] %d, Rate %f Mbps\n",(cum_send_time_cost*1000), cum_packet_number, double(8*ts->pktrate)/double(1000000));
                cum_send_time_cost = 0;
                cum_packet_number = 0;
            }

            cum_bytes_sent = 0;


            //if client is closed, client will not send "notfinish" to server, if server no longer recv "notfinish", it will close that thread
            char finish_buf[100];
            //int len = read(ts->connfd,finish_buf,sizeof(finish_buf));
            int len = recvfrom(ts->connfd,finish_buf,sizeof(finish_buf),0,NULL, 0);
            if(len == 0)
            {
                //printf("finished\n");
                break;
            }
            // cout<<"finish_buf: "<<finish_buf<<endl;
        }
        close(udp_sock);
        close(ts->connfd);
    }
}

int main(int argc, char const *argv[])
{
        // Do the things for send/recv mode
    if (argc >= 1) {
        for (int i = 1; i < argc; i++) {
            if ((i + 1) < argc && argv[i][0] == '-') {
                // if (strcmp(argv[i], "-stat") == 0) {
                //     //para_stat = strtol(argv[i + 1], NULL, 10);
                //     stat_disp = strtod(argv[i+1],NULL);
                    
                // }
                if (strcmp(argv[i], "-lhost") == 0){
                    send_host = argv[i+1];
                }
                if (strcmp(argv[i], "-lport") == 0){
                    send_port_num = atoi(argv[i+1]);
                }

                if (strcmp(argv[i], "-sbufsize") == 0){
                    send_buf_size = strtol(argv[i+1], NULL, 10);
                }

                if (strcmp(argv[i], "-rbufsize") == 0){
                    recv_buf_size = strtol(argv[i+1], NULL, 10);
                }
                if (strcmp(argv[i], "-poolsize") == 0){
                    pool_size = atoi(argv[i+1]);
                }
                if (strcmp(argv[i], "-tcpcca") == 0){
                    congestion_control = argv[i+1];
                }
            }
        }
    }

    //创建套接字
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd < 0)
    {
        perror("socket");
        exit(-1);
    }

    //set socket buffer size
    long set_socket_buf = setsockopt(listenfd, SOL_SOCKET, SO_SNDBUF, (char *)(&recv_buf_size), sizeof(recv_buf_size));
    if(set_socket_buf < 0)
    {
        perror("set socket buf");
        //cout<<"aaa"<<endl;
    }


    // //set congestion control mode:
    if(!congestion_control.empty())
    {
        socklen_t len = sizeof(congestion_control);
        if (setsockopt(listenfd, IPPROTO_TCP, TCP_CONGESTION, (void*)congestion_control.c_str(), len) != 0)
        {
            perror("setsockopt");
            return -1;
        }
    }
    
    
    char cong_name[100];
    socklen_t len = sizeof(cong_name);
    if (getsockopt(listenfd, IPPROTO_TCP, TCP_CONGESTION, cong_name, &len) != 0)
    {
        perror("getsockopt");
        return -1;
    }

    //print out overall parameters:
    cout<<"Parameters:"<<endl;
    cout<<"-lhost (Receiver host name/IP): "<<send_host<<endl;
    cout<<"-lport (Receiver port number): "<<send_port_num<<endl;
    cout<<"-sbufsize (Sender outgoing buffer, in Bytes): "<<send_buf_size<<endl;
    cout<<"-rbufsize (Sender outgoing buffer, in Bytes): "<<recv_buf_size<<endl;
    cout<<"-tcpcca (New Congestion mode): "<<cong_name<<endl;
    cout<<""<<endl;

    //绑定
    struct sockaddr_in self_addr;
    struct sockaddr_in cliaddr;     //record the client address info
    socklen_t cliaddr_len;

    self_addr.sin_family = AF_INET;
    self_addr.sin_port = htons(send_port_num);
    self_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    //int opt = 1;
    //setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&opt, sizeof(opt));

    int ret = bind(listenfd, (struct sockaddr*)&self_addr, sizeof(self_addr));
    if(ret < 0)
    {
        perror("bind");
        close(listenfd);
        exit(-1);
    }
    //监听，设置最大连接值
    int ret_listen = listen(listenfd, 128);
    if(ret_listen < 0)
    {
        perror("listen");
        close(listenfd);
        exit(-1);
    }
    else{
        printf("Accepting client connect ...\n");
    }

    struct s_info ts[256];						//创建结构体数组.
    pthread_t tid;

    printf("------------\n");
    printf("\n");
    threadpool_t *thp = threadpool_create(8,100,100);   /*创建线程池，池里最小/inital 8个线程，最大100，队列最大100*/
    printf("pool inited\n");
    printf("\n");
    printf("------------\n");
    
    //读写数据
    int i = 0;
    int total_connect_num;  //total connected client
    while(1)
    {
        //阻塞等待连接
        int newconfd = accept(listenfd, (struct sockaddr *)&cliaddr, &cliaddr_len);    //并取得对方地址信息（只能监听不能收数据）
        if(newconfd<0)
        {
            perror("accept");
            close(listenfd);
            exit(-1);
        }
        ts[i].cliaddr =cliaddr;
        ts[i].connfd = newconfd;

        //set socket buffer size
        long set_socket_buf = setsockopt(newconfd, SOL_SOCKET, SO_SNDBUF, (char *)(&recv_buf_size), sizeof(recv_buf_size));
        if(set_socket_buf < 0)
        {
            perror("set socket buf");
            //cout<<"aaa"<<endl;
        }

        //set congestion control mode:
        // socklen_t len = sizeof(congestion_control);
        // if (setsockopt(newconfd, IPPROTO_TCP, TCP_CONGESTION, (void*)congestion_control.c_str(), len) != 0)
        // {
        //     perror("setsockopt");
        //     return -1;
        // }
        
        ssize_t ret_send;	//client的大小
        //cout<<newconfd<<endl;

        //receive from client first to get the arguments
	    struct sockaddr_in client_addr;
	    socklen_t len = sizeof(client_addr);
        ssize_t ret_rcv;
        char recv_buf[100]= "\0";
        ret_rcv = recvfrom(newconfd, recv_buf, sizeof(recv_buf),0,(struct sockaddr*)&client_addr, &len);	//后两个可以直接写NULL
        if(ret_rcv < 0)
        {
            perror("recvfrom");
            close(newconfd);
            exit(-1);
        }
        printf("argument string is: %s\n",recv_buf);
        //printf("sizeof buf is: %ld\n",sizeof(recv_buf));

        
        //printf("mode is: %d\n",recv_buf[0] - '0');
        ts[i].mode = recv_buf[0] - '0';

        //printf("proto is: %d\n",recv_buf[1] - '0');
        ts[i].proto = recv_buf[1] - '0';

        char temp_sub_buff[11];
        memcpy(temp_sub_buff,&recv_buf[2],10);
        temp_sub_buff[10] = '\0';
        //printf("pktnum is: %d\n",atoi(temp_sub_buff));
        ts[i].pktnum = atoi(temp_sub_buff);
        
        memcpy(temp_sub_buff,&recv_buf[12],10);
        temp_sub_buff[10] = '\0';
        //printf("pktsize is: %d\n",atoi(temp_sub_buff));
        ts[i].pktsize = atoi(temp_sub_buff);
        
        memcpy(temp_sub_buff,&recv_buf[22],10);
        temp_sub_buff[10] = '\0';
        //printf("pktrate is: %d\n",atoi(temp_sub_buff));
        ts[i].pktrate = atoi(temp_sub_buff);

        //assign the connect id and total number of connected
        ts[i].connect_num = i;

        memcpy(temp_sub_buff,&recv_buf[32],10);
        temp_sub_buff[10] = '\0';
        ts[i].stat_disp = atoi(temp_sub_buff);

        //cout<<"ts[i].stat_disp  is: "<<ts[i].stat_disp <<endl;


        char str[INET_ADDRSTRLEN];
        string client_proto;
        if(ts[i].proto == 0)
        {
            client_proto = "UDP";
        }
        else
        {
            client_proto = "TCP";
        }


        string client_mode;
        
        if(ts[i].mode == 1)
        {
            client_mode = "SEND";
        }
        else if(ts[i].mode == 0)
        {
            client_mode = "RECV";
        }
        else if(ts[i].mode == 2)
        {
            client_mode = "RESP";
            client_proto = "TCP";
            ts[i].proto = 1;
        }
        else if(ts[i].mode == 3)
        {
            client_mode = "HTTP";
            client_proto = "TCP";
            //ts[i].proto = 1;
            printf("ts[i].proto is %d\n",ts[i].proto);
        }
        printf("Connected to %s port %d, [%d, %d] %s, %s, %d\n",inet_ntop(AF_INET, &(*ts).cliaddr.sin_addr, str, sizeof(str)), ntohs((*ts).cliaddr.sin_port), ts[i].connect_num, total_connect_num, client_mode.c_str(), client_proto.c_str(), ts[i].pktrate);

        //send to "newconfd" to client to mention the client start data transmition
        char buf[100];
        memset(buf,0,sizeof(buf));
        // char mode[2] = "1";
        sprintf(buf,"%d", ts[i].connfd);
        //struct sockaddr_in client_addr;
        //ssize_t ret_send;
        ret_send = sendto(newconfd, buf, sizeof(buf), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
		if(ret_send < 0)
		{
			perror("sendto");
			close(newconfd);
			exit(-1);
		}
        else if(ret_send == 0)
        {
            //printf("hahahah\n");
            close(newconfd);
        }
       
        if(ts[i].proto == 1 && ts[i].mode == 0)
        {
            //ts->num_tcp_clients +=1;
            threadpool_add(thp, tcp_do_work, (void*)&ts[i],ts[i].stat_disp);   /* 向线程池中添加任务 */
        }

        else if(ts[i].proto == 1 && ts[i].mode == 1)
        {
            //ts->num_tcp_clients +=1;
            threadpool_add(thp, tcp_do_work2, (void*)&ts[i],ts[i].stat_disp);   /* 向线程池中添加任务 */
        }

        else if(ts[i].mode == 2)
        {
            //ts->num_tcp_clients +=1;
            int one = 1;
            setsockopt(newconfd, SOL_TCP, TCP_NODELAY, &one, sizeof(one));
            //threadpool_add(thp, response_work, (void*)&ts[i],1);   /* 向线程池中添加任务 */
            char response_mode[100];
            int ret = read(newconfd,response_mode,sizeof(response_mode));
            if(ret < 0)
            {
                perror("read response mode");
                exit(-1);
            }
            else if(ret > 0)
            {
                //non-persistant, create a new TCP each time
                if(strcmp(response_mode, "false") == 0)
                {
                    printf("response_mode: false\n");
                    threadpool_add(thp, response_work2, (void*)&ts[i],ts[i].stat_disp);   /* 向线程池中添加任务 */
                }
                else if(strcmp(response_mode, "true") == 0)
                {
                    printf("response_mode: true\n");
                    threadpool_add(thp, response_work2, (void*)&ts[i],ts[i].stat_disp);    /* 向线程池中添加任务 */
                }
                else
                {
                    printf("something wrong with the non-persistent or persistent\n");
                }
            }
        }

        
        else if(ts[i].proto == 2 && ts[i].mode == 3)    //http
        {
            printf("hahah\n");
            threadpool_add(thp, http_work, (void*)&ts[i],ts[i].stat_disp);   /* 向线程池中添加任务 */
        }
        else if(ts[i].proto == 3 && ts[i].mode == 3)    //http
        {
            printf("xixiix\n");
            threadpool_add(thp, http_work2, (void*)&ts[i],ts[i].stat_disp);   /* 向线程池中添加任务 */
        }


        else if(ts[i].proto == 0 && ts[i].mode == 0)    //recv UDP
        {
            cout<<"ts[i].stat_disp: "<<ts[i].stat_disp<<endl;
            threadpool_add(thp, udp_do_work, (void*)&ts[i],ts[i].stat_disp);   /* 向线程池中添加任务 */
        }
        else if(ts[i].proto == 0 && ts[i].mode == 1)    //send UDP
        {
            cout<<"ts[i].stat_disp: "<<ts[i].stat_disp<<endl;
            threadpool_add(thp, udp_do_work2, (void*)&ts[i],ts[i].stat_disp);   /* 向线程池中添加任务 */
        }

        i++;
    }
    
    //关闭套接字
    close(listenfd);
    threadpool_destroy(thp);
    //close(newconfd);
    return 0;

}