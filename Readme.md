# Webserver
[toc]

## 版本信息
> webserver v2.0
增加日志和移除长时间不通信的用户，增加Makefile

> webserver v1.0
使用单Reactor多线程的模式

> epoll_ET
使用I/O多路复用中的epoll实现服务器并发(ET模式)

> epoll
使用I/O多路复用中的epoll实现服务器并发(默认LT模式)

> poll
使用I/O多路复用中的poll实现服务器并发

> select
使用I/O多路复用中的select实现服务器并发

> simple multi-thread server v1.0
增加端口复用

> simple multi-thread server
通过线程池 + 多线程的方式实现并发连接

> simple multi-process server
通过父子进程实现服务器的并发连接。

> simple server_client 
使用socket完成简易的TCP/IP通信，包括服务端与客户端。
