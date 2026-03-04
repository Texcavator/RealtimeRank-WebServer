/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */

#include "webserver.h"

using namespace std;

WebServer::WebServer(
            int port, int trigMode, int timeoutMS, bool OptLinger,
            int sqlPort, const char* sqlUser, const  char* sqlPwd,
            const char* dbName, int connPoolNum, int threadNum,
            bool openLog, int logLevel, int logQueSize):
            port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
            timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
    {
    // 设置静态资源目录
    srcDir_ = getcwd(nullptr, 256);         // getcwd获取当前进程的工作目录（分配256字节的空间，把目录路径存进去）
    assert(srcDir_);
    strncat(srcDir_, "/resources/", 16);    // 路径后拼接"/resources/"

    // 给httpconn静态成员变量赋值
    HttpConn::userCount = 0;                // 当前已连接的客户端数量
    HttpConn::srcDir = srcDir_;             // 每个httpconn对象都共享该资源目录

    // 初始化数据库连接池
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    // 设置事件触发模式
    InitEventMode_(trigMode);

    // 创建监听socket
    if(!InitSocket_()) { isClose_ = true;}

    // 如果开启日志系统，就初始化日志并打印启动信息
    if(openLog) {
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if(isClose_) { LOG_ERROR("========== Server init error!=========="); }
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

WebServer::~WebServer() {
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

// 配置epoll的触发模式（LT/ET）
void WebServer::InitEventMode_(int trigMode) {
    // EPOLLONESHOT 防止多个线程同时处理一个socket
    // EPOLLRDHUP   监听对方是否关闭连接
    listenEvent_ = EPOLLRDHUP;                  // 监听socket （监听socket只在主线程所以不会被多个线程抢）
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;     // 普通连接socket
    switch (trigMode)
    {
    case 0:
        break;
    // 默认是LT，ET需要手动改
    case 1:
        connEvent_ |= EPOLLET;
        break;
    case 2:
        listenEvent_ |= EPOLLET;
        break;
    case 3:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    default:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    }
    // 告诉httpconn是否使用ET
    HttpConn::isET = (connEvent_ & EPOLLET);
}

// 一直等事件，有事件就分类处理，没事件就继续等
void WebServer::Start() {
    int timeMS = -1;  /* epoll wait timeout == -1 无事件将阻塞 */
    if(!isClose_) { LOG_INFO("========== Server start =========="); }

    // server没关闭就一直运行
    while(!isClose_) {
        if(timeoutMS_ > 0) {
            // GetNextTick() 返回距离最近一个超时事件还有多久
            timeMS = timer_->GetNextTick();
        }

        // 阻塞在此，要么有事件发生，要么超时timeMS
        // 返回同一时间发生事件的数量
        int eventCnt = epoller_->Wait(timeMS);

        // 一个个处理事件
        for(int i = 0; i < eventCnt; i++) {
            // fd可能是监听socket（有新的客户端要连接），也可能是客户端socket（有已经连接的客户端要发数据）
            int fd = epoller_->GetEventFd(i);
            // 这个事件是干什么的
            uint32_t events = epoller_->GetEvents(i);

            // 监听socket，即有新的客户端要连接
            if(fd == listenFd_) {
                DealListen_();
            }
            // 客户端关闭or挂起or出错
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                assert(users_.count(fd) > 0);   // 确保一定有这个httpconn对象
                CloseConn_(&users_[fd]);        // 关闭连接
            }
            // 可读
            else if(events & EPOLLIN) {
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);
            }
            // 可写
            else if(events & EPOLLOUT) {
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]);
            } else {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

// 向客户端发送错误信息
void WebServer::SendError_(int fd, const char*info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);  // 向客户端发数据
    if(ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);  // 发完就把连接断了
}

// 关闭连接
void WebServer::CloseConn_(HttpConn* client) {
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());   // 以后epoll不会再监听这个socket
    client->Close();                    // 关闭socket
}

// 添加客户端连接
void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd > 0);
    // 每个客户端连接对应一个httpconn对象
    users_[fd].init(fd, addr);

    if(timeoutMS_ > 0) {
        // 给fd这个客户端添加一个定时器，timeoutMS_毫秒后触发 CloseConn_
        // bind(函数名, 谁调用函数, 传进函数的参数)
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }

    // 把普通连接socket加入epoll事件表
    // 告诉epoll要监听socket的connEvent_（已经配置过了，比如一些异常事件orET） | EPOLLIN
    epoller_->AddFd(fd, EPOLLIN | connEvent_);

    // 将该socket设置成非阻塞模式
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

// 处理客户端连接请求
void WebServer::DealListen_() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    // LT模式：如果有多个客户端同时想要连接，在处理完之前会一直被检测到，从而进入DealListen_所以只需要do一次
    // ET模式：如果有多个客户端同时想要连接，只会检测到一次，所以要do多次保证所有想连接的客户端都连接了
    do {
        // 接收客户端请求
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);
        if(fd <= 0) { return;}
        else if(HttpConn::userCount >= MAX_FD) {    // 超过了最大排队个数
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        AddClient_(fd, addr);
    } while(listenEvent_ & EPOLLET);
}

// 处理读请求
void WebServer::DealRead_(HttpConn* client) {
    assert(client);
    // 只要有读写操作，重新给timeoutMS_的时间
    ExtentTime_(client);
    // 向线程池中加入任务
    // 用bind的原因是AddTask只接受没有参数的任务，利用bind将有参变无参
    // 不直接调用OnRead是因为直接调用就是主线程在工作了，主线程不干重活
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client));
}

// 处理写请求
void WebServer::DealWrite_(HttpConn* client) {
    assert(client);
    // 只要有读写操作，重新给timeoutMS_的时间
    ExtentTime_(client);
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}

// 遇到读写就重置定时器
void WebServer::ExtentTime_(HttpConn* client) {
    assert(client);
    // 把这个定时器的过期时间重新设置为：当前时间 + timeoutMS_
    if(timeoutMS_ > 0) { timer_->adjust(client->GetFd(), timeoutMS_); }
}

// 具体怎么处理读请求
void WebServer::OnRead_(HttpConn* client) {
    assert(client);
    int readErrno = 0;  // 保存最近一次 read/recv 失败时的错误码
    int ret = client->read(&readErrno);
    // 如果读失败且不是因为读完了（EAGAIN）就关闭连接
    // ret > 0 → 成功读到数据
    // ret == 0 → 对方关闭连接
    // ret < 0 → 出错
    if(ret <= 0 && readErrno != EAGAIN) {
        CloseConn_(client);
        return;
    }
    OnProcess(client);
}

// 重置状态，准备接收下一个请求
void WebServer::OnProcess(HttpConn* client) {
    if(client->process()) {     // 处理完毕，响应数据已经放入写缓冲区，接下来要把数据发给客户端
        // 修改epoll对于该fd监听的事件类型，让这个连接开始监听可写事件
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    } else {    // 还没处理完，还需要继续读数据
        // 虽然原本就已经监听可读事件，但是设置过EPOLLONESHOT，每次事件触发后要重新注册监听事件（否则不会被再次触发）
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

// 具体怎么处理写请求
void WebServer::OnWrite_(HttpConn* client) {
    assert(client);
    int writeErrno = 0;
    int ret = client->write(&writeErrno);
    // ToWriteBytes() 返回写缓冲区还剩多少字节没发
    if(client->ToWriteBytes() == 0) {   // 传输完成
        if(client->IsKeepAlive()) {     // 如果是长连接，就不要关闭
            OnProcess(client);          // 重置状态，准备接收下一个请求
            return;
        }
    }
    else if(ret < 0) {                  // 出错了
        if(writeErrno == EAGAIN) {      // 发送缓冲区满了，暂时发不出去
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);    // 重置状态，等可以继续写的时候
            return;
        }
    }
    // 数据已经发完了且不是长连接 or 出错了（不是因为缓冲区满），就关闭连接
    CloseConn_(client);
}

// 创建监听socket
bool WebServer::InitSocket_() {
    // 确保端口在合适范围
    if(port_ > 65535 || port_ < 1024) {
        LOG_ERROR("Port:%d error!",  port_);
        return false;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    // INADDR_ANY: 0.0.0.0
    // 接受所有本机可用网卡的连接 (本机回环和局域网均可连接)
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    // 创建socket
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listenFd_ < 0) {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    // 如果使用优雅关闭，设置相关参数
    // 优雅关闭：关闭时不会立刻丢弃缓冲区数据，内核会尽量把剩余数据发送完，在最大时长内发送不完就强制关闭
    struct linger optLinger = { 0 };
    if(openLinger_) {
        optLinger.l_onoff = 1;  // 启用优雅关闭
        optLinger.l_linger = 1; // 最大等待时长1s
    }

    // 将优雅关闭选项与socket绑定
    int ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if(ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    // 让服务器能快速重启
    // 端口复用（允许这个端口在TIME_WAIT状态下被快速重新绑定）
    // 只有最后一个绑定的socket会正常接收数据
    int optval = 1; // 打开复用选项
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    // 将socket和具体IP、端口绑定
    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));
    if(ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    // 进入监听状态，最多6个排队
    ret = listen(listenFd_, 6);
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    // 把监听socket加入epoll事件表
    // 告诉epoll要监听socket的listenEvent_（已经配置过了，比如一些异常事件orET） | EPOLLIN
    ret = epoller_->AddFd(listenFd_,  listenEvent_ | EPOLLIN);
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }

    // 将监听socket设置成非阻塞模式
    SetFdNonblock(listenFd_);
    LOG_INFO("Server port:%d", port_);
    return true;
}

// 给fd设置非阻塞状态
int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    // fcntl 是操作文件描述符（fd）的函数
    // F_GETFD: 获取文件描述符的属性（比如FD_CLOEXEC表示exec系列函数执行时关闭这个fd）
    // F_SETFL: 把修改后的文件描述符属性(fcntl(fd, F_GETFD, 0) | O_NONBLOCK)重新赋给这个文件描述符
    // | O_NONBLOCK: 非阻塞标志，保证read/write调用不会阻塞（一旦没有数据就立即返回，不会让这个线程停滞，换其他的线程继续）
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}


