/*
 * @version: 
 * @Author: zsq 1363759476@qq.com
 * @Date: 2023-04-06 11:06:48
 * @LastEditors: zsq 1363759476@qq.com
 * @LastEditTime: 2024-10-01 15:55:40
 * @FilePath: /Linux_nc/WebServer/WebServer_main/main.cpp
 * @Descripttion: 
 */

// addsig 处理信号
// 程序模拟proactor模式

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>

#include "http_connt.h" 
#include "threadpools.h"

#define MAX_FD 65536 // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000 // 监听的最大的事件数量

// using namespace std;

// 添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool one_shot); 
// 从epoll中删除文件描述符
extern void removedfd(int epollfd, int fd);

// 处理SIGPIPE信号，若不处理就会终止进程
void addsig(int sig, void(handler)(int)) {                   
    // struct straction sa; 
    struct sigaction sa;            // 信号捕捉函数
    memset(&sa, '\0', sizeof(sa));  // memset(变量名, 起始字节, 结束字节)清空数据
    sa.sa_handler = handler; // 回调函数：handler(任意名)，被函数指针指向（管理）
    sigfillset(&sa.sa_mask); // 将信号集中的所有标志位置为1，设置临时阻塞信号集，临时都是阻塞的
                            
    assert(sigaction(sig, &sa, NULL) != -1); 
}

int main(int argc, char *argv[]) {  // 程序模拟的是proacter模式
    if (argc <= 1) {                // 端口号没有输入的错误提示
        // cout << "usage:" << basename(argv[0]) << "port_number" << endl;  
        printf( "usage: %s port_number\n", basename(argv[0]));
        exit(-1);
    }

    // int port = *argv[1]; 
    int port = atoi(argv[1]); 


    addsig(SIGPIPE, SIG_IGN); // 一端断开连接，另外一端还写数据的话，就会产生SIGPIPE信号

    // 创建线程池
    threadpool<http_conn> *pool =nullptr;
    try {
        pool = new threadpool<http_conn>;
    } catch (...) {
        return 1;
    }

    // users 是一个 http_conn 类型的数组，用于管理所有客户端连接的状态
    http_conn * users = new http_conn[MAX_FD]; 

    // client_data * users_timer = new client_data[MAX_FD]; //定时器 心跳包

    // 1.创建一个套接字
    int listenfd = socket(AF_INET, SOCK_STREAM, 0); 
    
    // 2.绑定，将 fd文件描述符 和本地的IP + 端口进行绑定
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port); // 端口

    int reuse = 1; // 要在绑定之前设置端口复用
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)); // 端口复用

    int ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    if(ret == -1) {
        perror("bind");
        exit(-1);
    }

    // 3.监听
    ret = listen(listenfd, 5);
    if(ret == -1) {
        perror("listen"); 
        exit(-1);
    }
    
    // 将监听的文件描述符相关的检测信息添加到epoll实例中
    int epollfd = epoll_create(5); // epollfd是用于epoll实例的文件描述符，用于监控多个文件描述符上的I/O事件。
    epoll_event events[MAX_EVENT_NUMBER]; 

    addfd(epollfd, listenfd, false); // 监听的文件描述符不用EPOLLONESHOT，因为本来就只触发一次，接收数据的文件描述符需要EPOLLONESHOT
    // 一般监听文件描述符listenfd不设置为边沿触发EPOLLET

    // 静态数量，默认是0
    http_conn::m_epollfd = epollfd;

    // 下面相当于是muduo网络库中的Reactor反应堆和事件分发器Demultiplex
    while (true) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1); // events[i]相当于是muduo网络库中的Event事件
        if ((number < 0) && errno != EINTR) { // 在中断情况下会出现EINTR
            printf("epoll failure\n");
            break;
        }

        // 遍历循环事件数组
        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd; // sockfd是在处理epoll事件时从事件数组中获取的文件描述符，用于表示正在处理的连接
            // 取出来一个事件，看 sockfd 是监听套接字 listenfd 还是一个已经建立连接的客户端套接字 connfd

            if (sockfd == listenfd) { // 客户端请求连接
                // 4.连接
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength); // socket通信函数之一，阻塞，直到有客户端连接
                // client_address是传出参数，记录了连接成功后客户端的地址信息（ip，port）
                
                if (connfd < 0) {
                    printf("errno is: %d\n", errno);
                    continue;
                } 

                if(http_conn::m_user_count >= MAX_FD) { // MAX_FD最大的文件描述符个数
                    // 给客户端写一个信息：服务器内部正忙  优化
                    close(connfd);
                    continue;
                }

                // 将新的客户的数据初始化，放到数组中
                users[connfd].init(connfd, client_address);

            } else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) { // 出现下列事件，
                // 对方异常断开或者错误等事件 EPOLLRDHUP代表对端断开连接
                users[sockfd].close_conn(); 

            } else if(events[i].events & EPOLLIN) {     // epoll检测到读事件，有读事件发生
                if (users[sockfd].read()) {             // 数据过来了，一次性把数据处理完
                    pool->append(users + sockfd);       // 添加到线程池队列当中，将事件event添加到EventLoop事件循环中
                } else { 
                    users[sockfd].close_conn();       
                }
                
            } else if(events[i].events & EPOLLOUT) {    // 有写事件发生  写缓冲区没有满，就可以写
                if (!users[sockfd].write()) {           // 一次性把数据写完，把数据写出去，发送数据
                    // 写失败，断开连接
                    users[sockfd].close_conn();
                }
            }
        }
    }

    close(epollfd);
    close(listenfd); // #include <unistd.h>
    delete[] users;
    delete pool;

    return 0;
}
