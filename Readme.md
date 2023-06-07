# Webserver
用C++实现的高性能WEB服务器，经过webbenchh压力测试可以实现上万的并发连接。

- [Webserver](#webserver)
  - [功能](#功能)
  - [版本信息](#版本信息)
  - [压力测试](#压力测试)
    - [1. 本地测试](#1-本地测试)
    - [2. 云服务器测试](#2-云服务器测试)
  - [文件树](#文件树)


## 功能
* 利用IO复用技术Epoll与线程池实现多线程的Reactor/Proactor高并发模型；
* 利用状态机解析HTTP请求报文，实现处理静态资源的请求；
* 利用RAII机制实现了数据库连接池，减少数据库连接建立与关闭的开销，同时实现了用户注册登录功能；
* 利用阻塞队列实现异步日志系统，记录服务器的运行状态；
* 基于链表/小根堆实现的定时器，关闭超时的非活动连接；
* 访问服务器数据库实现web端用户注册、登录功能，可以请求服务器图片和视频文件。

## 版本信息

> webserver v9.0
通过宏定义实现自由选择Reactor模型/Proactor模型

> webserver v8.0
通过宏定义实现listen_fd和conn_fd自由选择ET或者LT模式

> webserver v7.0
将定时器的结构由链表优化为小根堆, 增加日志范围选择

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

## 文件树
~~~
.
├── client
│   └── src
│       └── main.c
├── Readme.md
├── server
│   └── src
│       └── main.c
└── webserver
    ├── build.sh
    ├── http_conn
    │   ├── http_conn.cpp
    │   └── http_conn.h
    ├── locker
    │   └── locker.h
    ├── log
    │   ├── block_queue.h
    │   ├── log.cpp
    │   └── log.h
    ├── log_file
    ├── main.cpp
    ├── Makefile
    ├── resources                   // 静态文件
    │   ├── images
    │   │   ├── image1.jpg
    │   │   ├── xxx.jpg
    │   │   └── xxx.mp4
    │   ├── index.html
    │   ├── judge.html
    │   ├── logError.html
    │   ├── log.html
    │   ├── picture.html
    │   ├── registerError.html
    │   ├── register.html
    │   ├── video.html
    │   └── welcome.html
    ├── sql_connection_pool
    │   ├── sql_connection_pool.cpp
    │   └── sql_connection_pool.h
    ├── test_presure                // 压力测试工具
    │   └── webbench-1.5  
    ├── threadpool
    │   └── threadpool.h
    └── timer
        ├── lst_timer.cpp
        ├── lst_timer.h
        ├── heap_timer.cpp
        └── heap_timer.h

17 directories, 46 files
~~~