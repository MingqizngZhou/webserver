#include "heap_timer.h"

void HeapTimer::siftup_(size_t i) {
    assert(i >= 0 && i < heap_.size());
    size_t j = (i - 1) / 2;
    while(j >= 0) {
        if(heap_[j] < heap_[i]) { break; }
        SwapNode_(i, j);
        i = j;
        j = (i - 1) / 2;
    }
}

void HeapTimer::SwapNode_(size_t i, size_t j) {
    assert(i >= 0 && i < heap_.size());
    assert(j >= 0 && j < heap_.size());
    std::swap(heap_[i], heap_[j]);
    ref_[heap_[i]->user_data] = i;
    ref_[heap_[j]->user_data] = j;
} 

bool HeapTimer::siftdown_(size_t index, size_t n) {
    assert(index >= 0 && index < heap_.size());
    assert(n >= 0 && n <= heap_.size());
    size_t i = index;
    size_t j = i * 2 + 1;
    while(j < n) {
        if(j + 1 < n && heap_[j + 1] < heap_[j]) j++;
        if(heap_[i] < heap_[j]) break;
        SwapNode_(i, j);
        i = j;
        j = i * 2 + 1;
    }
    return i > index;
}

void HeapTimer::add(TimerNode* timer, int time_out) {
    LOG_DEBUG("===========adding timer.=============");
    Log::get_instance()->flush();
    if( !timer ) {
        // EMlog(LOGLEVEL_WARN ,"===========timer null.=========\n");
        LOG_WARN("===========timer null.=========");
        Log::get_instance()->flush(); 
        assert(timer != nullptr);
        return;
    }
    size_t i;
    
    time_t curr_time = time(NULL);  // 获取当前系统时间

    if(ref_.count(timer->user_data) == 0) {
        /* 新节点：堆尾插入，调整堆 */
        i = heap_.size();
        ref_[timer->user_data] = i;
        timer->expire = curr_time + time_out;
        heap_.push_back(timer);
        siftup_(i);
    } 
    else {
        /* 已有结点：调整堆 */
        i = ref_[timer->user_data];
        heap_[i]->expire = curr_time + time_out;
        if(!siftdown_(i, heap_.size())) {
            siftup_(i);
        }
    }
    LOG_DEBUG("===========added timer.=============");
    Log::get_instance()->flush();
}

void HeapTimer::del_timer(TimerNode* timer) {
    /* 删除指定id结点，并触发回调函数 */
    if(heap_.empty() || ref_.count(timer->user_data) == 0) {
        return;
    }
    size_t i = ref_[timer->user_data];
    TimerNode* node = heap_[i];
    del_(i);
}

void HeapTimer::del_(size_t index) {
    LOG_DEBUG("===========deleting timer.===========");
    Log::get_instance()->flush(); 
    /* 删除指定位置的结点 */
    assert(!heap_.empty() && index >= 0 && index < heap_.size());
    /* 将要删除的结点换到队尾，然后调整堆 */
    size_t i = index;
    size_t n = heap_.size() - 1;
    assert(i <= n);
    if(i < n) {
        SwapNode_(i, n);
        if(!siftdown_(i, n)) {
            siftup_(i);
        }
    }
    /* 队尾元素删除 */

    ref_.erase(heap_.back()->user_data);
    heap_.pop_back();

    LOG_DEBUG("===========deleted timer.===========");
    Log::get_instance()->flush(); 
}

void HeapTimer::adjust(TimerNode* timer, int new_time_out) {
    /* 调整指定timer的结点 */
    LOG_DEBUG("===========adjusting timer.=========");
    Log::get_instance()->flush();

    if( !timer )  {
        LOG_DEBUG("===========timer null.==========");
        Log::get_instance()->flush();   
        return;
    }

    if (heap_.empty() || ref_.count(timer->user_data) == 0) {
        add(timer, new_time_out);
    }

    time_t curr_time = time(NULL);  // 获取当前系统时间
    heap_[ref_[timer->user_data]]->expire = curr_time + new_time_out;
    
    siftdown_(ref_[timer->user_data], heap_.size());

    LOG_DEBUG("===========adjusted timer.==========");
    Log::get_instance()->flush(); 
}

void HeapTimer::tick() {
    /* 清除超时结点 */
    if(heap_.empty()) {
        return;
    }
    LOG_DEBUG("==========timer tick============");
    Log::get_instance()->flush();

    time_t curr_time = time(NULL);  // 获取当前系统时间
    while(!heap_.empty()) {
        TimerNode* node = heap_.front();
        if(node->expire >= curr_time) { 
            break; 
        }
        pop();
    }
}

void HeapTimer::pop() {
    assert(!heap_.empty());
    del_(0);
}

void HeapTimer::clear() {
    ref_.clear();
    heap_.clear();
}
