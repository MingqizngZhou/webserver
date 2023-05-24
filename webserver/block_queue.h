/******************************************
 * 循环数组实现阻塞队列
 * 线程安全：每个操作前都要加锁，操作后解锁
*******************************************/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdio.h>
#include <pthread.h>
#include <sys/time.h>
#include "locker.h"

template <class T>
class block_queue
{
public:
    block_queue(int max_size = 1000) {
        if (max_size <= 0)
        {
            exit(-1);
        }

        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    void clear() {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    ~block_queue() {
        m_mutex.lock();
        if (m_array != NULL)
            delete [] m_array;

        m_mutex.unlock();
    }
    //判断队列是否满了
    bool full() {
        m_mutex.lock();
        if (m_size >= m_max_size)
        {

            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    //判断队列是否为空
    bool empty() {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    //学习一下返回值＋参数传递的方式
    //返回队首元素
    bool front(T &value) {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }
    //返回队尾元素
    bool back(T &value) {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    int size() {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_size;

        m_mutex.unlock();
        return tmp;
    }

    int max_size() {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_max_size;

        m_mutex.unlock();
        return tmp;
    }
    //往队列添加元素，需要将所有使用队列的线程先唤醒
    //当有元素push进队列,相当于生产者生产了一个元素
    //若当前没有线程等待条件变量,则唤醒无意义
    // 当没有队列中没有写满元素的时候阻塞消费者
    // 当队列中放满元素以后生产者无法写入，唤醒消费者
    bool push(const T &item) {

        m_mutex.lock();
        if (m_size >= m_max_size)
        {
            // 开始写入
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        // 阻塞队列未满，放入元素
        // 当满以后可以直接回到队列头，这样不用对数组进行重置
        // 用m_front与m_back确定数组的范围
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;

        m_size++;

        m_cond.broadcast();   // ？
        m_mutex.unlock();
        return true;
    }
    //pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T &item) {

        m_mutex.lock();
        while (m_size <= 0) {
            
            if (!m_cond.wait(m_mutex.get()))
            {
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    //增加了超时处理
    bool pop(T &item, int ms_timeout) {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if (m_size <= 0)
        {
            // 以当前时间为起点，超时ms_timeout后唤醒
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t))
            {
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0)
        {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;     // 锁
    cond m_cond;        // 环境变量

    T *m_array;         // 数组
    int m_size;         // 数组当前的长度
    int m_max_size;     // 数组最大的长度
    int m_front;        // 数组头
    int m_back;         // 数组尾
};

#endif