/*
 * @version: 
 * @Author: zsq 1363759476@qq.com
 * @Date: 2024-02-17 16:51:49
 * @LastEditors: zsq 1363759476@qq.com
 * @LastEditTime: 2024-07-19 13:15:40
 * @FilePath: /Linux_nc/WebServer/myWebServer/logger.cpp
 * @Descripttion: 
 */
#include <time.h>
#include <iostream>
#include "logger.h"

// 获取日志的单例，单例模式
Logger& Logger::GetInstance() {
    static Logger logger; // 只创建一个 Logger 实例，并在整个项目中共享这个实例
    return logger;
}

Logger::Logger() {
    std::thread writeLogTask([&]() {
        for (;;) {
            // 获取当前的日期，然后取日志信息，写入相应的日志文件当中 a+(追加的方式，文件不存在的话创建，文件存在的话追加)
            time_t now = time(nullptr);
            tm *nowtm = localtime(&now);

            // 构建文件名，年月日-log.txt
            char file_name[128];
            sprintf(file_name, "%d-%d-%d-log.txt", nowtm->tm_year+1900, nowtm->tm_mon+1, nowtm->tm_mday);
            
            // 打开日志文件
            FILE *pf = fopen(file_name, "a+");
            if (pf == nullptr) {
                std::cout << "logger file : " << file_name << " open error!" << std::endl;
                exit(EXIT_FAILURE);
            }

            std::string msg = m_lckQue.Pop();

            // 时分秒 %d:%d:%d
            char time_buf[128] = {0};
            sprintf(time_buf, "%d:%d:%d =>[%s] ", 
                    nowtm->tm_hour, 
                    nowtm->tm_min, 
                    nowtm->tm_sec,
                    (m_loglevel == INFO ? "info" : "error"));
            msg.insert(0, time_buf);
            msg.append("\n");

            // 把数据写入文件
            fputs(msg.c_str(), pf);
            fclose(pf);
        }
    }); 

    // 设置分离线程，就相当于是一个守护线程
    writeLogTask.detach();
}

// 设置日志级别 
void Logger::SetLogLevel(LogLevel level) {
    m_loglevel = level;
}

// 写日志， 把日志信息写入lockqueue缓冲区当中
void Logger::Log(std::string msg) {
    m_lckQue.Push(msg);
}