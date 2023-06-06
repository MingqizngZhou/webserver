#include "http_conn.h"
#include "../log/log.h"
#include <fstream>
#include <map>
#include <string>
#include <mysql/mysql.h>

http_conn::http_conn(){}

http_conn::~http_conn(){}

int http_conn::m_epoll_fd = -1;     // 类中静态成员需要外部定义
int http_conn::m_user_cnt = 0;  
int http_conn::m_request_cnt = 0; 
HeapTimer http_conn::m_timer_heap;

#define connfdET //边缘触发非阻塞
// #define connfdLT //水平触发阻塞

#define listenfdET //边缘触发非阻塞
// #define listenfdLT //水平触发阻塞

// locker http_conn::m_timer_lst_locker;

// 网站的根目录
const char* doc_root = "/home/zmq/Webserver/webserver/resources";
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

//将表中的用户名和密码放入map
std::map<std::string, std::string> users;
locker m_lock;

void http_conn::initmysql_result(connection_pool *connPool)
{
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        std::string temp1(row[0]);
        std::string temp2(row[1]);
        users[temp1] = temp2;
        // std::cout << temp1 << " " << temp2 << std::endl;
    }
}

// 设置文件描述符为非阻塞
void set_nonblocking(int fd){
    int flag = fcntl(fd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);
}

// 添加需要监听的文件描述符到epoll中
void addfd(int epoll_fd, int fd, bool one_shot){
    epoll_event event;
    event.data.fd = fd;
    // if(et){
    //     event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;  // 对所有fd设置边沿触发，但是listen_fd不需要，可以另行判断处理
    // }else{
    //     event.events = EPOLLIN | EPOLLRDHUP;    // 默认水平触发    对端连接断开触发的epoll 事件包含 EPOLLIN | EPOLLRDHUP挂起，不用根据返回值判断，直接通过事件判断异常断开
    // }

    #ifdef connfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    #endif

    #ifdef connfdLT
        event.events = EPOLLIN | EPOLLRDHUP;
    #endif

    #ifdef listenfdET
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    #endif

    #ifdef listenfdLT
        event.events = EPOLLIN | EPOLLRDHUP;
    #endif

    if(one_shot){
        event.events |= EPOLLONESHOT;       // 注册为 EPOLLONESHOT事件，防止同一个通信被不同的线程处理
    }
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);

    // 设置文件描述符为非阻塞（epoll ET模式）
    set_nonblocking(fd);
}


// 从epoll中删除文件描述符
void rmfd(int epoll_fd, int fd){
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}


// 在epoll中修改文件描述符，重置socket上EPOLLONESHOT事件，确保下次可读时，EPOLLIN 事件被触发
void modfd(int epoll_fd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    // event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    #ifdef connfdET
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    #endif

    #ifdef connfdLT
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    #endif

    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
}


// 初始化新的连接
void http_conn::init(int sock_fd, const sockaddr_in& addr){ 
    m_sock_fd = sock_fd;    // 套接字
    m_addr = addr;          // 客户端地址

    // 设置端口复用
    int reuse = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    // 添加sock_fd到epoll对象中
    addfd(m_epoll_fd, sock_fd, true);
    ++m_user_cnt;

    char ip[16] = "";
    const char* str = inet_ntop(AF_INET, &addr.sin_addr.s_addr, ip, sizeof(ip));
    // EMlog(LOGLEVEL_INFO, "The No.%d user. sock_fd = %d, ip = %s.\n", m_user_cnt, sock_fd, str);
    LOG_INFO("The No.%d user. sock_fd = %d, ip = %s.", m_user_cnt, sock_fd, str);
    Log::get_instance()->flush();

    init();             // 初始化其他信息，私有

}

// 初始化连接之外的其他信息
void http_conn::init(){
    mysql = NULL;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_linger = false;                       // 默认不保持连接
    m_content_len = 0;
    m_host = 0;

    m_check_stat = CHECK_STATE_REQUESTLINE; // 初始化状态为正在解析请求首行
    m_checked_idx = 0;                      // 初始化解析字符索引
    m_line_start = 0;                       // 行的起始位置
    m_rd_idx = 0;                           // 读取字符的位置

    m_write_idx = 0;
    bytes_have_send = 0;
    bytes_to_send = 0;

    cgi = 0;

    bzero(m_rd_buf, RD_BUF_SIZE);           // 清空读缓存
    bzero(m_write_buf, WD_BUF_SIZE);        // 清空写缓存
    bzero(m_real_file, FILENAME_LEN);       // 清空文件路径
}

// 关闭连接
void http_conn::conn_close(){
    if(m_sock_fd != -1){
        --m_user_cnt;   // 客户端数量减一
        // EMlog(LOGLEVEL_INFO, "closing fd: %d, rest user num :%d\n", m_sock_fd, m_user_cnt);
        LOG_INFO("closing fd: %d, rest user num :%d", m_sock_fd, m_user_cnt);
        Log::get_instance()->flush();
        rmfd(m_epoll_fd, m_sock_fd);    // 移除epoll检测,关闭套接字
        m_sock_fd = -1;
    }
}

// 循环读取客户数据，直到无数据可读 或 关闭连接
bool http_conn::read(){
    if(timer) {             // 更新超时时间

        // time_t curr_time = time( NULL );
        // timer->expire = curr_time + 3 * TIMESLOT;
        m_timer_heap.adjust( timer, 3 * TIMESLOT );
    }
    
    if(m_rd_idx >= RD_BUF_SIZE) return false;   // 超过缓冲区大小

    int bytes_rd = 0;

#ifdef connfdLT
    bytes_rd = recv(m_sock_fd, m_rd_buf + m_rd_idx, RD_BUF_SIZE - m_rd_idx, 0);
    m_rd_idx += bytes_rd;

    if (bytes_rd <= 0)
    {
        return false;
    }

    return true;
#endif

#ifdef connfdET
    while(true){    // m_sock_fd已设置非阻塞
        bytes_rd = recv(m_sock_fd, m_rd_buf + m_rd_idx, RD_BUF_SIZE - m_rd_idx, 0);   // 第二个参数传递的是缓冲区中开始读入的地址偏移
        if(bytes_rd == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                break;      // 非阻塞读取，没有数据了
            }
            return false;   // 读取错误，调用conn_close()
        }else if(bytes_rd == 0){    
            return false;   // 对方关闭连接，调用conn_close()
        }
        m_rd_idx += bytes_rd;   // 更新下一次读取位置
    }

    ++m_request_cnt;

    // EMlog(LOGLEVEL_INFO, "sock_fd = %d read done. request cnt = %d\n", m_sock_fd, m_request_cnt);    // 全部读取完毕
    LOG_INFO("sock_fd = %d read done. request cnt = %d", m_sock_fd, m_request_cnt);
    Log::get_instance()->flush();
    
    return true;
#endif
}


// 主状态机 解析HTTP请求
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_stat = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char* text = 0;
    while((m_check_stat == CHECK_STATE_CONTENT && line_stat == LINE_OK) // 主状态机正在解析请求体，且从状态机OK，不需要一行一行解析
        || (line_stat = parse_one_line()) == LINE_OK){                  // 从状态机解析到一行数据

        // 获取一行数据
        text = get_line();
        LOG_INFO("%s", text);
        Log::get_instance()->flush();
        m_line_start = m_checked_idx;           // 更新下一行的起始位置

        // EMlog(LOGLEVEL_DEBUG, ">>>>>> %s\n", text);
        LOG_DEBUG(">>>>>> %s", text);
        Log::get_instance()->flush();

        switch(m_check_stat){
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }

            case CHECK_STATE_HEADER:
            {
                ret = parse_request_headers(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }else if(ret == GET_REQUEST){
                    return do_request();        // 解析具体的请求信息
                }
                break;
            }

            case CHECK_STATE_CONTENT:
            {
                ret = parse_request_content(text);
                if(ret == GET_REQUEST){
                    return do_request();        // 解析具体的请求信息
                }
                line_stat = LINE_OPEN;          // != GET_REQUEST
                break;
            }

            default:
            {
                return INTERNAL_ERROR;          // 内部错误
            }
        }       
    }
    return NO_REQUEST;   // 数据不完整
}  

// 解析请求首行，获得请求方法，目标URL，HTTP版本             
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");   // 找到第一次出现空格或者\t的下标
    if(!m_url) return BAD_REQUEST;
    *m_url = '\0';      // GET\0/index.html HTTP/1.1，此时text到\0结束，表示 GET\0
    m_url++;            // /index.html HTTP/1.1

    char* method = text;    // GET\0
    if(strcasecmp(method, "GET") == 0){
        m_method = GET;
    }else if(strcasecmp(method, "POST") == 0){
        m_method = POST;;
        cgi = 1;
    }else{
        return BAD_REQUEST; // 非GET请求方法
    }

    // /index.html HTTP/1.1
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if(!m_version) return BAD_REQUEST;
    *m_version = '\0';  // /index.html\0HTTP/1.1，此时m_url到\0结束，表示 /index.html\0
    m_version++;        // HTTP/1.1
    m_version += strspn(m_version, " \t");
    // if(strcasecmp(m_version, "HTTP/1.1") != 0) return BAD_REQUEST;  // 非HTTP1.1版本，压力测试时为1.0版本，忽略改行

    // 可能出现带地址的格式 http://192.168.15.128.1:9999/index.html
    if(strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7;                 // 192.168.15.128.1:9999/index.html
        m_url = strchr(m_url, '/'); // /index.html
    }
    // 可能出现带地址的格式 https://192.168.15.128.1:9999/index.html
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if(!m_url || m_url[0] != '/'){
        return BAD_REQUEST;
    }

    //当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");

    m_check_stat = CHECK_STATE_HEADER;  // 主状态机状态改变为检查请求头部
    return NO_REQUEST;                  // 请求尚未解析完成

} 

// 解析请求头部    
http_conn::HTTP_CODE http_conn::parse_request_headers(char* text){      // 在枚举类型前加上 `http_conn::` 来指出它的所属作用域
    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_len != 0 ) {     // 请求体有内容
            m_check_stat = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );      // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_len = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
        #ifdef COUT_OPEN
            // EMlog(LOGLEVEL_DEBUG,"oop! unknow header: %s\n", text );
            LOG_INFO("oop! unknow header: %s", text);
            Log::get_instance()->flush();
        #endif   
    }
    return NO_REQUEST;
}  

// 解析请求体  
http_conn::HTTP_CODE http_conn::parse_request_content(char* text){
    if ( m_rd_idx >= ( m_content_len + m_checked_idx ) )    // 读到的数据长度 大于 已解析长度（请求行+头部+空行）+请求体长度
    {                                                       // 数据被完整读取
        text[ m_content_len ] = '\0';   // 标志结束
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}    


// 从状态机解析一行数据，判断\r\n
http_conn::LINE_STATUS http_conn::parse_one_line(){
        char temp;
        for( ; m_checked_idx < m_rd_idx; ++m_checked_idx){  // 检查的索引 小于 读到的索引
            
            temp = m_rd_buf[m_checked_idx];                 // 遍历缓冲区字符

            if(temp == '\r'){
                if(m_checked_idx + 1 == m_rd_idx){          // 回车符是已经读到的最后一个字符，表示行数据尚不完整
                    return LINE_OPEN;                       
                }else if(m_rd_buf[m_checked_idx+1] == '\n'){// 当前检查到 \r\n
                    m_rd_buf[m_checked_idx++] = '\0';       // \r 变 \0  idx++
                    m_rd_buf[m_checked_idx++] = '\0';       // \n 变 \0  idx++，到下一行的起始位置
                    return LINE_OK;
                }
                return LINE_BAD;                            // 语法有问题

            }else if(temp == '\n'){
                if(m_checked_idx > 1 && m_rd_buf[m_checked_idx-1] == '\r'){  // 上一次读取的数据行不完整，刚好\r \n 在不同数据的结尾和开头的情况
                    m_rd_buf[m_checked_idx-1] = '\0';       // \r 变 \0
                    m_rd_buf[m_checked_idx++] = '\0';       // \n 变 \0  idx++，到下一行的起始位置
                    return LINE_OK;
                }
                return LINE_BAD;  
            }
            
        }   
        return LINE_OPEN;   // 没有到结束符，数据尚不完整
}         


// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request(){
    // "/home/zmq/Webserver/webserver/resources"
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    const char *p = strrchr(m_url, '/');

    //处理cgi
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {

        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        //同步线程登录校验
        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            // std::cout << sql_insert << std::endl;

            if (users.find(name) == users.end())
            {

                m_lock.lock();
                int res = mysql_query(mysql, sql_insert); 
                users.insert(std::pair<std::string, std::string>(name, password));
                m_lock.unlock();

                if (!res) {
                    strcpy(m_url, "/log.html");
                    LOG_INFO("Mysql : %s", sql_insert);
    		        Log::get_instance()->flush();
                }
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
    }

    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/index.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
         
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    // 创建内存映射
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}  

// 对内存映射区执行munmap操作
void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}


// 写HTTP响应数据
bool http_conn::write(){
    int temp = 0;

    if(timer) {             // 更新超时时间

        // time_t curr_time = time( NULL );
        // timer->expire = curr_time + 3 * TIMESLOT;
        m_timer_heap.adjust( timer, 3 * TIMESLOT );
    }
    // EMlog(LOGLEVEL_INFO, "sock_fd = %d writing %d bytes. request cnt = %d\n", m_sock_fd, bytes_to_send, m_request_cnt); 
    LOG_INFO("sock_fd = %d writing %d bytes. request cnt = %d", m_sock_fd, bytes_to_send, m_request_cnt);
    Log::get_instance()->flush();
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epoll_fd, m_sock_fd, EPOLLIN ); 
        init();
        return true;
    }

    while(1) {
        // 分散写   m_write_buf + m_file_address
        temp = writev(m_sock_fd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epoll_fd, m_sock_fd, EPOLLOUT );
                return true;
            }
            unmap();        // 释放内存映射m_file_address空间
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;

        if (bytes_have_send >= m_iv[0].iov_len){    // 发完头部了
            m_iv[0].iov_len = 0;                    // 更新两个发送内存块的信息
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);    // 已经发了部分的响应体数据
            m_iv[1].iov_len = bytes_to_send;
        }else{                                      // 还没发完头部
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0){
            // 没有数据要发送了
            unmap();
            modfd(m_epoll_fd, m_sock_fd, EPOLLIN);

            if (m_linger){
                init();
                return true;
            }else{
                return false;
            }
        }
    }
    // printf("write done.\n");
    LOG_INFO("write done.");
    Log::get_instance()->flush();
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ... ) {
    if( m_write_idx >= WD_BUF_SIZE ) {      // 写缓冲区满了
        return false;
    }
    va_list arg_list;                       // 可变参数，格式化文本
    va_start( arg_list, format );           // 添加文本到到写缓冲区m_write_buf中
    int len = vsnprintf( m_write_buf + m_write_idx, WD_BUF_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WD_BUF_SIZE - 1 - m_write_idx ) ) {
        return false;                       // 没写完，已经满了
    }
    m_write_idx += len;                     // 更新下次写数据的起始位置
    va_end( arg_list );
    LOG_INFO("request:%s", m_write_buf);
    Log::get_instance()->flush();
    return true;
}

// 添加状态码（响应行）
bool http_conn::add_status_line( int status, const char* title ) {
    // EMlog(LOGLEVEL_DEBUG,"<<<<<<< %s %d %s\r\n", "HTTP/1.1", status, title);   
    LOG_DEBUG("<<<<<<< %s %d %s\r", "HTTP/1.1", status, title);
    Log::get_instance()->flush();  
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

// 添加了一些必要的响应头部
void http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    // EMlog(LOGLEVEL_DEBUG,"<<<<<<< Content-Length: %d\r\n", content_len);  
    LOG_DEBUG("<<<<<<< Content-Length: %d\r", content_len);
    Log::get_instance()->flush();  
    return add_response( "Content-Length: %d\r\n", content_len );
}
bool http_conn::add_content_type() {    // 响应体类型，当前文本形式
    // EMlog(LOGLEVEL_DEBUG,"<<<<<<< Content-Type:%s\r\n", "text/html");  
    LOG_DEBUG("<<<<<<< Content-Type:%s\r", "text/html");
    Log::get_instance()->flush();  
    return add_response("Content-Type:%s\r\n", "text/html");    
}
bool http_conn::add_linger(){
    // EMlog(LOGLEVEL_DEBUG,"<<<<<<< Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
    LOG_DEBUG("<<<<<<< Connection: %s\r", ( m_linger == true ) ? "keep-alive" : "close");
    Log::get_instance()->flush();  
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}
bool http_conn::add_blank_line(){
    // EMlog(LOGLEVEL_DEBUG,"<<<<<<< %s", "\r\n" );    
    LOG_DEBUG("<<<<<<< %s", "\r");
    Log::get_instance()->flush();  
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content ){
    // EMlog(LOGLEVEL_DEBUG,"<<<<<<< %s\n", content );
    LOG_DEBUG("<<<<<<< %s", content);
    Log::get_instance()->flush();  
    return add_response( "%s", content );
}


// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret){
    switch (ret)
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
        case FILE_REQUEST:  // 请求文件
            add_status_line(200, ok_200_title );
            if (m_file_stat.st_size != 0){
                add_headers(m_file_stat.st_size);
                // EMlog(LOGLEVEL_DEBUG, "<<<<<<< %s", m_file_address);
                LOG_DEBUG("<<<<<<< %s", m_file_address);
                Log::get_instance()->flush();  
                // 封装m_iv
                m_iv[ 0 ].iov_base = m_write_buf;   // 起始地址
                m_iv[ 0 ].iov_len = m_write_idx;    // 长度
                m_iv[ 1 ].iov_base = m_file_address;
                m_iv[ 1 ].iov_len = m_file_stat.st_size;
                m_iv_count = 2;                     // 两块内存
                bytes_to_send = m_write_idx + m_file_stat.st_size;  // 响应头的大小 + 文件的大小
                return true;
            }else{
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}


// 由线程池中的工作线程调用，处理HTTP请求的入口函数
void http_conn::process(){      // 线程池中线程的业务处理
    // EMlog(LOGLEVEL_DEBUG, "=======parse request, create response.=======\n");
    LOG_DEBUG("=======parse request, create response.=======");
    Log::get_instance()->flush();  
    
    // 解析HTTP请求
    // EMlog(LOGLEVEL_DEBUG,"=============process_reading=============\n");
    LOG_DEBUG("=============process_reading=============");
    Log::get_instance()->flush(); 
    HTTP_CODE read_ret = process_read();
    // EMlog(LOGLEVEL_INFO,"========PROCESS_READ HTTP_CODE : %d========\n", read_ret);
    LOG_DEBUG("========PROCESS_READ HTTP_CODE : %d========", read_ret);
    Log::get_instance()->flush(); 
    if(read_ret == NO_REQUEST){
        modfd(m_epoll_fd, m_sock_fd, EPOLLIN);  // 继续监听EPOLLIN （| EPOLLONESHOT）
        return;         // 返回，线程空闲
    }
    
    // 生成响应
    bool write_ret = process_write(read_ret);
    if(!write_ret){
        conn_close();
        if(timer) m_timer_heap.del_timer(timer);  // 移除其对应的定时器
    }
 
    modfd(m_epoll_fd, m_sock_fd, EPOLLOUT);     // 重置EPOLLONESHOT
}