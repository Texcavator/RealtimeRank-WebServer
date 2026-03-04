/*
 * @Author       : mark
 * @Date         : 2020-06-19
 * @copyleft Apache 2.0
 */

#include "epoller.h"

Epoller::Epoller(int maxEvent):epollFd_(epoll_create(512)), events_(maxEvent){
    assert(epollFd_ >= 0 && events_.size() > 0);
}

Epoller::~Epoller() {
    close(epollFd_);
}

// 向epoll注册 <一个fd和它关注的事件>
bool Epoller::AddFd(int fd, uint32_t events) {
    if(fd < 0) return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    // epoll_ctl(epoll文件描述符, 操作类型, 文件描述符fd, 事件信息)
    // 成功返回0，失败-1
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
}

// 修改已经注册的fd的事件类型
bool Epoller::ModFd(int fd, uint32_t events) {
    if(fd < 0) return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    // epoll_ctl(epoll文件描述符, 操作类型, 文件描述符fd, 事件信息)
    // 成功返回0，失败-1
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
}

// 从epoll中移除fd
bool Epoller::DelFd(int fd) {
    if(fd < 0) return false;
    epoll_event ev = {0};
    // epoll_ctl(epoll文件描述符, 操作类型, 文件描述符fd, 事件信息)
    // 成功返回0，失败-1
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &ev);
}

// 等待事件：返回事件数量
int Epoller::Wait(int timeoutMs) {  // timeoutMs等于-1表示无限等待
    // epoll_wait(epoll文件描述符, 存放就绪事件的数组起始地址, 数组容量, 等待事件)
    return epoll_wait(epollFd_, &events_[0], static_cast<int>(events_.size()), timeoutMs);
}

// 获取第i个事件对应的fd
int Epoller::GetEventFd(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].data.fd;
}

// 获取第i个事件的事件类型：EPOLLIN、EPOLLOUT等
uint32_t Epoller::GetEvents(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].events;
}