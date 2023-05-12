#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

struct sockInfo {
    int fd;  // 文件描述符
    pthread_t tid;  // 线程号
    struct sockaddr_in addr;
};

struct sockInfo sockInfos[128];  // 类似于线程池

void* working(void* arg) {
    // 子线程和客户端通信
    // 输出客户端的信息
    struct sockInfo* pinfo = (struct sockInfo *)arg;
    char clientIP[16];
    inet_ntop(AF_INET, &pinfo->addr.sin_addr.s_addr, clientIP, sizeof(clientIP));
    unsigned short clientPort = ntohs(pinfo->addr.sin_port);
    printf("client ip is %s, port is %d\n", clientIP, clientPort);

    // 通信
    char recvBuf[1024] = {0};
    while(1) {
        // 获取客户端的数据
        int num = read(pinfo->fd, recvBuf, sizeof(recvBuf));
        if(num == -1) {
            perror("read");
            exit(-1);
        } else if(num > 0) {


            printf("recv client data : %s\n", recvBuf);
        } else if(num == 0) {
            printf("clinet closed...\n");
            break;
        } else ;

        // char * data = "hello,i am server";
        char* data = recvBuf;
        // 给客户端发送数据
        write(pinfo->fd, data, strlen(data));

    }
    // 关闭子进程通信的文件描述符
    close(pinfo->fd);
    // exit(0);
    return NULL;
}

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

    // 初始化
    int max = sizeof(sockInfos) / sizeof(sockInfos[0]);
    for(int i = 0; i < max; ++ i) {
        bzero(&sockInfos[i], sizeof(sockInfos[i]));
        sockInfos[i].fd = -1;
        sockInfos[i].tid = -1;
    }
    // 4.不断接收客户端连接
    while (1) {
        struct sockaddr_in clientaddr;
        int len = sizeof(clientaddr);
        int cfd = accept(lfd, (struct sockaddr *)&clientaddr, &len);
        if(cfd == -1) {
            perror("accept");
            exit(-1);
        }

        struct sockInfo * pinfo;
        for (int i = 0; i < max; ++ i) {
            if(sockInfos[i].fd == -1) {
                pinfo = &sockInfos[i];
                break;
            }
            if (i == max - 1) {
                sleep(1);
                i --;
            }
        }

        pinfo->fd = cfd;
        memcpy(&pinfo->addr, &clientaddr, len);
        // 每一个连接进来，创建一个线程与客户端进行通信
        pthread_create(&pinfo->tid, NULL, working, pinfo);

        pthread_detach(pinfo->tid);  // join是阻塞的
    }

    close(lfd);

    return 0;
}
