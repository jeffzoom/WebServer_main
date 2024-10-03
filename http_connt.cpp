/*
 * @version: 
 * @Author: zsq 1363759476@qq.com
 * @Date: 2023-04-06 11:07:23
 * @LastEditors: zsq 1363759476@qq.com
 * @LastEditTime: 2024-10-01 16:03:47
 * @FilePath: /Linux_nc/WebServer/WebServer_main/http_connt.cpp
 * @Descripttion: 
 */

#include "http_connt.h"

using namespace std;

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

const char* doc_root = "/home/nowcoder/Linux_nc/WebServer/myWebServer/resources";
// const char* doc_root = "/home/nowcoder/webserver/resources";

int http_conn::m_epollfd = -1; // 所有的socket上的事件都被注册到同一个epol
// 所有的客户数
int http_conn::m_user_count = 0; // 统计用户的数量

// 设置文件描述符为非阻塞
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL); // 获取文件描述符状态flag
    int new_option = old_option | O_NONBLOCK; // 设置为非阻塞
    fcntl(fd, F_SETFL, new_option); 

    return old_option; 
}

// 向epoll中添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    
    event.events = EPOLLIN | EPOLLRDHUP; // read()测试，用默认的水平触发
    if (one_shot) { 
        // 防止同一个通信被不同的多个线程处理
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    setnonblocking(fd); // 设置文件描述符为非阻塞
}

// 从epoll中移除监听的文件描述符，从epollfd中移除fd
void removedfd(int epollfd, int fd) {
    // epoll_ctl(epollfd, fd, EPOLL_CTL_DEL, nullptr); // 优化，nullptr应该也行吧
    epoll_ctl(epollfd, fd, EPOLL_CTL_DEL, 0);
    close(fd);
}

// 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP; // ev应该是自己的事件的值，或了之后就达到了修改文件描述符的目的
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event ); // 对epoll实例进行管理，添加删除修改文件描述符信息
    //                            MOD修改
}

// 关闭连接
void http_conn::close_conn() {
    printf("Close connection\n");
    LOG_INFO("Close connection");
    if (m_sockfd != -1) {
        removedfd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 初始化连接其余的信息
void http_conn::init() { 

    bytes_to_send = 0;
    bytes_have_send = 0;

    m_read_idx = 0;             // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置
    m_checked_idx = 0;          // 当前正在分析的字符在读缓冲区中的位置
    m_start_line = 0;           // 当前正在解析的行的起始位置
    m_content_length = 0;
    m_host = 0;
    m_write_idx = 0;

    m_check_state = CHECK_STATE_REQUESTLINE;    // 初始化状态为解析请求首行
    m_method = GET;             // 默认请求方式为GET

    m_url = 0;                  // 客户请求的目标文件的文件名
    m_version = 0;              // HTTP协议版本号，仅支持HTTP1.1
    m_linger = false;           // HTTP请求是否要求保持连接
                                // 默认不保持链接  Connection : keep-alive保持连接

    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, READ_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}   

void http_conn::init(int sockfd, const sockaddr_in &client_address) {
    printf("Init\n");
    LOG_INFO("Init");
    m_sockfd = sockfd;
    m_address = client_address;

    int reuse = 1;      // 要在绑定之前设置端口复用
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)); // 端口复用

    // 将数据传输的文件描述符加入到epoll中
    addfd(m_epollfd, m_sockfd, true);   //epoll实例的文件描述符，要检测的文件描述符
    m_user_count++;                     // 总用户数+1

    init();
}

// 非阻塞的读 一次性读完数据
bool http_conn::read() {
    printf("Read data\n");
    LOG_INFO("Read data");
    if (m_read_idx >= READ_BUFFER_SIZE) { 
        return false;
    }

    // 读取到的字节
    int bytes_read = 0;
    while (true) {
        // recv接收请求数据，通过m_sockfd文件描述符数数据，将读到的数据保存到m_read_buf
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                break;
            }
            return false;
        } else if (bytes_read == 0) {
            return false;
        }
        m_read_idx += bytes_read; // 索引
    }

    printf("Read the data:\n%s\n", m_read_buf); // 读到了数据
    // LOG_INFO("Read the data:");
    LOG_INFO("Read the data:\n%.*s", static_cast<int>(strlen(m_read_buf)), m_read_buf);

    return true;
}


// 主状态机，解析HTTP请求  
http_conn::HTTP_CODE http_conn::process_read() {
    LOG_INFO("Process read");
    printf("Process read\n");
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char * text = 0;

    while ( ((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
            || (line_status = parse_line()) == LINE_OK) {
        
        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_idx; 
        printf("Get one http line: %s\n", text); // 获得一行http信息
        LOG_INFO("Get one http line: %s", text);

        switch (m_check_state) {

            case CHECK_STATE_REQUESTLINE:   // 正在分析请求行（第一行）GET /index.html HTTP/1.1
                ret = parse_request_line(text); // 解析正常的话，m_check_state会变成CHECK_STATE_HEADER
                if (ret == BAD_REQUEST) { 
                    return BAD_REQUEST;     // 表示客户请求语法错误
                }
                break;

            case CHECK_STATE_HEADER:  // 正在分析请求头（第一行下面的，回车符和换行符上面的）Connection: Content-Length: Host:
                ret = parse_headers(text); // 解析正常的话，m_check_state会变成CHECK_STATE_CONTENT
                if (ret == BAD_REQUEST) { 
                    return BAD_REQUEST;         // 表示客户请求语法错误
                } else if (ret == GET_REQUEST) {// 当请求头被全部解析出来，后面是换行\r时，认为获得了一个完成的客户请求
                    return do_request();        // 解析具体的请求信息
                }
                break;
        
            case CHECK_STATE_CONTENT:  // 当前正在解析请求体
                ret = parse_content(text);
                if (ret == GET_REQUEST) {   // 当请求头被全部解析出来，后面是换行\r时，认为获得了一个完成的客户请求
                    return do_request();    // 解析具体的请求信息
                }                           // 很多东西都没有判断，优化
                line_status = LINE_OPEN; // 从状态
                break;
            
            default: 
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST; // 主状态机
}

// 下面这一组函数被process_read调用以分析HTTP请求
// 解析HTTP请求首行，获得请求方法，目标URL，HTTP版本
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) { 

    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t"); // 判断第二个参数中的字符哪个在text中最先出现，\t是空格，返回\t所在的位置
    if (! m_url) { 
        printf("Error:Failed to parse the first line of HTTP request\n"); // 解析HTTP请求首行失败
        LOG_INFO("Error:Failed to parse the first line of HTTP request");
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';    // 置位空字符，字符串结束符，相当于GET是一个字符串，然后/index.html HTTP/1.1是下一个字符串

    char* method = text;
    if ( strcasecmp(method, "GET") == 0 ) { // 判断字符串是否相等的函数,忽略大小写比较,相等返回0
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    // /index.html\0HTTP/1.1
    *m_version++ = '\0';

    if (strcasecmp( m_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }
    /**
     * http://192.168.110.129:10000/index.html
    */
    if (strncasecmp(m_url, "http://", 7) == 0 ) { // 比较两个字符串的前n个字符
        m_url += 7; //  192.168.110.129:10000/index.html
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr(m_url, '/');   // /index.html，查找字符串中的一个字符，并返回该字符在字符串中第一次出现的位置。
    }
    if ( !m_url || m_url[0] != '/' ) {  // 优化，这个判断比较简单，判断应该是有很多的条件
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER; // 检查状态变成检查头
    return NO_REQUEST;
}

// 解析请求头
http_conn::HTTP_CODE http_conn::parse_headers(char* text) { 

    // 遇到空行，表示头部字段解析完毕
    if(text[0] == '\0') {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 ) { // m_content_length是http请求中的一个参数，如果有请求体，则m_content_length不为0，就是在请求头中有content_length后面跟一个数值，比如说是1000，说明请求体有1000个字符
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) { // strncasecmp相等返回0
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11; // 指向空格 char *text = " keep-alive"
        // 从str1的第一个元素开始往后数，看str1中是不是连续往后每个字符都在str2中可以找到，到第一个不在gruop的元素为止，可以无序
        text += strspn(text, " \t"); // 返回 str1 中第一个不在字符串 str2 中出现的字符下标，也就是1，指向字母"k"
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text); // 参数text所指向的字符串转换为一个长整数（类型为 long int 型）。
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        // 处理Host头部字段 Host: 192.168.18.176:10000
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else { // 好的服务器应该对所有的请求头都由解析
        printf("oop! unknow header: %s\n", text); // 未知的请求头
        LOG_INFO("oop! unknow header: %s", text);
    }
    return NO_REQUEST;
}

// 解析请求体
http_conn::HTTP_CODE http_conn::parse_content(char* text) { 
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 解析一行，判断依据是\r\n
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) { 
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;       
            } else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';     // 把\r\n变成\0
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;         // 读取到一个完整的行
            }
            return LINE_BAD;            // 行出错
        } else if (temp == '\n') {      
             if((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) { //m_checked_idx > 1是防止后面-1，变成-1会数组内存操作错误
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 解析具体的请求信息
/*
    当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
    如果目标文件存在、对所有用户可读，且不是目录，
    则使用mmap将其映射到内存地址m_file_address处，并告诉调用者获取文件成功
*/
// do_request()负责处理完整的 HTTP 请求并生成相应的 HTTP 响应。
// 这个函数在请求解析完成后调用，通过访问请求的目标资源（例如文件），并生成适当的响应数据。
http_conn::HTTP_CODE http_conn::do_request() {

    // "/home/nowcoder/Linux_nc/WebServer/myWebServer/resources"
    // m_real_file一开始是空的
    strcpy(m_real_file, doc_root); // 把 str2 所指向的字符串复制到 str1 中，m_real_file=/home/nowcoder/Linux_nc/WebServer/myWebServer/resources
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1); // m_url="/index.html"
    // 把 src2 所指向的字符串复制到 src1，最多复制 n 个字符。当 src2 的长度小于 n 时，src1 的剩余部分将用空字节填充
    // strncpy 没有自动加上终止符\0的？需要手动加上不然会出问题的？
    // m_real_file=/home/nowcoder/Linux_nc/WebServer/myWebServer/resources/index.html

    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    // stat()用来将参数file_name 所指的文件状态, 复制到参数buf 所指的结构中。file_name是文件路径（名）
    // 返回值：执行成功则返回0，失败返回-1，错误代码存于errno。
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }

    // 判断访问权限 牛逼
    if ( !(m_file_stat.st_mode & S_IROTH)) { // S_IROTH:其他用户具可读取权限 
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST; // 如果客户端请求的是目录而不是文件
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY); // O_RDONLY:文件只读
    // 关键 创建内存映射 把网页的数据映射到地址上 
    // PROT_READ读权限 MAP_PRIVATE内存映射区文件和原文件不同步 fd是需要映射的文件的文件描述符
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作，写数据的时候用
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size); //释放内存映射
        m_file_address = 0;
    }
}

// 非阻塞的写 一次性写完数据
bool http_conn::write() { 
    printf("Write data\n\n");
    LOG_INFO("Write data\n");
    // m_write_buf 写缓冲区中待发送的字节数
    // m_file_address 客户请求的目标文件被mmap映射到内存中的起始位置
    // 要写两块数据，请求体映射的地址m_file_address和响应的数据m_write_buf

    int temp = 0;
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd(m_epollfd, m_sockfd, EPOLLIN); 
        init();
        return true;
    }

    while(1) {
        // 分散写 有多快内存 不是连续的，可以把多块内存的数据一起写出去

        // 第一个参数fd是个文件描述符，第二个参数是指向iovec数据结构的一个指针，
        // 其中iov_base为缓冲区首地址，iov_len为缓冲区长度，第三个参数iovcnt指定了iovec的个数。
        // writev函数调用成功时返回写的总字节数，失败时返回-1并设置相应的errno
        temp = writev(m_sockfd, m_iv, m_iv_count); // writev就把数据写出去了

        if (temp <= -1) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if(errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp; // 已经发送的字节数
        bytes_to_send -= temp;   // 将要发送的数据的字节数，也就是还没发送的字节数

        if (bytes_have_send >= m_iv[0].iov_len) { // 已经发送的字节数大于等于第一块数据的长度，发送第二块数据
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else { // 继续发送第一块数据
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0) {
            // 没有数据要发送了
            unmap(); // 全部写完之后要释放掉
            modfd(m_epollfd, m_sockfd, EPOLLIN); // 重置监听事件

            if (m_linger) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }
    return true;
}

// va_list 是在C语言中解决变参问题的一组宏 变参问题是指参数的个数不定，可以是传入一个参数也可以是多个;可变参数中的每个参数的类型可以不同,也可以相同;
// add_response专门往写缓冲中写入待发送的数据，数据写到m_write_buf这个数组当中
// 字符串" "的类型就是const char*
bool http_conn::add_response(const char* format, ...) { // const char* format是格式，...是可变参数，类似ptintf("%s %d %s", "HTTP/1.1", 1, "test")，后面的"HTTP/1.1", 1, "test"就是可变参数
    if(m_write_idx >= WRITE_BUFFER_SIZE) { // 写缓冲区中待发送的字节数大于写缓冲区的大小
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);     // 指针指向 format 之后的参数

    // vsnprintf()用于向一个字符串输入格式化字符串，且可以限定输入的格式化字符串的最大长度 C99或者C++11
    // s索引 maxlen大小 format格式 arg可变长度参数列表 ，把数据写到m_write_buf这个数组当中
    // 限定最多打印到缓冲区s的字符的个数为maxlen-1个，因为vsnprintf还要在结果的末尾追加\0。如果格式化字符串长度大于n-1，则多出的部分被丢弃。如果格式化字符串长度小于等于n-1，则可以格式化的字符串完整打印到缓冲区s。一般这里传递的值就是s缓冲区的长度。
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        return false;               // 写不下，返回false
    }
    m_write_idx += len;
    va_end(arg_list); // 最后用VA_END宏结束可变参数的获取，标准格式
    return true;
}

// 添加响应状态行，也就是响应首行
bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 添加响应头部
bool http_conn::add_headers(int content_len) { 
    add_content_length(content_len);    // Content-Length:
    add_content_type();                 // Content-Type:
    add_linger();                       // Connection:
    add_blank_line();                   // 加入空行
}

// 简化了，有些头没有相关的信息，追加响应正文长度
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}

// 追加响应正文类型，发送回去的内容，一个完整的服务器应该根据不同的资源，比如请求的是MP3文件，Content-Type就不是text/html
bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 追加Connection头，如果true就是keep-alive
bool http_conn::add_linger() {
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

// 加入空行
bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

// 实际用途：往写缓冲中写入HTTP响应的一些状态信息
bool http_conn::add_content(const char* content) {
    return add_response("%s", content);
}

// 填充HTTP应答
// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
// 为了生成响应报文
bool http_conn::process_write(HTTP_CODE ret) { 
    printf("Process write\n");
    LOG_INFO("Process write");
    switch (ret)
    {
        case INTERNAL_ERROR: // 表示服务器内部错误
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (! add_content(error_500_form)) {
                return false;
            }
            break;
        case BAD_REQUEST: // 表示客户请求语法错误
            add_status_line(400, error_400_title);
            add_headers( strlen(error_400_form));
            if (! add_content(error_400_form)) {
                return false;
            }
            break;
        case NO_RESOURCE: // 请求不完整，需要继续读取客户数据
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (! add_content(error_404_form)) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST: // 表示客户对资源没有足够的访问权限
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (! add_content(error_403_form)) {
                return false;
            }
            break;
        case FILE_REQUEST:                          // 文件请求完整,获取文件成功
            add_status_line(200, ok_200_title);     // 添加状态行
            add_headers(m_file_stat.st_size);       // 添加头 
            m_iv[0].iov_base = m_write_buf;         // 获取第一块数据，m_write_buf是第一块内存的起始位置
            m_iv[0].iov_len = m_write_idx;          // m_write_buf写缓冲区中待发送的字节数
            m_iv[1].iov_base = m_file_address;      // 获取第二块数据，m_file_address是第二块内存的起始位置
            m_iv[1].iov_len = m_file_stat.st_size;  // m_file_address中待发送的大小
            m_iv_count = 2;                         // 一共两块内存

            bytes_to_send = m_write_idx + m_file_stat.st_size; // 将要发送的数据的字节数
            return true;
            // m_write_buf: 响应头部（Header）
            // 这部分包含了HTTP响应的状态行、响应头字段及其值等。
            // 例如：状态行包含HTTP版本、状态码和状态描述（如HTTP/1.1 200 OK），响应头字段包括Content-Type、Content-Length等。
            // 在代码中，响应头部的内容被写入到m_write_buf中。
            // m_file_address: 响应体（Body）
            // 这部分是实际的内容，如HTML页面、图片或文件数据等。
            // 在代码中，响应内容是通过内存映射技术（mmap）映射到内存中的文件数据，指针为m_file_address。

        default:
            return false;
    }

    // 感觉这个是无用代码啊，根本就走不到这个地方
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

/*
    逻辑：当有请求发送到服务器时，一次性把数据读完users[sockfd].read()，之后调用
    pool->append(users + sockfd)把任务追加到线程池中，然后线程池就会执行run()，
    不断的循环从队列里面去取，取到一个就执行任务的process()，执行process()就是做业务处理，
    解析，然后生成响应，就是生成响应的数据，数据有了之后，如果要写数据的话，满足事件events[i].events & EPOLLOUT
    就users[sockfd].write()一次性把数据写完，这个就是整体流程

    read()读数据 -> process_read()解析数据 -> process_write()打包数据 -> write()发送数据
    read()和write()其实是在main主线程运行的，没有加入到任务队列

    真正的业务处理：read()，write()，还有就是process()
*/
// 处理客户端请求
// 由线程池中的工作线程调用，这是处理HTTP请求的入口
// 下面相当于是muduo网络库中的Eventhandler事件处理器，handler就是回调函数的意思
void http_conn::process() {

    printf("Process data\n"); // 处理数据
    LOG_INFO("Process data");

    // 解析http请求，这里就是做业务逻辑的，处理一行一行的http请求，请求头什么的
    // 用状态机
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN); // 修改socket的事件，监听读事件，继续去读
        return;
    }

    // 生成响应
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);   // 在addfd中用了one_shot事件，
                                            // 所以操作完之后每次必须重新添加这个事件
                                            // EPOLLOUT是写事件
}



