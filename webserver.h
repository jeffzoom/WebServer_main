/*
 * @version: 
 * @Author: zsq 1363759476@qq.com
 * @Date: 2024-03-04 23:01:16
 * @LastEditors: zsq 1363759476@qq.com
 * @LastEditTime: 2024-10-01 15:56:23
 * @FilePath: /Linux_nc/WebServer/WebServer_main/webserver.h
 * @Descripttion: 
 */
#ifndef _WEBSERVER_H_
#define _WEBSERVER_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include <string.h>

#include "lst_timer.h"
#include "threadpools.h"
#include "http_connt.h"

// const int MAX_FD = 65536;           //最大文件描述符
// const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    // void init(int port , string user, string passWord, string databaseName,
    //           int log_write , int opt_linger, int trigmode, int sql_num,
    //           int thread_num, int close_log, int actor_model);

    
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    
public:
    
    //定时器相关
    client_data *users_timer;
    Utils utils;
};
#endif
