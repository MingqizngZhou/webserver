#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_EPOLLEVENTS 1024

int main() {
    // 1.创建socket(用于监听的套接字)
    int lfd = socket(PF_INET, SOCK_STREAM, 0);
    if(lfd == -1) {
        perror("socket");
        exit(-1);
    }
    // 设置端口复用
    int optval = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);

    // 2.绑定
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    // inet_pton(AF_INET, "192.168.193.128", saddr.sin_addr.s_addr);
    saddr.sin_addr.s_addr = INADDR_ANY;  // 0.0.0.0
    saddr.sin_port = htons(9999);
    int ret = bind(lfd, (struct sockaddr *)&saddr, sizeof(saddr));
    if(ret == -1) {
        perror("bind");
        exit(-1);
    }

    // 3.监听
    ret = listen(lfd, 8);
    if(ret == -1) {
        perror("listen");
        exit(-1);
    }

    // 调用epoll_create()创建一个epoll实例
    int epfd = epoll_create(1);

    // 将监听的文件描述符相关的检测信息加到实例中
    struct epoll_event epev;
    epev.events = EPOLLIN | EPOLLOUT;
    epev.data.fd = lfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &epev);

    struct epoll_event epevs[MAX_EPOLLEVENTS];  // 传出参数，保存了发送了变化的文件描述符的信息

    while (1) {
        int ret = epoll_wait(epfd, epevs, MAX_EPOLLEVENTS, -1);  // 阻塞
        if (ret == -1) {
            perror("epoll_wait");
            exit(-1);
        } 
        // printf("ret = %d\n", ret);

        for (int i = 0; i < ret; ++ i) {
            int curFd = epevs[i].data.fd;

            if (curFd == lfd) {
                // 监听的文件描述符有数据到达--有客户端到达
                struct sockaddr_in client_addr;
                int len = sizeof(client_addr);
                int cfd = accept(lfd, (struct sockaddr*)&client_addr, &len);

                // 设置cfd属性非阻塞
                int flag = fcntl(cfd, F_GETFL);
                flag |= O_NONBLOCK;
                fcntl(cfd, F_SETFL, flag);


                // 输出客户端的信息
                char clientIP[16];
                inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, clientIP, sizeof(clientIP));
                unsigned short clientPort = ntohs(client_addr.sin_port);
                printf("client ip is %s, port is %d\n", clientIP, clientPort);

                epev.events = EPOLLIN | EPOLLET; // 设置边沿触发
                epev.data.fd = cfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &epev);
            } else {
                if (epevs[i].events & EPOLLOUT) continue;

                // 循环读取所有数据
                char buf[5] = {0};
                int len = 0;
                while ((len = read(curFd, buf, sizeof(buf))) > 0) {
                    // 打印
                    // printf("recvData = %s\n", buf);
                    write(STDIN_FILENO, buf, len); // 直接写到终端
                    write(curFd, buf, len);
                    // 清空一下缓存
                    memset(buf, 0, sizeof buf);
                }
                if (len == 0) {
                    printf("client closed...\n");
                } else if (len == -1) {
                    if (errno == EAGAIN) {
                        // 如果文件缓冲区里面的数据恰好在上一轮读完，这个时候去读就会产生这个错误
                        // printf("data over"); 
                        continue;
                    }
                    else {
                        perror("read");
                        exit(-1);
                    }
                }
            }
        }
    }
    close(lfd);
    close(epfd);

    return 0;
}
