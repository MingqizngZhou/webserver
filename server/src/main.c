#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <poll.h>

#define MAX_CONCURRENCY 1024

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

    // 初始化检测为文件描述符数组
    struct pollfd fds[MAX_CONCURRENCY]; // 设置最大并发量是1024
    for (int i = 0; i < MAX_CONCURRENCY; ++ i) {
        fds[i].fd = -1;
        fds[i].events = POLLIN;
    }

    fds[0].fd = lfd;  // fds[0]用作监听位置
    int nfds = 0;

    while (1) {
        // 调用poll系统函数，让内核帮忙检测哪些文件描述符有数据
        int ret = poll(fds, nfds + 1, -1);  // -1设置阻塞，当检测到需要检测的文件描述符有变化，解除阻塞
        if (ret == -1) {
            perror("poll");
            exit(-1);
        } else if (ret == 0) {
            continue;
        } else if (ret > 0) {
            // 文件描述符的对应的缓冲区的数据发生了变化
            if (fds[0].revents & POLLIN) {  // 内核返回的参数可能为 POLLIN | POLLOUT
                // 表示有新的客户端连接进来
                struct sockaddr_in client_addr;
                int len = sizeof(client_addr);
                int cfd = accept(lfd, (struct sockaddr*)&client_addr, &len);

                // 输出客户端的信息
                char clientIP[16];
                inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, clientIP, sizeof(clientIP));
                unsigned short clientPort = ntohs(client_addr.sin_port);
                printf("client ip is %s, port is %d\n", clientIP, clientPort);


                // 将新的文件描述符加入到集合中
                for (int i = 1; i < MAX_CONCURRENCY; ++ i) {
                    if (fds[i].fd == -1) {
                        fds[i].fd = cfd;
                        fds[i].events = POLLIN;
                        break;
                    }
                }

                // 更新最大的文件描述符索引
                nfds = nfds > cfd ? nfds : cfd;
            }

            for (int i = 1; i <= nfds; ++ i) {
                if (fds[i].revents & POLLIN) {
                    // 说明这个文件描述符对应的客户端发来了数据
                    char buf[1024] = {0};
                    int len = read(fds[i].fd, buf, sizeof buf);
                    if (len == -1) {
                        perror("read");
                        exit(-1);
                    } else if (len == 0) {
                        printf("client closed...\n");
                        close(fds[i].fd);
                        fds[i].fd = -1;;
                    } else if (len > 0) {
                        printf("recv client data : %s\n", buf);
                        write(fds[i].fd, buf, strlen(buf));
                    }
                }
            }
        } else ;
    }

    close(lfd);

    return 0;
}
