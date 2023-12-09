// LinuxClient.cpp
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <iostream>
#include <sstream>
#include <errno.h>
#include "apue.h"
#include <cstring>
#include <sys/resource.h>
#include <fstream>
#include <math.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <algorithm>
#include <numeric> //用于accumulate函数
#include <vector>


#include <openssl/ssl.h>
#include <openssl/err.h>
#include <netdb.h>

using namespace std;

#define BUFFER_SIZE 1024

char *generate_message(int message_length, int sequence_number) // pktsize, num_index
{
    char *message = (char *)malloc(message_length * sizeof(char));
    sprintf(message, "%d", sequence_number + 1);
    // puts(message);
    int flag = 0;
    for (int i = 0; i < message_length; i++)
    {
        if (message[i] == 0)
        {
            // printf("yes\n");
            if (flag == 0)
            {
                message[i] = '#';
                flag = 1;
                // continue;
            }

            else if (flag == 1)
            {
                message[i] = '0';
            }
        }
    }
    message[message_length - 1] = 0;
    return message;
}

char *got_sequence_number(char *message)
{
    char *seq_num = (char *)malloc(sizeof(message) * sizeof(char));
    for (int i = 0; i < sizeof(message); i++)
    {
        if (message[i] != '#')
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
enum
{
    NS_PER_SECOND = 1000000000
};

void sub_timespec(struct timespec t1, struct timespec t2, struct timespec *td)
{
    td->tv_nsec = t2.tv_nsec - t1.tv_nsec;
    td->tv_sec = t2.tv_sec - t1.tv_sec;
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

double calculate_average_value(double *timelist, int pack_num)
{
    double average = 0;
    for (int i = 0; i < sizeof(timelist) / sizeof(double); i++)
    {
        average += timelist[i];
    }
    average = average / pack_num;

    return average;
}

double sum_value(double *timelist)
{
    double sum = 0;
    for (int i = 0; i < sizeof(timelist) / sizeof(double); i++)
    {
        sum += timelist[i];
    }

    return sum;
}

double average(double a[], int n)
{
    // Find sum of array element
    double sum = 0;
    for (int i = 0; i < n; i++)
        sum += a[i];

    return (double)sum / n;
}

double cal_max_value(double *timelist, int n)
{
    int count;
    double highest;
    highest = timelist[0];
    for (count = 1; count < n; count++)
    {
        if (timelist[count] > highest)
            highest = timelist[count];
    }
    return highest;
}

double cal_min_value(double *timelist, int n)
{
    int count;
    double lowest;
    lowest = timelist[0];
    for (count = 1; count < n; count++)
    {
        if (timelist[count] < lowest && timelist[count] > 0)
            lowest = timelist[count];
    }
    return lowest;
}

double getCPUUtilization()
{
    std::ifstream statFile("/proc/stat");
    std::string line;

    // Read the first line of the stat file
    std::getline(statFile, line);

    // Extract CPU utilization values from the line
    std::istringstream iss(line);
    std::string cpuLabel;
    long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    iss >> cpuLabel >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >> guest_nice;

    // Calculate total CPU time
    long totalCpuTime = user + nice + system + idle + iowait + irq + softirq + steal;

    // Calculate CPU utilization as a percentage
    double cpuUtilization = static_cast<double>(totalCpuTime - idle) / totalCpuTime * 100.0;

    return cpuUtilization;
}

string para_mode;
int stat_disp = 500;            // display statistics every 500ms (default)
string send_host = "localhost"; // send data to host specified by hostname (localhost default)
int send_port_num = 4180;       // default port number
int recv_port_num = 4180;
string protocol_name = "UDP"; // default protocol
int bsize = 1000;             // send message of xxx bytes(including header), 1000 bytes default
int txrate = 1000;            // send data at a data rate of xxx bytes/sec
int total_msg_num = 0;        // send a total of num messages, 0 for infinite. For UDP: Number of message  = Number of packet
// int bsize;                          // set the outgoing socket buffer size to bsize bytes.
int recv_stat_disp = 500; // receiver display statistics every 500ms (default)
int recv_bsize = 1000;    // recive message of xxx bytes(including header), 1000 bytes default
// string recv_host = INADDR_ANY;

long send_buf_size = 65536; // Sender ongoing socket buffer size: in Bytes
long recv_buf_size = 65535;

bool if_persistent = false; // default is false means everytime we use a new TCP for -response

string my_url;
string file_name;

struct DOKI_packet
{
    unsigned short mode;  // 0 --> send, 1 --> recv
    unsigned short proto; // 0 --> udp, 1 --> tcp
    int pktsize;
    int pktnum;
    int pktrate;
    int state;
};

string DOKI_int_to_string(int value)
{
    ostringstream ss;
    ss << value;
    string new_string(ss.str());
    return new_string;
}

string DOKI_add_zero(string old_string, int n_zero, char ch = '0')
{
    string new_string = string(n_zero - old_string.length(), ch) + old_string;
    return new_string;
}

string parameter_packet_string_repr(struct DOKI_packet *parameter_packet)
{
    string mode = DOKI_int_to_string(parameter_packet->mode);
    string proto = DOKI_int_to_string(parameter_packet->proto);
    string pktnum = DOKI_add_zero(DOKI_int_to_string(parameter_packet->pktnum), 10, '0');
    string pktsize = DOKI_add_zero(DOKI_int_to_string(parameter_packet->pktsize), 10, '0');
    string pktrate = DOKI_add_zero(DOKI_int_to_string(parameter_packet->pktrate), 10, '0');
    string state = DOKI_add_zero(DOKI_int_to_string(parameter_packet->state), 10, '0');
    // cout<<mode<<endl;
    // cout<<proto<<endl;
    // cout<<pktnum<<endl;
    // cout<<pktsize<<endl;
    // cout<<pktrate<<endl;

    string string_repr = mode + proto + pktnum + pktsize + pktrate + state;
    // cout<<string_repr<<endl;
    return string_repr;
}

int main(int argc, char const *argv[])
{

    /* code */
    // cout<<"The argc is: "<<argc <<endl;

    DOKI_packet property;
    string connect_info;

    if (argc > 1)
    {
        para_mode = argv[1]; // send or recv or response
        // printf("Operation Mode: %s\n", para_mode.c_str());

        // Do the things for send/recv mode
        if (argc > 2)
        {
            for (int i = 2; i < argc; i++)
            {
                if ((i + 1) < argc && argv[i][0] == '-')
                {
                    if (strcmp(argv[i], "-stat") == 0)
                    {
                        // para_stat = strtol(argv[i + 1], NULL, 10);
                        stat_disp = strtod(argv[i + 1], NULL);
                    }
                    if (strcmp(argv[i], "-rhost") == 0)
                    {
                        send_host = argv[i + 1];
                    }
                    if (strcmp(argv[i], "-rport") == 0)
                    {
                        send_port_num = atoi(argv[i + 1]);
                    }

                    if (strcmp(argv[i], "-pktsize") == 0)
                    {
                        bsize = atoi(argv[i + 1]);
                    }
                    if (strcmp(argv[i], "-pktrate") == 0)
                    {
                        txrate = atoi(argv[i + 1]);
                    }
                    if (strcmp(argv[i], "-pktnum") == 0)
                    {
                        total_msg_num = atoi(argv[i + 1]);
                    }

                    if (strcmp(argv[i], "-proto") == 0)
                    {
                        protocol_name = argv[i + 1];
                    }

                    if (strcmp(argv[i], "-sbufsize") == 0)
                    {
                        send_buf_size = strtol(argv[i + 1], NULL, 10);
                    }

                    if (strcmp(argv[i], "-rbufsize") == 0)
                    {
                        recv_buf_size = strtol(argv[i + 1], NULL, 10);
                    }

                    if (strcmp(argv[i], "-persist") == 0)
                    {
                        string result = argv[i + 1];
                        if (result == "yes")
                        {
                            printf("yes\n");
                            if_persistent = true; // default is true means everytime we use the initial TCP for -response
                        }
                        else if (result == "no")
                        {
                            printf("no\n");
                            if_persistent = false;
                        }
                        else
                        {
                            printf("wrong input for -persist\n");
                            exit(-1);
                        }
                    }

                    if (strcmp(argv[i], "-url") == 0)
                    {
                        my_url = argv[i + 1];
                        // Get the protocol
                        char url[256];
                        strcpy(url, my_url.c_str());
                        char protocol[16] = "";
                        char *protocolEnd = strstr(url, "://");
                        if (protocolEnd != NULL)
                        {
                            strncpy(protocol, url, protocolEnd - url);
                        }
                        if(strcmp(protocol,"http")==0)
                        {
                            //printf("strcmp(protocol,http)=0\n");
                            protocol_name = "HTTP";
                            property.proto = 2;
                        }
                        else if(strcmp(protocol,"https")==0)
                        {
                            protocol_name = "HTTPS";
                            property.proto = 3;
                        }

                    }

                    if (strcmp(argv[i], "-file") == 0)
                    {
                        file_name = argv[i + 1];
                    }
                }
            }
        }
        // print out overall parameters:
        printf("Operation Mode: %s\n", para_mode.c_str());
        cout << "Parameters:" << endl;
        cout << "-stat (Interval): " << stat_disp << endl;
        cout << "-pktsize (Packet Size, in Bytes): " << bsize << endl;
        cout << "-pktrate (Sending rate, in Bytes/s): " << txrate << endl;
        cout << "-pktnum (Total number of packets): " << total_msg_num << endl;
        cout << "-rhost (Receiver host name/IP): " << send_host << endl;
        cout << "-rport (Receiver port number): " << send_port_num << endl;
        cout << "-proto (Transport protocol): " << protocol_name << endl;
        cout << "-sbufsize (Sender outgoing buffer, in Bytes): " << send_buf_size << endl;
        cout << "-rbufsize bsize (Incoming socket buffer size, in bytes): " << recv_buf_size << endl;
        cout << "-persist (yes|no): " << if_persistent << endl;
        cout << "-url: " << my_url << endl;
        cout << "-file: " << file_name << endl;
        cout << "" << endl;

        if (para_mode == "-send")
        {
            property.mode = 0;
        }
        else if (para_mode == "-recv")
        {
            property.mode = 1;
        }
        else if (para_mode == "-response")
        {
            property.mode = 2;
        }
        else if (para_mode == "-http")
        {
            property.mode = 3;
        }
        else
        {
            cout << "Wrong mode" << endl;
            return 0;
        }

        if (protocol_name == "UDP" || protocol_name == "udp")
        {
            property.proto = 0;
        }
        else if (protocol_name == "TCP" || protocol_name == "tcp")
        {
            property.proto = 1;
        }

        property.pktnum = total_msg_num;
        property.pktrate = txrate;
        property.pktsize = bsize;
        property.state = stat_disp;

        connect_info = parameter_packet_string_repr(&property);
        printf("connect_info: %s\n", connect_info.c_str());
    }

    /*
    no matter what mode or protocol it is,
    client will always connect with sever with TCP,
    recv "1" from sever,
    then send parameters to server with TCP
    */

    // 创建套接字
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("socket");
        exit(-1);
    }

    int bufsize;
    socklen_t size = sizeof(bufsize);
    getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &bufsize, &size);
    printf("initial socket buffer size is: %d\n", bufsize);

    struct hostent *pHost = NULL;
    string my_send_host;
    pHost = gethostbyname(send_host.c_str());
    if (pHost != NULL)
    {
        my_send_host = inet_ntoa(*((struct in_addr *)(pHost->h_addr_list[0])));
        cout << my_send_host << endl;
    }

    // 向服务器（特定的IP和端口）发起请求
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));                    // 每个字节都用0填充
    serv_addr.sin_family = AF_INET;                              // 使用IPv4地址
    serv_addr.sin_addr.s_addr = inet_addr(my_send_host.c_str()); // 具体的IP地址
    serv_addr.sin_port = htons(send_port_num);                   // 端口
    connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    char send_buf[100] = "\0";
    ssize_t ret_snd;
    strcpy(send_buf, connect_info.c_str());
    ret_snd = sendto(sock, send_buf, sizeof(send_buf), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret_snd < 0)
    {
        printf("%s", strerror(errno));
        return -1;
    }

    struct sockaddr_in sever_addr;
    socklen_t len = sizeof(struct sockaddr_in);
    ssize_t ret_rcv;

    char recv_buf[100] = "\0";
    ret_rcv = recvfrom(sock, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&sever_addr, &len); // 后两个可以直接写NULL
    if (ret_rcv < 0)
    {
        perror("recvfrom");
        close(sock);
        exit(-1);
    }

    // read(sock,recv_buf,sizeof(recv_buf));
    // printf("first recv: %s\n",recv_buf);

    if (recv_buf != NULL) // after receiving "1" from Sever, start transmitting/receiving
    {
        // close(sock);

        printf("start sending/recving... \n");
        // receive from server if mode == 1
        cout << "property.mode: " << property.mode << endl;
        cout << "property.proto: " << property.proto << endl;

        // client send TCP
        if (property.mode == 0 && property.proto == 1)
        {
            printf("Client send TCP\n");

            // set socket buffer size
            long set_socket_buf = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)(&send_buf_size), sizeof(send_buf_size));
            if (set_socket_buf < 0)
            {
                perror("set socket buf");
                // cout<<"aaa"<<endl;
            }

            // 读写数据
            // printf("txrate is %d\n",txrate);
            double single_iter_pkt_threshold = txrate * ((double)stat_disp / 1000); // in bytes/sec
            // cout<<"single_iter_pkt_threshold "<<single_iter_pkt_threshold<<endl;

            int a = 0; ////p_num_index
            int cum_bytes_sent = 0;
            int cum_packet_number = 0;
            double cum_send_time_cost = 0.0;

            while (a < total_msg_num || total_msg_num == 0)
            {
                struct timespec start, finish, delta;
                struct timespec start1, finish1, delta1;
                clock_gettime(CLOCK_REALTIME, &start);
                clock_gettime(CLOCK_REALTIME, &start1);
                if (cum_bytes_sent <= single_iter_pkt_threshold || single_iter_pkt_threshold == 0)
                {
                    char *message = generate_message(bsize * 8, a);
                    // cout<<message<<endl;
                    char my_message[bsize * 8];
                    strcpy(my_message, message);
                    // printf("sizeof message: %ld\n",sizeof(my_message));
                    ssize_t ret;

                    int sendto_flag = 1;
                    while (sendto_flag)
                    {
                        ret = sendto(sock, my_message, sizeof(my_message), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
                        // cout<<"haha"<<endl;
                        if (ret != -1)
                        {
                            sendto_flag = 0;
                        }
                        // sleep(1);
                    }
                    // free(message);
                    a++;
                    cum_packet_number++;
                    cum_bytes_sent = cum_bytes_sent + bsize;
                }
                clock_gettime(CLOCK_REALTIME, &finish);
                sub_timespec(start, finish, &delta);
                double time_cost = (double)delta.tv_sec + ((double)delta.tv_nsec) / 1000000000;

                double res = double(double(bsize) / double(txrate));
                // printf("res is: %f\n",res);
                double interval = res - time_cost;
                usleep(fabs(interval) * 1000000);

                clock_gettime(CLOCK_REALTIME, &finish1);
                sub_timespec(start1, finish1, &delta1);
                double tot_time_cost = (double)delta1.tv_sec + ((double)delta1.tv_nsec) / 1000000000;
                cum_send_time_cost = cum_send_time_cost + tot_time_cost;
                // cout<<cum_send_time_cost<<endl;

                // cum_send_time_cost = cum_send_time_cost + time_cost;
                // printf("cum_send_time_cost is: %f\n",cum_send_time_cost);
                // break;

                if (cum_send_time_cost * 1000 >= double(stat_disp))
                {
                    // cout<<cum_send_time_cost*1000<<endl;
                    // cout<<double(stat_disp)<<endl;
                    printf("Sender: [Elapsed] %.2f ms, [Pkts] %d, Rate %f Mbps\n", (cum_send_time_cost * 1000), cum_packet_number, double((8 * txrate)) / double(1000000));
                    cum_send_time_cost = 0;
                    cum_packet_number = 0;
                }
                cum_bytes_sent = 0;
            }
            // 关闭套接字
            close(sock);
        }

        // client recv TCP
        else if (property.mode == 1 && property.proto == 1)
        {
            cout << "Receiver (Client) Side receiving TCP" << endl;
            cout << "Listen Connection..." << endl;

            // set socket buffer size
            long set_socket_buf = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)(&recv_buf_size), sizeof(recv_buf_size));
            if (set_socket_buf < 0)
            {
                perror("set socket buf");
                // cout<<"aaa"<<endl;
            }

            // 读写数据
            struct sockaddr_in client_addr;
            socklen_t len = sizeof(struct sockaddr_in);

            int message_recv = 0;
            int total_messae_recv = 0;
            // double loss_rate = 0;
            int bytes_recv = 0;
            double cum_time_cost = 0;

            int Jitter_max = recv_bsize;
            double *inter_arrival_list = (double *)malloc(Jitter_max * sizeof(double));
            double *J_i_list = (double *)malloc(Jitter_max * sizeof(double));
            // double inter_arrival_list[Jitter_max*sizeof(double)];
            // double J_i_list[Jitter_max*sizeof(double)];

            double sum_J_i_list = 0;

            char buf[100] = "1"; // used to tell server could send data

            while (1)
            {
                write(sock, buf, sizeof(buf)); // tell server could send data
                bzero(buf, sizeof(buf));

                struct timespec start, finish, delta;
                clock_gettime(CLOCK_REALTIME, &start);
                char buf[recv_bsize * 8];
                // read(newconfd, buf, sizeof(buf));
                ssize_t ret;
                ret = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&client_addr, &len);
                if (ret < 0)
                {
                    perror("Failed to receive data");
                    exit(EXIT_FAILURE);
                }
                else if (ret == 0)
                {
                    printf("aaaaaa\n");
                    break;
                }
                else
                {
                    bytes_recv = bytes_recv + ret;
                }
                // printf("read: %s\n",buf);

                clock_gettime(CLOCK_REALTIME, &finish);
                sub_timespec(start, finish, &delta);
                double time_cost = (double)delta.tv_sec + ((double)delta.tv_nsec) / 1000000000;
                // printf("time_cost is %f\n",time_cost);

                // calculate for jitter
                inter_arrival_list[message_recv] = time_cost * 1000;
                // printf("inter_arrival_list[message_recv] is: %f\n", inter_arrival_list[message_recv]);

                message_recv += 1;
                total_messae_recv += 1;
                // printf("seq num is: %s\n",seq_num);

                // calculate for jitter
                double D = calculate_average_value(inter_arrival_list, message_recv);
                // printf("D is %f, time_cost is %f\n",D,time_cost*1000);

                J_i_list[message_recv - 1] = time_cost * 1000 - D;
                // printf("J_i_list is %f\n",J_i_list[message_recv-1]);
                sum_J_i_list += J_i_list[message_recv - 1];

                char *seq_num = got_sequence_number(buf);
                // printf("seq num is: %s\n",seq_num);

                bzero(buf, sizeof(buf));

                double throughput = (bytes_recv * 8) / (time_cost * 1000000);
                // printf("Rate is %.2f Mbps\n",throughput);

                cum_time_cost = cum_time_cost + time_cost;
                bytes_recv = 0;

                double cpuUtilization = getCPUUtilization();

                // std::cout << "CPU utilization: " << cpuUtilization << "%" << std::endl;

                if (cum_time_cost * 1000 >= double(stat_disp))
                {
                    double Jitter = sum_J_i_list / message_recv;
                    sum_J_i_list = 0;
                    // loss_rate = (atoi(seq_num) == total_messae_recv)?  0 : 100*(atoi(seq_num)-total_messae_recv)/double(atoi(seq_num));

                    // printf("Receiver: [Elapsed] %.2f ms, [Pkts] %d, [Rate] %.2f Mbps, [Jitter] %.6f ms, [CPU] %.2f%% \n",cum_time_cost*1000, message_recv, throughput, Jitter,cpuUtilization);
                    printf("Receiver: [Elapsed] %.2f ms, [Pkts] %d, [Rate] %.2f Mbps, [Jitter] %.6f ms \n", cum_time_cost * 1000, message_recv, throughput, Jitter);
                    cum_time_cost = 0;
                    message_recv = 0;
                    bytes_recv = 0;
                }
                // free(seq_num);
                // loss_rate = 0;
            }
            free(J_i_list);
            free(inter_arrival_list);
            // 关闭套接字
            close(sock);

            // //读写数据
            // char buf[100] = "1";
            // while(1)
            // {

            //     write(sock, buf, sizeof(buf));
            //     bzero(buf,sizeof(buf));
            //     read(sock, buf, sizeof(buf));
            //     puts(buf);
            // }
        }

        // client send udp
        else if (property.mode == 0 && property.proto == 0)
        {
            int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);

            // 向服务器（特定的IP和端口）发起请求
            struct sockaddr_in serv_addr;
            memset(&serv_addr, 0, sizeof(serv_addr));                    // 每个字节都用0填充
            serv_addr.sin_family = AF_INET;                              // 使用IPv4地址
            serv_addr.sin_addr.s_addr = inet_addr(my_send_host.c_str()); // 具体的IP地址
            serv_addr.sin_port = htons(send_port_num + atoi(recv_buf));  // 端口
            cout << "client send udp port: " << send_port_num + atoi(recv_buf) << endl;
            bzero(recv_buf, sizeof(recv_buf));

            // set socket buffer size
            long set_socket_buf = setsockopt(udp_sock, SOL_SOCKET, SO_SNDBUF, (char *)(&send_buf_size), sizeof(send_buf_size));
            if (set_socket_buf < 0)
            {
                perror("set socket buf");
                // cout<<"aaa"<<endl;
            }

            // 读写数据
            double single_iter_pkt_threshold = txrate * ((double)stat_disp / 1000); // in bytes/sec
            cout << "single_iter_pkt_threshold " << single_iter_pkt_threshold << endl;
            // E.g., stat = 1 s, pktrate = 10000 Bytes. Then each 1s, we totally allow 10 MB data to send (as “single_iter_pkt_threshold”)

            ssize_t ret;
            int a = 0; // p_num_index
            int cum_bytes_sent = 0;
            int cum_packet_number = 0;
            double cum_send_time_cost = 0;

            while (a < total_msg_num || total_msg_num == 0) // while(p_num_index<para_pktnum || para_pktnum == 0)
            {
                struct timespec start, finish, delta;
                struct timespec start1, finish1, delta1;
                clock_gettime(CLOCK_REALTIME, &start1);
                clock_gettime(CLOCK_REALTIME, &start);
                if (cum_bytes_sent <= single_iter_pkt_threshold || single_iter_pkt_threshold == 0)
                {
                    char *message = generate_message(bsize * 8, a);
                    // cout<<message<<endl;
                    char my_message[bsize * 8];
                    strcpy(my_message, message);
                    // printf("sizeof message: %ld\n",sizeof(my_message));

                    int sendto_flag = 1;
                    while (sendto_flag)
                    {
                        ret = sendto(udp_sock, my_message, sizeof(my_message), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
                        if (ret != -1)
                        {
                            sendto_flag = 0;
                        }
                        if (ret < 0)
                        {
                            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                                continue;
                            // printf("wenti\n");
                            printf("%s", strerror(errno));
                            return -1;
                        }
                    }
                    a++;
                    cum_packet_number++;

                    cum_bytes_sent = cum_bytes_sent + bsize; // in bytes
                }
                clock_gettime(CLOCK_REALTIME, &finish);
                sub_timespec(start, finish, &delta);
                double time_cost = (double)delta.tv_sec + ((double)delta.tv_nsec) / 1000000000;
                // printf("tiem cost: %f\n",time_cost);

                // printf("cum_packet_num: %d\n",cum_packet_number);
                // printf("cum_bytes_sent: %d\n",cum_bytes_sent);
                // break;

                double res = double(double(bsize) / double(txrate));
                // printf("res is: %f\n",res);
                double interval = res - time_cost;
                usleep(fabs(interval) * 1000000);
                // if(interval>0)
                // {
                //     sleep(interval);
                // }
                // sleep(res);

                // time_cost = 0;
                clock_gettime(CLOCK_REALTIME, &finish1);
                sub_timespec(start1, finish1, &delta1);
                double tot_time_cost = (double)delta1.tv_sec + ((double)delta1.tv_nsec) / 1000000000;
                cum_send_time_cost = cum_send_time_cost + tot_time_cost;
                // cout<<cum_send_time_cost<<endl;

                // cum_send_time_cost = cum_send_time_cost + time_cost;
                // printf("cum_send_time_cost is: %f\n",cum_send_time_cost);
                // break;

                if (cum_send_time_cost * 1000 >= double(stat_disp))
                {
                    // cout<<cum_send_time_cost*1000<<endl;
                    // cout<<double(stat_disp)<<endl;
                    printf("Sender: [Elapsed] %.2f ms, [Pkts] %d, Rate %f Mbps\n", (cum_send_time_cost * 1000), cum_packet_number, double(8 * txrate) / double(1000000));
                    cum_send_time_cost = 0;
                    cum_packet_number = 0;
                }

                cum_bytes_sent = 0;
            }

            close(udp_sock);

            // if client is closed, client will send "finish" to server, if server recv "finish", it will close that thread
            char finish_message[100] = "finish";
            write(sock, finish_message, sizeof(finish_message));
            close(sock);
        }

        // client recv UDP
        else if (property.mode == 1 && property.proto == 0)
        {
            printf("property.mode == 1 && property.proto == 0\n");
            int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd < 0)
            {
                perror("socket");
                exit(-1);
            }
            printf("sockfd %d\n", sockfd);

            struct sockaddr_in server_addr;
            server_addr.sin_family = AF_INET;
            int port_num = send_port_num + atoi(recv_buf);
            cout << "client send udp port: " << port_num << endl;
            bzero(recv_buf, sizeof(recv_buf));
            server_addr.sin_port = htons(port_num);
            server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 根据不同server地址需要修改

            int my_bind = bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
            if (my_bind < 0)
            {
                perror("bind");
                close(sockfd);
                exit(-1);
            }
            // 读写数据
            struct sockaddr_in client_addr;
            socklen_t len = sizeof(struct sockaddr_in);
            ssize_t ret;

            int message_recv = 0;
            int total_messae_recv = 0;
            double loss_rate = 0;
            int loss_num = 0;
            int bytes_recv = 0;
            double cum_time_cost = 0;

            int Jitter_max = recv_bsize;
            // double *inter_arrival_list = (double*)malloc(Jitter_max*sizeof(double));
            // double *J_i_list = (double*)malloc(Jitter_max*sizeof(double));
            double inter_arrival_list[Jitter_max * sizeof(double)];
            double J_i_list[Jitter_max * sizeof(double)];

            double sum_J_i_list = 0;

            while (1)
            {
                // printf("hahahahahahha\n");
                // printf("");
                struct timespec start, finish, delta;
                clock_gettime(CLOCK_REALTIME, &start);
                // sleep(1);

                // char buf[bsize] = "\0";
                // recv_bsize = recv_bsize;
                char buf[recv_bsize * 8];

                int ret = recvfrom(sockfd, buf, sizeof(buf), 0, (struct sockaddr *)&client_addr, &len); // ts->connfd == newconfd
                if (ret == 0)
                {
                    printf("the client %d closed...\n", sockfd);
                    break; // 跳出循环,关闭cfd
                }
                // printf("received from %s at PORT %d\n",
                //         inet_ntop(AF_INET, &(*ts).cliaddr.sin_addr, str, sizeof(str)),
                //         ntohs((*ts).cliaddr.sin_port));         //打印客户端信息(IP/PORT)
                // printf("received: %s\n",buf);
                else
                {
                    // cout<<buf<<endl;
                    bytes_recv = bytes_recv + ret; // in bytes
                }
                clock_gettime(CLOCK_REALTIME, &finish);
                sub_timespec(start, finish, &delta);
                double time_cost = (double)delta.tv_sec + ((double)delta.tv_nsec) / 1000000000;
                // printf("The time receiving data is: %f\n",time_cost);

                // calculate for jitter
                inter_arrival_list[message_recv] = time_cost * 1000;

                message_recv += 1;
                total_messae_recv += 1;
                // printf("seq num is: %s\n",seq_num);

                // calculate for jitter
                double D = calculate_average_value(inter_arrival_list, message_recv);
                // printf("D is %f, time_cost is %f\n",D,time_cost*1000);

                J_i_list[message_recv - 1] = time_cost * 1000 - D;
                // printf("J_i_list is %f\n",J_i_list[message_recv-1]);
                sum_J_i_list += J_i_list[message_recv - 1];

                // printf("message recv: %d\n",message_recv);

                char *seq_num = got_sequence_number(buf);
                char udp_seq_num[recv_bsize * 8];
                strcpy(udp_seq_num, seq_num);
                // cout<<"udp_seq_num: "<<udp_seq_num<<endl;
                free(seq_num);

                // cout<<seq_num<<endl;

                bzero(buf, sizeof(buf));

                double throughput = (bytes_recv * 8) / (time_cost * 1000000);
                // printf("Rate is %.2f Mbps\n",throughput);

                cum_time_cost = cum_time_cost + time_cost;
                // printf("cum time cost: %f\n",cum_time_cost);
                // printf("stat_disp is: %d\n",stat_disp);
                bytes_recv = 0;

                double cpuUtilization = getCPUUtilization();

                // std::cout << "CPU utilization: " << cpuUtilization << "%" << std::endl;

                if (cum_time_cost * 1000 >= double(stat_disp))
                {
                    double Jitter = sum_J_i_list / message_recv;
                    sum_J_i_list = 0;
                    loss_num = atoi(udp_seq_num) - total_messae_recv;
                    loss_rate = (atoi(udp_seq_num) == total_messae_recv) ? 0 : double(100 * (atoi(udp_seq_num) - total_messae_recv)) / double(atoi(udp_seq_num));

                    // printf("Receiver: Connected fd [%d], Elapsed [%.2f ms], Pkts [%d], Rate [%.2f Mbps], Jitter [%.6f ms], CPU [%.2f%%], Loss [%d, %f%%]\n",sockfd,cum_time_cost*1000, message_recv,throughput, Jitter, cpuUtilization, loss_num,loss_rate);
                    // printf("CPU占用率:%f\n",cpu);
                    // printf("Receiver: Connected fd [%d], Elapsed [%.2f ms], Pkts [%d], Rate [%.2f Mbps], Jitter [%.6f ms], Loss [%d, %f%%]\n",sockfd,cum_time_cost*1000, message_recv,throughput, Jitter, loss_num,loss_rate);

                    printf("Receiver: Elapsed [%.2f ms], Pkts [%d], Rate [%.2f Mbps], Jitter [%.6f ms], Loss [%d, %f%%]\n", cum_time_cost * 1000, message_recv, throughput, Jitter, loss_num, loss_rate);
                    cum_time_cost = 0;
                    message_recv = 0;
                    // loss_num = 0;
                }
                // free(seq_num);
                loss_rate = 0;
                loss_num = 0;

                char finish_message[100] = "notfinish";
                // write(sock,finish_message,sizeof(finish_message));
                ret = sendto(sock, finish_message, sizeof(finish_message), 0, NULL, 0);
                if (ret < 0)
                {
                    // printf("wenti\n");
                    printf("%s\n", strerror(errno));
                    break;
                    // exit(-1);	//pthread_exit(0)效果一样;
                }

                // close(sockfd);
                // close(sock);
            }

            // cout<<"finished!!!"<<endl;
            close(sockfd);

            // //if client is closed, client will send "finish" to server, if server recv "finish", it will close that thread
            // char finish_message[100] = "finish";
            // //write(sock,finish_message,sizeof(finish_message));
            // ret = sendto(sock,finish_message,sizeof(finish_message),0,NULL, 0);
            // if(ret < 0)
            // {
            //     //printf("wenti\n");
            //     printf("%s\n", strerror(errno));
            //     close(sock);
            //     exit(-1);	//pthread_exit(0)效果一样;
            // }
            close(sock);
        }

        else if (property.mode == 2)
        {
            int one = 1;
            setsockopt(sock, SOL_TCP, TCP_NODELAY, &one, sizeof(one));
            cout << "Receiver (Client) Side responding TCP" << endl;
            cout << "Listen Connection..." << endl;

            // set socket buffer size
            long set_socket_buf = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)(&send_buf_size), sizeof(send_buf_size));
            if (set_socket_buf < 0)
            {
                perror("set socket buf");
                // cout<<"aaa"<<endl;
            }

            if (if_persistent == false)
            {
                printf("false\n");
                // firstly tell server that we need a new TCP connection
                char persistent_buf[100] = "false";
                sendto(sock, persistent_buf, sizeof(persistent_buf), 0, NULL, 0);

                double single_iter_pkt_threshold = txrate; // in num/sec
                cout << "single_iter_pkt_threshold " << single_iter_pkt_threshold << endl;

                int a = 0; ////p_num_index
                int cum_bytes_sent = 0;
                int cum_packet_number = 0;
                double cum_send_time_cost = 0.0;

                int Jitter_max = 1000;
                double *inter_arrival_list = (double *)malloc(Jitter_max * sizeof(double));
                double *J_i_list = (double *)malloc(Jitter_max * sizeof(double));

                double sum_J_i_list = 0;

                int bytes_recv = 0;
                int message_recv = 0;
                int total_messae_recv = 0;

                int request_num = 0;

                while (a < total_msg_num || total_msg_num == 0)
                {
                    struct timespec start, finish, delta;
                    struct timespec start1, finish1, delta1;
                    clock_gettime(CLOCK_REALTIME, &start);
                    clock_gettime(CLOCK_REALTIME, &start1);
                    ssize_t ret;
                    // if(request_num<=single_iter_pkt_threshold)
                    // {
                    // char* message = generate_message(bsize*8,a);
                    char message[100] = "request"; // used to tell server could send data
                    // cout<<message<<endl;
                    char my_message[100];
                    strcpy(my_message, message);
                    // printf("sizeof message: %ld\n",sizeof(my_message));

                    int sendto_flag = 1;
                    while (sendto_flag)
                    {
                        ret = sendto(sock, my_message, sizeof(my_message), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
                        // cout<<"haha"<<endl;
                        if (ret != -1)
                        {
                            sendto_flag = 0;
                        }
                        // sleep(1);
                    }
                    // free(message);
                    a++;
                    cum_packet_number++;
                    cum_bytes_sent = cum_bytes_sent + bsize;
                    request_num++;

                    char recv_buf[recv_bsize * 8];
                    // read(newconfd, buf, sizeof(buf));

                    ret = recvfrom(sock, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&serv_addr, &len);
                    if (ret < 0)
                    {
                        perror("Failed to receive data");
                        exit(EXIT_FAILURE);
                    }
                    else if (ret == 0)
                    {
                        printf("Client finished sending\n");
                        break;
                    }
                    else
                    {
                        bytes_recv = bytes_recv + ret;
                    }
                    // }

                    request_num = 0;

                    clock_gettime(CLOCK_REALTIME, &finish);
                    sub_timespec(start, finish, &delta);

                    double time_cost = (double)delta.tv_sec + ((double)delta.tv_nsec) / 1000000000;

                    // calculate for jitter
                    inter_arrival_list[message_recv] = time_cost * 1000;
                    message_recv += 1;
                    total_messae_recv += 1;

                    // calculate for jitter
                    double D = calculate_average_value(inter_arrival_list, message_recv);
                    // printf("D is %f, time_cost is %f\n",D,time_cost*1000);

                    // printf("time_cost is %f\n",time_cost*1000);
                    J_i_list[message_recv - 1] = time_cost * 1000 - D;
                    // printf("J_i_list is %f\n",J_i_list[message_recv-1]);
                    sum_J_i_list += J_i_list[message_recv - 1];

                    // double res = double(double(bsize)/double(txrate));

                    // double res = double(double(bsize)/double(txrate));
                    // printf("res is: %f\n",res);

                    // cout<<"time_cost: "<<time_cost<<endl;
                    double interval = double(1) / double(single_iter_pkt_threshold) - time_cost;
                    usleep(fabs(interval) * 1000000);

                    clock_gettime(CLOCK_REALTIME, &finish1);
                    sub_timespec(start1, finish1, &delta1);
                    double tot_time_cost = (double)delta1.tv_sec + ((double)delta1.tv_nsec) / 1000000000;
                    cum_send_time_cost = cum_send_time_cost + tot_time_cost;
                    // cout<<cum_send_time_cost<<endl;

                    // cum_send_time_cost = cum_send_time_cost + time_cost;
                    // printf("cum_send_time_cost is: %f\n",cum_send_time_cost);
                    // break;

                    if (cum_send_time_cost * 1000 >= double(stat_disp))
                    {
                        // printf("Sender: [Elapsed] %.2f ms, [Pkts] %d, Rate %d Mbps\n",(cum_send_time_cost*1000), cum_packet_number, (8*txrate)/1000000);
                        // printf("sum_J_i_list %f\n",sum_J_i_list);
                        double Jitter = sum_J_i_list / message_recv;

                        int n = message_recv;
                        // double min_val = (*min_element(inter_arrival_list+1, inter_arrival_list+total_messae_recv-1))*1000;
                        double min_val = cal_min_value(inter_arrival_list, n);
                        // double max_val = (*max_element(inter_arrival_list+1, inter_arrival_list+total_messae_recv-1))*1000;
                        double max_val = cal_max_value(inter_arrival_list, n);
                        // double sum = accumulate(inter_arrival_list+1, inter_arrival_list+total_messae_recv, 0)*1000; //计算数组元素总和

                        double avg_val = average(inter_arrival_list, n);
                        printf("Receiver: [Elapsed] %.2f ms, [Replies] %d, [min] %f ms, [max] %f ms, [avg] %f ms [Jitter] %.6f ms \n", (cum_send_time_cost * 1000), message_recv, min_val, max_val, avg_val, fabs(Jitter));

                        cum_send_time_cost = 0;
                        cum_packet_number = 0;
                        message_recv = 0;
                        bytes_recv = 0;
                        sum_J_i_list = 0;
                    }
                    cum_bytes_sent = 0;
                }
                free(J_i_list);
                free(inter_arrival_list);
                // 关闭套接字
                close(sock);
            }
            else if (if_persistent == true)
            {
                printf("true\n");

                // firstly tell server that we need a new TCP connection
                char persistent_buf[100] = "false";
                sendto(sock, persistent_buf, sizeof(persistent_buf), 0, NULL, 0);

                double single_iter_pkt_threshold = txrate; // in num/sec
                cout << "single_iter_pkt_threshold " << single_iter_pkt_threshold << endl;

                int a = 0; ////p_num_index
                int cum_bytes_sent = 0;
                int cum_packet_number = 0;
                double cum_send_time_cost = 0.0;

                int Jitter_max = 1000;
                double *inter_arrival_list = (double *)malloc(Jitter_max * sizeof(double));
                double *J_i_list = (double *)malloc(Jitter_max * sizeof(double));

                double sum_J_i_list = 0;

                int bytes_recv = 0;
                int message_recv = 0;
                int total_messae_recv = 0;

                int request_num = 0;

                while (a < total_msg_num || total_msg_num == 0)
                {
                    struct timespec start, finish, delta;
                    struct timespec start1, finish1, delta1;
                    clock_gettime(CLOCK_REALTIME, &start);
                    clock_gettime(CLOCK_REALTIME, &start1);
                    ssize_t ret;
                    // if(request_num<=single_iter_pkt_threshold)
                    // {
                    // char* message = generate_message(bsize*8,a);
                    char message[100] = "request"; // used to tell server could send data
                    // cout<<message<<endl;
                    char my_message[100];
                    strcpy(my_message, message);
                    // printf("sizeof message: %ld\n",sizeof(my_message));

                    int sendto_flag = 1;
                    while (sendto_flag)
                    {
                        ret = sendto(sock, my_message, sizeof(my_message), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
                        // cout<<"haha"<<endl;
                        if (ret != -1)
                        {
                            sendto_flag = 0;
                        }
                        // sleep(1);
                    }
                    // free(message);
                    a++;
                    cum_packet_number++;
                    cum_bytes_sent = cum_bytes_sent + bsize;
                    request_num++;

                    char recv_buf[recv_bsize * 8];
                    // read(newconfd, buf, sizeof(buf));

                    ret = recvfrom(sock, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&serv_addr, &len);
                    if (ret < 0)
                    {
                        perror("Failed to receive data");
                        exit(EXIT_FAILURE);
                    }
                    else if (ret == 0)
                    {
                        printf("Client finished sending\n");
                        break;
                    }
                    else
                    {
                        bytes_recv = bytes_recv + ret;
                    }
                    // }

                    request_num = 0;

                    clock_gettime(CLOCK_REALTIME, &finish);
                    sub_timespec(start, finish, &delta);

                    double time_cost = (double)delta.tv_sec + ((double)delta.tv_nsec) / 1000000000;

                    // calculate for jitter
                    inter_arrival_list[message_recv] = time_cost * 1000;
                    message_recv += 1;
                    total_messae_recv += 1;

                    // calculate for jitter
                    double D = calculate_average_value(inter_arrival_list, message_recv);
                    // printf("D is %f, time_cost is %f\n",D,time_cost*1000);

                    // printf("time_cost is %f\n",time_cost*1000);
                    J_i_list[message_recv - 1] = time_cost * 1000 - D;
                    // printf("J_i_list is %f\n",J_i_list[message_recv-1]);
                    sum_J_i_list += J_i_list[message_recv - 1];

                    // double res = double(double(bsize)/double(txrate));

                    // double res = double(double(bsize)/double(txrate));
                    // printf("res is: %f\n",res);

                    // cout<<"time_cost: "<<time_cost<<endl;
                    double interval = double(1) / double(single_iter_pkt_threshold) - time_cost;
                    usleep(fabs(interval) * 1000000);

                    clock_gettime(CLOCK_REALTIME, &finish1);
                    sub_timespec(start1, finish1, &delta1);
                    double tot_time_cost = (double)delta1.tv_sec + ((double)delta1.tv_nsec) / 1000000000;
                    cum_send_time_cost = cum_send_time_cost + tot_time_cost;
                    // cout<<cum_send_time_cost<<endl;

                    // cum_send_time_cost = cum_send_time_cost + time_cost;
                    // printf("cum_send_time_cost is: %f\n",cum_send_time_cost);
                    // break;

                    if (cum_send_time_cost * 1000 >= double(stat_disp))
                    {
                        // printf("Sender: [Elapsed] %.2f ms, [Pkts] %d, Rate %d Mbps\n",(cum_send_time_cost*1000), cum_packet_number, (8*txrate)/1000000);
                        // printf("sum_J_i_list %f\n",sum_J_i_list);
                        double Jitter = sum_J_i_list / message_recv;

                        int n = message_recv;
                        // double min_val = (*min_element(inter_arrival_list+1, inter_arrival_list+total_messae_recv-1))*1000;
                        double min_val = cal_min_value(inter_arrival_list, n);
                        // double max_val = (*max_element(inter_arrival_list+1, inter_arrival_list+total_messae_recv-1))*1000;
                        double max_val = cal_max_value(inter_arrival_list, n);
                        // double sum = accumulate(inter_arrival_list+1, inter_arrival_list+total_messae_recv, 0)*1000; //计算数组元素总和

                        double avg_val = average(inter_arrival_list, n);
                        printf("Receiver: [Elapsed] %.2f ms, [Replies] %d, [min] %f ms, [max] %f ms, [avg] %f ms [Jitter] %.6f ms \n", (cum_send_time_cost * 1000), message_recv, min_val, max_val, avg_val, fabs(Jitter));

                        cum_send_time_cost = 0;
                        cum_packet_number = 0;
                        message_recv = 0;
                        bytes_recv = 0;
                        sum_J_i_list = 0;
                    }
                    cum_bytes_sent = 0;
                }
                free(J_i_list);
                free(inter_arrival_list);
                // 关闭套接字
                close(sock);
            }
        }

        else if (property.mode == 3)
        {
            int one = 1;
            setsockopt(sock, SOL_TCP, TCP_NODELAY, &one, sizeof(one));
            cout << "Receiver (Client) Side send request" << endl;

            char url[256];
            strcpy(url, my_url.c_str());
            char request[BUFFER_SIZE];
            char serverIP[INET6_ADDRSTRLEN];

            // Parse the URL to extract the host and path

            // Get the protocol
            char protocol[16] = "";
            char *protocolEnd = strstr(url, "://");
            if (protocolEnd != NULL)
            {
                strncpy(protocol, url, protocolEnd - url);
            }

            // Get the hostname
            char *hostnameStart = protocolEnd != NULL ? protocolEnd + 3 : url;
            char *hostnameEnd = strchr(hostnameStart, '/');
            char host[256] = "";
            if (hostnameEnd != NULL)
            {
                strncpy(host, hostnameStart, hostnameEnd - hostnameStart);
            }
            else
            {
                strcpy(host, hostnameStart);
            }

            // Get the port number
            char *portStart = strchr(host, ':');
            char port[16] = "";
            if (portStart != NULL)
            {
                strcpy(port, portStart + 1);
                *portStart = '\0';
            }
            else
            {
                printf("portStart == NULL\n");
            }

            // Get the path
            char *pathStart = hostnameEnd != NULL ? hostnameEnd : url + strlen(url);
            char path[256] = "";
            strcpy(path, pathStart);

            // Display the extracted components
            printf("Protocol: %s\n", protocol);
            printf("Hostname: %s\n", host);
            printf("Port: %s\n", port);
            printf("Path: %s\n", path);

            // printf("host is %s\n",host);
            // printf("port is %s\n",port);
            // printf("path is %s\n",path);

            // Resolve the host name to IP address
            struct addrinfo hints, *res;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            if (getaddrinfo(host, protocol, &hints, &res) != 0)
            {
                perror("Failed to resolve host");
                exit(1);
            }

            // Convert the IP address to a string
            inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, serverIP, sizeof(serverIP));

            // Create socket
            int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
            if (clientSocket < 0)
            {
                perror("Failed to create socket");
                exit(1);
            }

            // Set server address information
            struct sockaddr_in serverAddr;
            memset(&serverAddr, 0, sizeof(serverAddr));
            serverAddr.sin_family = AF_INET;
            printf("----\n");

            /*----------for http protocol-----------*/
            if (strcmp(protocol, "http") == 0)
            {
                if (strcmp(port, "") == 0)
                {
                    printf("hahahahah\n");
                    if (strcmp(host, "localhost") == 0)
                    {
                        printf("haha");
                        serverAddr.sin_port = htons(8080);
                    }
                    else
                    {
                        serverAddr.sin_port = htons(80);
                    }
                }
                else
                {
                    printf("xx\n");
                    serverAddr.sin_port = htons(atoi(port));
                }

                if (inet_pton(AF_INET, serverIP, &(serverAddr.sin_addr)) <= 0)
                {
                    perror("Invalid server IP address");
                    exit(1);
                }

                // Connect to the server
                if (connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
                {
                    perror("Failed to connect to server");
                    exit(1);
                }

                // Prepare HTTP GET request
                if (strcmp(path, "") == 0)
                {
                    snprintf(request, BUFFER_SIZE, "GET / HTTP/1.1\r\n"
                                                   "Host: %s\r\n"
                                                   "Connection: close\r\n"
                                                   "\r\n",
                             host);
                }
                else
                {
                    // printf("haha\n");
                    snprintf(request, BUFFER_SIZE, "GET %s HTTP/1.1\r\n"
                                                   "Host: %s\r\n"
                                                   "Connection: close\r\n"
                                                   "\r\n",
                             path, host);
                }

                // Send the request to the server
                if (send(clientSocket, request, strlen(request), 0) < 0)
                {
                    perror("Failed to send request");
                    exit(1);
                }

                // Receive and print the response from the server
                char response[BUFFER_SIZE];
                memset(response, 0, sizeof(response));
                FILE *fp = NULL;
                
                const char* my_file_name = file_name.c_str();
                if(strcmp(my_file_name,"")== 0)
                {
                    while (recv(clientSocket, response, BUFFER_SIZE - 1, 0) > 0)
                    {
                        printf("%s", response);
                        //fputs(response, fp);
                        memset(response, 0, sizeof(response));
                    }
                }
                else
                {
                    fp = fopen(my_file_name, "w+");
                    while (recv(clientSocket, response, BUFFER_SIZE - 1, 0) > 0)
                    {
                        //printf("%s", response);
                        fputs(response, fp);
                        memset(response, 0, sizeof(response));
                    }
                    fclose(fp);
                }
                
                // Close the socket
                close(clientSocket);
            }


            /*----------for https protocol-----------*/
            else if (strcmp(protocol, "https") == 0)
            {
                
                if (strcmp(port, "") == 0)
                {
                    if (strcmp(host, "localhost") == 0)
                    {
                        serverAddr.sin_port = htons(4433);
                    }
                    else
                    {
                        serverAddr.sin_port = htons(443);
                    }
                }
                else
                {
                    printf("with port num\n");
                    serverAddr.sin_port = htons(atoi(port));
                }

                if (inet_pton(AF_INET, serverIP, &(serverAddr.sin_addr)) <= 0)
                {
                    perror("Invalid server IP address");
                    exit(1);
                }

                // Connect to the server
                if (connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
                {
                    perror("Failed to connect to server");
                    exit(1);
                }

                // Initialize SSL
                SSL_library_init();
                SSL_CTX *sslContext = SSL_CTX_new(SSLv23_client_method());

                SSL *ssl = SSL_new(sslContext);
                SSL_set_fd(ssl, clientSocket);

                // Enable server certificate verification
                if (strcmp(host, "localhost") == 0)
                {
                    // Load trusted CA certificates from the system's trust store
                    if (SSL_CTX_set_default_verify_paths(sslContext) != 1)
                    {
                        fprintf(stderr, "Failed to load default CA certificates\n");
                        exit(1);
                    }

                    // Load the client's self-signed CA certificate from the local directory
                    if (SSL_CTX_load_verify_locations(sslContext, "domain.crt", NULL) != 1)
                    {
                        fprintf(stderr, "Failed to load client CA certificate\n");
                        exit(1);
                    }
                    SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);
                }

                // Enable server name indication (SNI)
                // SSL_set_tlsext_host_name(ssl, "www.google.com");
                // SSL_set_tlsext_host_name(ssl, "www.ietf.org");
                SSL_set_tlsext_host_name(ssl, host);

                // Establish SSL connection
                if (SSL_connect(ssl) <= 0)
                {
                    fprintf(stderr, "Failed to establish SSL connection\n");
                    exit(1);
                }
                // Perform server hostname verification
                X509 *serverCert = SSL_get_peer_certificate(ssl);
                if (serverCert == NULL)
                {
                    fprintf(stderr, "Failed to retrieve server certificate\n");
                    exit(1);
                }

                // Perform data exchange over the SSL/TLS connection
                // char request[BUFFER_SIZE];
                // snprintf(request, BUFFER_SIZE, "GET /%s HTTP/1.1\r\n"
                //                                "Host: %s\r\n"
                //                                "Connection: close\r\n"
                //                                "\r\n",
                //          path, host);
                if (path == "")
                {
                    snprintf(request, BUFFER_SIZE, "GET / HTTP/1.1\r\n"
                                                   "Host: %s\r\n"
                                                   "Connection: close\r\n"
                                                   "\r\n",
                             host);
                }
                else
                {
                    // printf("haha\n");
                    snprintf(request, BUFFER_SIZE, "GET %s HTTP/1.1\r\n"
                                                   "Host: %s\r\n"
                                                   "Connection: close\r\n"
                                                   "\r\n",
                             path, host);
                }
                printf("req is: %s\n", request);

                // Send the request to the server
                if (strcmp(host, "localhost") == 0)
                {
                    char serverCN[256];
                    X509_NAME_get_text_by_NID(X509_get_subject_name(serverCert), NID_commonName, serverCN, sizeof(serverCN));
                    if (strcmp(serverCN, "localhost") != 0)
                    {
                        fprintf(stderr, "Hostname verification failed. Expected: localhost, Actual: %s\n", serverCN);
                        exit(1);
                    }
                    // printf("send to localhost\n");
                    if (send(clientSocket, request, strlen(request), 0) < 0)
                    {
                        perror("Failed to send request");
                        exit(1);
                    }
                }
                else
                {
                    if (SSL_write(ssl, request, strlen(request)) < 0)
                    {
                        perror("Failed to send request");
                        exit(1);
                    }
                }

                // Receive and print the response from the server, save it into a file
                char response[BUFFER_SIZE];
                memset(response, 0, sizeof(response));
                FILE *fp = NULL;
                const char* my_file_name = file_name.c_str();

                if(strcmp(my_file_name,"")==0)
                {
                    
                    if (strcmp(host, "localhost") == 0)
                    {
                        while (recv(clientSocket, response, BUFFER_SIZE - 1, 0) > 0)
                        {
                            printf("%s", response); 
                            memset(response, 0, sizeof(response));
                        }
                    }
                    else
                    {
                        while (SSL_read(ssl, response, BUFFER_SIZE - 1) > 0)
                        {
                            printf("%s", response); 
                            memset(response, 0, sizeof(response));
                        }
                    }

                    
                }
                else
                {
                    fp = fopen(my_file_name, "w+");
                    if (strcmp(host, "localhost") == 0)
                    {
                        while (recv(clientSocket, response, BUFFER_SIZE - 1, 0) > 0)
                        {
                            printf("%s", response);
                            fputs(response, fp);
                            memset(response, 0, sizeof(response));
                        }
                    }
                    else
                    {
                        while (SSL_read(ssl, response, BUFFER_SIZE - 1) > 0)
                        {
                            printf("%s", response);
                            fputs(response, fp);
                            memset(response, 0, sizeof(response));
                        }
                    }
                    fclose(fp);
                }

                

                X509_NAME *subject_name = X509_get_subject_name(serverCert);
                if (subject_name != NULL)
                {
                    char *subject_data = X509_NAME_oneline(subject_name, NULL, 0);
                    printf("\n");
                    printf("Displaying the certificate subject data:\n%s\n", subject_data);
                    OPENSSL_free(subject_data);
                }
                else
                {
                    printf("Failed to retrieve the certificate subject data\n");
                }
                X509_free(serverCert);

                // Cleanup
                SSL_shutdown(ssl);
                SSL_free(ssl);
                SSL_CTX_free(sslContext);
                close(clientSocket);
            }
        }

        // 关闭套接字
        // close(sock);
    }
    return 0;
}