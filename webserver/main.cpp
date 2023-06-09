#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <error.h>
#include <fcntl.h>      
#include <sys/epoll.h>
#include <signal.h>
#include <assert.h>
#include "locker/locker.h"
#include "threadpool/threadpool.h"
#include "http_conn/http_conn.h"
// #include "timer/lst_timer.h"
#include "timer/heap_timer.h"
#include "log/log.h"
#include "sql_connection_pool/sql_connection_pool.h"

#define MAX_FD 65535            // 最大文件描述符（客户端）数量
#define MAX_EVENT_SIZE 10000    // 监听的最大的事件数量
#define TIMESLOT 5             //最小超时单位

// #define SYNLOG  //同步写日志
#define ASYNLOG //异步写日志

#define listenfdET //边缘触发非阻塞
// #define listenfdLT //水平触发阻塞

// 时间处理模型
// #define Reactor
#define Proactor  // 模拟Proactor

static int pipefd[2];           // 管道文件描述符 0为读，1为写
static HeapTimer m_timer_heap;   // 定时器堆
static int epoll_fd = 0;

// 添加文件描述符到epoll中 （声明成外部函数）
extern void addfd(int epoll_fd, int fd, bool one_shot); 
// 从epoll中删除文件描述符
extern void rmfd(int epoll_fd, int fd);
// 在epoll中修改文件描述符
extern void modfd(int epoll_fd, int fd, int ev);
// 文件描述符设置非阻塞操作
extern void set_nonblocking(int fd);

// 信号处理，添加信号捕捉
void addsig(int sig, void(handler)(int)){       
    struct sigaction sigact;                    // sig 指定信号， void handler(int) 为处理函数
    memset(&sigact, '\0', sizeof(sigact));      // bezero 清空
    sigact.sa_flags = 0;                        // 调用sa_handler
    // sigact.sa_flags |= SA_RESTART;                  // 指定收到某个信号时是否可以自动恢复函数执行，不需要中断后自己判断EINTR错误信号
    sigact.sa_handler = handler;                // 指定回调函数
    sigfillset(&sigact.sa_mask);                // 将临时阻塞信号集中的所有的标志位置为1，即都阻塞
    sigaction(sig, &sigact, NULL);              // 设置信号捕捉sig信号值
}

// 向管道写数据的信号捕捉回调函数
void sig_to_pipe(int sig){
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler()
{
    // 定时处理任务，实际上就是调用tick()函数
    m_timer_heap.tick();
    // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
    alarm(TIMESLOT);
}

//定时器回调函数，删除非活动连接在socket上的注册事件，并关闭
void cb_func(client_data *user_data)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_cnt--;
    LOG_INFO("close fd %d", user_data->sockfd);
    Log::get_instance()->flush();
}

int main(int argc, char* argv[]){
#ifdef ASYNLOG
    Log::get_instance()->init("log_file/ServerLog", 2000, 800000, 100); //异步日志模型
#endif

#ifdef SYNLOG
    Log::get_instance()->init("log_file/ServerLog", 2000, 800000, 0); //同步日志模型
#endif
    if(argc <= 1){      // 形参个数，第一个为执行命令的名称
        // EMlog(LOGLEVEL_ERROR, "run as: %s port_number\n", basename(argv[0]));      // argv[0] 可能是带路径的，用basename转换
        LOG_ERROR("run as: %s port_number", basename(argv[0]));
        Log::get_instance()->flush();  
        exit(-1);
    }
    printf("Server run as Port: %s\n", argv[1]);
    // 获取端口号
    int port = atoi(argv[1]);   // 字符串转整数

    // 对SIGPIE信号进行处理(捕捉忽略，默认退出)
    addsig(SIGPIPE, SIG_IGN);       

    //创建数据库连接池
    connection_pool *connPool = connection_pool::GetInstance();
    connPool->init("localhost", "debian-sys-maint", "UW1uE5OekHYOBrjq", "yourdb", 3306, 8);    
    
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);    // 监听套接字
    assert( listen_fd >= 0 );                            // ...判断是否创建成功

    // 设置端口复用
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    // 绑定
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);


    int ret = bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    assert( ret != -1 );    // ...判断是否成功

    // 监听
    ret = listen(listen_fd, 8);
    assert( ret != -1 );    // ...判断是否成功

    // 创建epoll对象，事件数组（IO多路复用，同时检测多个事件）
    epoll_event events[MAX_EVENT_SIZE]; // 结构体数组，接收检测后的数据
    epoll_fd = epoll_create(5);     // 参数 5 无意义， > 0 即可
    assert( epoll_fd != -1 );
    // 将监听的文件描述符添加到epoll对象中
    addfd(epoll_fd, listen_fd, false);  // 监听文件描述符不需要 ONESHOT & ET
    
    // 创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert( ret != -1 );
    set_nonblocking( pipefd[1] );               // 写管道非阻塞
    addfd(epoll_fd, pipefd[0], false ); // epoll检测读管道

    // 设置信号处理函数
    addsig(SIGALRM, sig_to_pipe);   // 定时器信号
    addsig(SIGTERM, sig_to_pipe);   // SIGTERM 关闭服务器
    bool stop_server = false;       // 关闭服务器标志位

    // 创建一个保存所有客户端信息的数组
    http_conn* users = new http_conn[MAX_FD];
    http_conn::m_epoll_fd = epoll_fd;       // 静态成员，类共享

    // 创建线程池，初始化线程池
    threadpool<http_conn> * pool = NULL;    // 模板类 指定任务类类型为 http_conn
    try{
        pool = new threadpool<http_conn>(connPool);
    }catch(...){
        exit(-1);
    }

    //初始化数据库读取表
    users->initmysql_result(connPool);

    client_data *users_timer = new client_data[MAX_FD];

    bool timeout = false;   // 定时器周期已到
    alarm(TIMESLOT);        // 定时产生SIGALRM信号

    while(!stop_server){
        // 检测事件
        int num = epoll_wait(epoll_fd, events, MAX_EVENT_SIZE, -1);     // 阻塞，返回事件数量
        if(num < 0 && errno != EINTR){
            // EMlog(LOGLEVEL_ERROR,"EPOLL failed.\n");
            LOG_ERROR("EPOLL failed.");
            Log::get_instance()->flush();  
            break;
        }

        // 循环遍历事件数组
        for(int i = 0; i < num; ++i){

            int sock_fd = events[i].data.fd;
            if(sock_fd == listen_fd){   // 监听文件描述符的事件响应
                // 有客户端连接进来
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
#ifdef listenfdLT
                int conn_fd = accept(listen_fd,(struct sockaddr*)&client_addr, &client_addr_len);
                // ...判断是否连接成功
                if (conn_fd == -1) {
                    perror("accept");
                    exit(-1);
                }

                if(http_conn::m_user_cnt >= MAX_FD){
                    // 目前连接数满了
                    // ...给客户端写一个信息：服务器内部正忙
                    close(conn_fd);
                    continue;
                }
                // 将新客户端数据初始化，放到数组中
                users[conn_fd].init(conn_fd, client_addr);  // conn_fd 作为索引
                // 当listen_fd也注册了ONESHOT事件时(addfd)，
                // 接受了新的连接后需要重置socket上EPOLLONESHOT事件，确保下次可读时，EPOLLIN 事件被触发
                // modfd(epoll_fd, listen_fd, EPOLLIN); 

                // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到堆timer_heap中
                users_timer[conn_fd].address = client_addr;
                users_timer[conn_fd].sockfd = conn_fd;
                TimerNode* timer = new TimerNode;
                timer->user_data = &users_timer[conn_fd];
                timer->cb_func = cb_func;
                users_timer[conn_fd].timer = timer;
                m_timer_heap.add(timer, 3 * TIMESLOT);
#endif

#ifdef listenfdET
                while (1)
                {
                    int conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_addr_len);
                   // ...判断是否连接成功
                    if (conn_fd == -1) {
                        LOG_ERROR("%s:errno is:%d", "accept error", errno);
                        break;
                    }

                    if(http_conn::m_user_cnt >= MAX_FD){
                        // 目前连接数满了
                        // ...给客户端写一个信息：服务器内部正忙
                        close(conn_fd);
                        break;
                    }
                    // 将新客户端数据初始化，放到数组中
                    users[conn_fd].init(conn_fd, client_addr);  // conn_fd 作为索引

                    // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到堆timer_heap中
                    users_timer[conn_fd].address = client_addr;
                    users_timer[conn_fd].sockfd = conn_fd;
                    TimerNode* timer = new TimerNode;
                    timer->user_data = &users_timer[conn_fd];
                    timer->cb_func = cb_func;
                    users_timer[conn_fd].timer = timer;
                    m_timer_heap.add(timer, 3 * TIMESLOT);
                }
                continue;
#endif
            }   
                // 读管道有数据，SIGALRM 或 SIGTERM信号触发
            else if(sock_fd == pipefd[0] && (events[i].events & EPOLLIN)){  
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if(ret == -1){
                    continue;
                }else if(ret == 0){
                    continue;
                }else{
                    for(int i = 0; i < ret; ++i){
                        switch (signals[i]) // 字符ASCII码
                        {
                        case SIGALRM:
                        // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                        // 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务。
                            timeout = true;
                            break;
                        case SIGTERM:
                            stop_server = true;
                        }
                    }
                }
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                // 对方异常断开 或 错误 等事件
                // EMlog(LOGLEVEL_DEBUG,"-------EPOLLRDHUP | EPOLLHUP | EPOLLERR--------\n");
                LOG_DEBUG("-------EPOLLRDHUP | EPOLLHUP | EPOLLERR--------");
                Log::get_instance()->flush(); 
                //服务器端关闭连接，移除对应的定时器
                TimerNode *timer = users_timer[sock_fd].timer;
                timer->cb_func(&users_timer[sock_fd]);
                if (timer) {
                    m_timer_heap.del_timer(timer);  // 移除其对应的定时器
                    delete timer;
                }
            }
            else if(events[i].events & EPOLLIN){
                // EMlog(LOGLEVEL_DEBUG,"-------EPOLLIN-------\n\n");
                TimerNode *timer = users_timer[sock_fd].timer;

                LOG_DEBUG("-------EPOLLIN-------\n");
                Log::get_instance()->flush();
#ifdef Proactor

                if (users[sock_fd].read()){         // 主进程一次性读取缓冲区的所有数据
                    LOG_INFO("deal with the client(%s)", inet_ntoa(users[sock_fd].get_address()->sin_addr));
                    Log::get_instance()->flush();

                    pool->append(users + sock_fd);  // 加入到线程池队列中，数组指针 + 偏移 &users[sock_fd]

                    if (timer) {
                        m_timer_heap.adjust(timer, 3 * TIMESLOT);
                    }

                }else{
                    timer->cb_func(&users_timer[sock_fd]);
                    if (timer) {
                        m_timer_heap.del_timer(timer);  // 移除其对应的定时器
                        delete timer;
                    }
                }

#endif 

#ifdef Reactor
                //若监测到读事件，将该事件放入请求队列
                pool->append(users + sock_fd, 0);
                if (timer) {
                        m_timer_heap.adjust(timer, 3 * TIMESLOT);
                }

                while (true) {
                    if (1 == users[sock_fd].improv) {
                        if (1 == users[sock_fd].timer_flag) {
                            m_timer_heap.del_timer(timer);  // 移除其对应的定时器
                            delete timer;
                            users[sock_fd].timer_flag = 0;
                        }
                        users[sock_fd].improv = 0;
                        break;
                    }
                }
                
#endif
            }
            else if(events[i].events & EPOLLOUT){
                // EMlog(LOGLEVEL_DEBUG, "-------EPOLLOUT--------\n\n");
                LOG_DEBUG("-------EPOLLOUT--------\n");
                Log::get_instance()->flush(); 

                TimerNode* timer = users_timer[sock_fd].timer;
#ifdef Proactor
                if (users[sock_fd].write()) {
                    // 主进程一次性写完所有数据
                    LOG_INFO("send data to the client(%s)", inet_ntoa(users[sock_fd].get_address()->sin_addr));
                    Log::get_instance()->flush();

                    m_timer_heap.adjust(timer, 3 * TIMESLOT);

                }
                else {
                    timer->cb_func(&users_timer[sock_fd]);    // 写入失败
                    LOG_ERROR("-------WRITE FAILED--------\n");
                    Log::get_instance()->flush();
                    if (timer) {
                        m_timer_heap.del_timer(users[sock_fd].timer);  // 移除其对应的定时器 
                        delete timer;
                    }
                }
#endif

#ifdef Reactor
                pool->append(users + sock_fd, 1);
                if (timer) {
                    m_timer_heap.adjust(timer, 3 * TIMESLOT);
                }
                while (true) {
                    if (1 == users[sock_fd].improv) {
                        if (1 == users[sock_fd].timer_flag) {
                            m_timer_heap.del_timer(users[sock_fd].timer);  // 移除其对应的定时器 
                            delete timer;
                            users[sock_fd].timer_flag = 0;
                        }
                        users[sock_fd].improv = 0;
                        break;
                    }
                }
#endif
            }
        }
        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if(timeout) {
            // 超时处理
            timer_handler();
            timeout = false;    // 重置timeout
        }
    }
    close(epoll_fd);
    close(listen_fd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;    
    delete[] users_timer;
    delete pool;
    return 0;
}