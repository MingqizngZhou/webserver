#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <queue>
#include <unordered_map>
#include <algorithm>
#include <time.h>
#include <arpa/inet.h> 
#include <functional> 
#include <assert.h> 
#include <chrono>
#include "../log/log.h"
#include "../http_conn/http_conn.h"

class http_conn;
class TimerNode;

struct client_data
{
    sockaddr_in address;
    int sockfd;
    TimerNode *timer;
};

class TimerNode
{
public:
    TimerNode(){}
    ~TimerNode() {
        delete user_data;
    }

public:
    time_t expire;
    void (*cb_func)(client_data *);
    client_data *user_data;
};

class HeapTimer {
public:
    HeapTimer() { heap_.reserve(64); }

    ~HeapTimer() { clear(); }
    // 当某个定时任务发生变化时，调整对应的定时器在堆中的位置
    void adjust(TimerNode* timer, int newExpires);

    // 将目标定时器timer添加到堆中
    void add(TimerNode* timer, int timeout);

    // 将目标定时器 timer 从链表中删除
    void del_timer(TimerNode* timer);

    void clear();

    // SIGALARM 信号每次被触发就在其信号处理函数中执行一次 tick() 函数，以处理堆中到期任务。
    void tick();

    void pop();


private:
    // 删除指定位置的结点
    void del_(size_t i);
    
    void siftup_(size_t i);

    bool siftdown_(size_t index, size_t n);

    void SwapNode_(size_t i, size_t j);

    std::vector<TimerNode*> heap_;

    std::unordered_map<client_data*, size_t> ref_;
};

#endif