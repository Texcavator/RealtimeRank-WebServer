/*
 * @Author       : mark
 * @Date         : 2020-06-15
 * @copyleft Apache 2.0
 */ 
#ifndef EPOLLER_H
#define EPOLLER_H

#include <sys/epoll.h> //epoll_ctl()
#include <fcntl.h>  // fcntl()
#include <unistd.h> // close()
#include <assert.h> // close()
#include <vector>
#include <errno.h>

class Epoller {
public:
    // explicit 防止隐式类型转换
        // A a2 = 10;   // ❌ 编译错误
        // A a3(10);    // ✅ 正确，显式调用构造函数
    explicit Epoller(int maxEvent = 1024);

    ~Epoller();

    bool AddFd(int fd, uint32_t events);

    bool ModFd(int fd, uint32_t events);

    bool DelFd(int fd);

    int Wait(int timeoutMs = -1);

    int GetEventFd(size_t i) const;

    uint32_t GetEvents(size_t i) const;
        
private:
    int epollFd_;   // epoll_create返回的文件描述符，用于操作epoll内核事务

    std::vector<struct epoll_event> events_;    // 存储epoll_wait返回的就绪事件数组
};

#endif //EPOLLER_H