#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <wait.h>
#include <errno.h>

void recyleChild (int arg) {
    while(1) {
        int ret = waitpid(-1, NULL, WNOHANG);
        if (ret == -1) {
            // 所有子进程都被回收
            break;
        } else if (ret == 0) {
            // 还有子进程活着
            break;
        } else {
            // 子进程被回收
            printf("子进程 %d 被回收了\n", ret);
        }
    }
}

int main() {
    struct sigaction act;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    act.sa_handler = recyleChild;
    // 注册信号捕捉
    sigaction(SIGCHLD, &act, NULL);

    // 1.创建socket(用于监听的套接字)
    int lfd = socket(PF_INET, SOCK_STREAM, 0);
    if(lfd == -1) {
        perror("socket");
        exit(-1);
    }

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

    // 4.不断接收客户端连接
    while (1) {
        struct sockaddr_in clientaddr;
        int len = sizeof(clientaddr);
        int cfd = accept(lfd, (struct sockaddr *)&clientaddr, &len);
        if(cfd == -1) {
            if (errno == EINTR) {
                // 信号软中断连接以后没有连接被接收到
                // 需要单独进行处理，不然程序会直接按照连接错误的方式退出
                continue;
            }
            perror("accept");
            exit(-1);
        }

        // 每一个连接进来，创建一个子进程与客户端进行通信
        pid_t pid = fork();
        if(pid == 0) {
            // 子进程
            char clientIp[16];
            // 输出客户端的信息
            char clientIP[16];
            inet_ntop(AF_INET, &clientaddr.sin_addr.s_addr, clientIP, sizeof(clientIP));
            unsigned short clientPort = ntohs(clientaddr.sin_port);
            printf("client ip is %s, port is %d\n", clientIP, clientPort);

            // 5.通信
            char recvBuf[1024] = {0};
            while(1) {
                // 获取客户端的数据
                int num = read(cfd, recvBuf, sizeof(recvBuf));
                if(num == -1) {
                    perror("read");
                    exit(-1);
                } else if(num > 0) {


                    printf("recv client data : %s\n", recvBuf);
                } else if(num == 0) {
                    printf("clinet closed...\n");
                    break;
                }

                // char * data = "hello,i am server";
                char* data = recvBuf;
                // 给客户端发送数据
                write(cfd, data, strlen(data));

            }
            // 关闭子进程通信的文件描述符
            close(cfd);
            exit(0);
        }
    }
   
    // 关闭监听的文件描述符
    close(lfd);

    return 0;
}
