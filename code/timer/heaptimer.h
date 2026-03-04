/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */ 
#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <queue>
#include <unordered_map>
#include <time.h>
#include <algorithm>
#include <arpa/inet.h> 
#include <functional> 
#include <assert.h> 
#include <chrono>
#include "../log/log.h"

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;   // 高精度时钟类型
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;                //  某个时间点

// 定时器节点
struct TimerNode {
    int id;                 // 编号
    TimeStamp expires;      // 到期时间
    TimeoutCallBack cb;     // 定时器回调
    bool operator<(const TimerNode& t) {
        return expires < t.expires;     // 最小的到期时间在栈顶
    }
};
class HeapTimer {
public:
    HeapTimer() { heap_.reserve(64); }  // 提前分配容量

    ~HeapTimer() { clear(); }
    
    void adjust(int id, int newExpires);

    void add(int id, int timeOut, const TimeoutCallBack& cb);

    void doWork(int id);

    void clear();

    void tick();

    void pop();

    int GetNextTick();

private:
    void del_(size_t i);
    
    void siftup_(size_t i);

    bool siftdown_(size_t index, size_t n);

    void SwapNode_(size_t i, size_t j);

    // vector手动维护小顶堆
    std::vector<TimerNode> heap_;

    // ip到heap_下标的映射
    std::unordered_map<int, size_t> ref_;
};

#endif //HEAP_TIMER_H