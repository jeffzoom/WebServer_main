/*
 * @version: 
 * @Author: zsq 1363759476@qq.com
 * @Date: 2024-02-17 16:51:07
 * @LastEditors: zsq 1363759476@qq.com
 * @LastEditTime: 2024-10-01 15:52:11
 * @FilePath: /Linux_nc/WebServer/WebServer_main/logger.h
 * @Descripttion: 
 */

// #pragma once

#ifndef _LOGGER_H_
#define _LOGGER_H_
#include <string>
#include "lockqueue.h"

// 定义宏 LOG_INFO("xxx %d %s", 20, "xxxx")
#define LOG_INFO(logmsgformat, ...) \
    do \
    {  \
        Logger &logger = Logger::GetInstance(); \
        logger.SetLogLevel(INFO); \
        char c[1024] = {0}; \
        snprintf(c, 1024, logmsgformat, ##__VA_ARGS__); \
        logger.Log(c); \
    } while(0) \
// __VA_ARGS__代表可变参的参数列表 

#define LOG_ERR(logmsgformat, ...) \
    do \
    {  \
        Logger &logger = Logger::GetInstance(); \
        logger.SetLogLevel(ERROR); \
        char c[1024] = {0}; \
        snprintf(c, 1024, logmsgformat, ##__VA_ARGS__); \
        logger.Log(c); \
    } while(0) \

// 日志包含普通信息和错误信息
enum LogLevel {
    INFO,  // 普通信息
    ERROR, // 错误信息
};

// Mprpc框架提供的日志系统
class Logger {
public:
    // 获取日志的单例  用到了单例设计模式，单例的意思是只有一个对象，因此在构造函数中可以进行初始化
    static Logger& GetInstance();
    // 设置日志级别 
    void SetLogLevel(LogLevel level);
    // 写日志
    void Log(std::string msg);
    
private:
    int m_loglevel; // 记录日志级别
    LockQueue<std::string>  m_lckQue; // 日志缓冲队列

    Logger(); // 单例的意思是只有一个对象，因此在构造函数中可以进行日志模块的初始化
    Logger(const Logger&) = delete; 
    Logger(Logger&&) = delete;
};

#endif
