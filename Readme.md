# Webserver
用C++实现的高性能WEB服务器，经过webbenchh压力测试可以实现上万的并发连接。

[toc]

## 功能
* 利用IO复用技术Epoll与线程池实现多线程的Reactor高并发模型；
* 利用状态机解析HTTP请求报文，实现处理静态资源的请求；
* 利用RAII机制实现了数据库连接池，减少数据库连接建立与关闭的开销，同时实现了用户注册登录功能；
* 利用阻塞队列实现异步日志系统，记录服务器的运行状态；
* 基于链表实现的定时器，关闭超时的非活动连接。

## 版本信息

> webserver v6.0
将用户的信息存储在Mysql中

> webserver v5.0
增加POST功能, 实现基本的用户注册, 登录, 请求资源

> webserver v4.0
实现异步日志功能

> webserver v3.0
将带有时间戳的日志信息输出到文本文件中

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


## 压力测试
* 并发连接数：10000、20000
* 测试时间： 5s

### 1. 本地测试
本地环境：Ubuntu 20.04 双核 内存8G
~~~
zmq@zmq-virtual-machine:~/Webserver/webserver/test_presure/webbench-1.5$ ./webbench -c 10000 -t 5 http://192.168.141.128:10000/index.html
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://192.168.141.128:10000/index.html
10000 clients, running 5 sec.

Speed=2417040 pages/min, 17442798 bytes/sec.
Requests: 201420 susceed, 0 failed.
~~~

### 2. 云服务器测试
云服务器环境：Ubuntu 20.04 单核 内存2G
~~~
zmq@zmq-virtual-machine:~/test_presure/webbench-1.5$ ./webbench -c 10000 -t 5 http://xxx.xx.xx.xxx:10000/index.html
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://xxx.xx.xx.xxx:10000/index.html
10000 clients, running 5 sec.

Speed=28764 pages/min, 207840 bytes/sec.
Requests: 2397 susceed, 0 failed.
~~~

~~~
zmq@zmq-virtual-machine:~/test_presure/webbench-1.5$ ./webbench -c 20000 -t 5 http://xxx.xx.xx.xxx:10000/index.html
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://xxx.xx.xx.xxx:10000/index.html
20000 clients, running 5 sec.

Speed=54084 pages/min, 431874 bytes/sec.
Requests: 4507 susceed, 0 failed.
~~~
