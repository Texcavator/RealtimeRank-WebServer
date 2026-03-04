/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */ 
#include "heaptimer.h"

// 堆的向上调整
void HeapTimer::siftup_(size_t i) {
    assert(i >= 0 && i < heap_.size());
    while(i > 0) {                  // i = 0 时直接退出
        size_t j = (i - 1) / 2;     // 父节点
        if(heap_[j] < heap_[i]) break; // 满足小根堆
        SwapNode_(i, j);
        i = j;
    }
}

// 交换堆中两个节点
void HeapTimer::SwapNode_(size_t i, size_t j) {
    assert(i >= 0 && i < heap_.size());
    assert(j >= 0 && j < heap_.size());
    std::swap(heap_[i], heap_[j]);
    ref_[heap_[i].id] = i;
    ref_[heap_[j].id] = j;
} 

// 堆的向下调整
bool HeapTimer::siftdown_(size_t index, size_t n) {
    assert(index >= 0 && index < heap_.size());
    assert(n >= 0 && n <= heap_.size());
    size_t i = index;       // 父节点
    size_t j = i * 2 + 1;   // 左孩子
    while(j < n) {
        // 如果右孩子存在且比左孩子更小，就换成右孩子
        if(j + 1 < n && heap_[j + 1] < heap_[j]) j++;
        // 父节点比最小的孩子小，跳出循环
        if(heap_[i] < heap_[j]) break;
        // 否则父节点和最小孩子交换位置
        SwapNode_(i, j);
        i = j;
        j = i * 2 + 1;
    }
    return i > index;   // 返回是否移动
}

// 添加新的定时器
void HeapTimer::add(int id, int timeout, const TimeoutCallBack& cb) {
    assert(id >= 0);
    size_t i;
    if(ref_.count(id) == 0) {
        /* 新节点：堆尾插入，调整堆 */
        i = heap_.size();
        ref_[id] = i;
        heap_.push_back({id, Clock::now() + MS(timeout), cb});
        siftup_(i);     // 新定时器从堆尾往上调
    } 
    else {
        /* 已有结点：调整堆 */
        i = ref_[id];
        heap_[i].expires = Clock::now() + MS(timeout);
        heap_[i].cb = cb;
        if(!siftdown_(i, heap_.size())) {   // 如果不能下移那就上移
            siftup_(i);
        }
    }
}

// 立即执行某个定时器的回调
void HeapTimer::doWork(int id) {
    if(heap_.empty() || ref_.count(id) == 0) {
        return;
    }
    // 删除指定节点，触发回调函数
    size_t i = ref_[id];
    TimerNode node = heap_[i];
    node.cb();
    del_(i);
}

// 删除索引为i的定时器
void HeapTimer::del_(size_t index) {
    assert(!heap_.empty() && index >= 0 && index < heap_.size());
    // 将要删除的结点换到队尾，然后调整堆
    size_t i = index;
    size_t n = heap_.size() - 1;
    assert(i <= n);
    if(i < n) {
        SwapNode_(i, n);
        if(!siftdown_(i, n)) {
            siftup_(i);
        }
    }
    // 队尾元素删除
    ref_.erase(heap_.back().id);
    heap_.pop_back();
}

// 调整定时器的到期时间，重新堆化
void HeapTimer::adjust(int id, int timeout) {
    assert(!heap_.empty() && ref_.count(id) > 0);
    heap_[ref_[id]].expires = Clock::now() + MS(timeout);;
    siftdown_(ref_[id], heap_.size());
}

// 检查堆顶计时器是否到期并触发回调
void HeapTimer::tick() {
    if(heap_.empty())  return;
    while(!heap_.empty()) {
        TimerNode node = heap_.front();
        // duration_cast 用于单位转换
        // .count() 用于获取整数值。
        if(std::chrono::duration_cast<MS>(node.expires - Clock::now()).count() > 0) break; // 没到时间
        node.cb();  // 触发回调
        pop();      // 删除堆顶计时器
    }
}

// 移除堆顶计时器
void HeapTimer::pop() {
    assert(!heap_.empty());
    del_(0);
}

// 清空所有计时器
void HeapTimer::clear() {
    ref_.clear();
    heap_.clear();
}

// 返回距离下一个定时器到期的时间
int HeapTimer::GetNextTick() {
    tick();
    size_t res = -1;
    if(!heap_.empty()) {
        res = std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();
        if(res < 0) { res = 0; }
    }
    return res;
}