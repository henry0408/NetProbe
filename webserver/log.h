#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>

#include "locker.h"

using namespace std;

class Log
{
public:
    //C++11以后,使用局部变量懒汉不用加锁
    static Log *get_instance()
    {
        // static Log instance;
        // return &instance;
        if(instance == NULL)
        {
            instance = new Log;
        }
        return instance;
    }

    //可选择的参数有日志文件、日志缓冲区大小、最大行数
    bool init(const char *file_name, int log_buf_size = 8192, int split_lines = 5000000);

    //将输出内容按照标准格式整理
    void write_log(int level, const char *format, ...);

    //强制刷新缓冲区
    void flush(void);

private:
    Log()
    {
        m_count = 0;            //日志默认0行
    };
    // virtual ~Log();

    static Log* instance;

private:
    char dir_name[128]; //路径名
    char log_name[128]; //log文件名
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针
    char *m_buf;
    locker m_mutex;     //互斥锁，互斥写日志文件
};


//重定向
//对日志等级进行分级，包括DEBUG，INFO，WARN和ERROR四种级别的日志。

/*
0: Debug，调试代码时的输出，在系统实际运行时，一般不使用。

1: Warn，这种警告与调试时终端的warning类似，同样是调试代码时使用。

2: Info，报告系统当前的状态，当前执行的流程或接收的信息等。

3: Error和Fatal，输出系统的错误信息。
*/

#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif
