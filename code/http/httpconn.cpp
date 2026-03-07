/*
 * @Author       : mark
 * @Date         : 2020-06-15
 * @copyleft Apache 2.0
 */ 
#include "httpconn.h"
using namespace std;

const char* HttpConn::srcDir;
std::atomic<int> HttpConn::userCount;
bool HttpConn::isET;
static RankingService rankingService;

HttpConn::HttpConn() { 
    fd_ = -1;
    addr_ = { 0 };
    isClose_ = true;
};

HttpConn::~HttpConn() { 
    Close(); 
};

// 连接建立的时候初始化
void HttpConn::init(int fd, const sockaddr_in& addr) {
    assert(fd > 0);
    userCount++;        // 连接数+1
    addr_ = addr;
    fd_ = fd;
    // 清空缓冲区，复位读写指针（把缓冲区视为空）
    writeBuff_.RetrieveAll();
    readBuff_.RetrieveAll();
    isClose_ = false;
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
}

void HttpConn::Close() {
    response_.UnmapFile();
    if(isClose_ == false){
        isClose_ = true; 
        userCount--;
        close(fd_);
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
    }
}

int HttpConn::GetFd() const {
    return fd_;
};

struct sockaddr_in HttpConn::GetAddr() const {
    return addr_;
}

const char* HttpConn::GetIP() const {
    return inet_ntoa(addr_.sin_addr);   // 二进制换成点分十进制
}

int HttpConn::GetPort() const {
    return addr_.sin_port;
}

// 从socket读数据存入readBuff，返回最后一次读到的字节数
ssize_t HttpConn::read(int* saveErrno) {
    ssize_t len = -1;
    do {
        len = readBuff_.ReadFd(fd_, saveErrno);
        if (len <= 0) { // 没数据读了
            break;
        }
    } while (isET); // LT只读一次 ET反复读
    return len;
}

// 将数据写入writeBuff，返回最后一次写的字节数
ssize_t HttpConn::write(int* saveErrno) {
    ssize_t len = -1;
    do {
        // 往socket发送数据 返回len为实际发送字节数
        len = writev(fd_, iov_, iovCnt_);
        if(len <= 0) {  // 出错
            *saveErrno = errno;
            break;
        }
        if(iov_[0].iov_len + iov_[1].iov_len  == 0) { break; }  // 该发的都发了
        // static_cast: 普通类型转换
        // 发送量>header长度：说明header已经全部发送，并发送了一部分file
        else if(static_cast<size_t>(len) > iov_[0].iov_len) {
            // 更新下一次要发送的起始位置和长度
            iov_[1].iov_base = (uint8_t*) iov_[1].iov_base + (len - iov_[0].iov_len);
            iov_[1].iov_len -= (len - iov_[0].iov_len);
            if(iov_[0].iov_len) {
                writeBuff_.RetrieveAll();   // 清空写缓冲区（file是不放进写缓冲区的）
                iov_[0].iov_len = 0;
            }
        }
        // 发送量<header长度：说明header还没发完
        else {
            iov_[0].iov_base = (uint8_t*)iov_[0].iov_base + len; 
            iov_[0].iov_len -= len; 
            writeBuff_.Retrieve(len);   // 更新写缓冲区
        }
    } while(isET || ToWriteBytes() > 10240);    // 如果是ET就一直写，如果是LT剩余数据>10KB也继续写，减少epoll触发次数
    return len;
}

// 处理 HTTP 请求并生成响应（处理buffer的数据）
bool HttpConn::process() {
    request_.Init();    // 重置request对象，清空之前的状态
    if(readBuff_.ReadableBytes() <= 0) {    // 缓冲区里可以读取的字节数 <= 0
        return false;   // 处理失败 or 没收到完整请求
    }
    else if(request_.parse(readBuff_)) {    // 解析 HTTP 请求（请求行、头部、body）
        LOG_DEBUG("%s", request_.path().c_str());   // 打印请求路径
        if (request_.path() == "/rank/top") {
            std::string json = rankingService.getTopN(10);
            response_.Init(srcDir, "", request_.IsKeepAlive(), 200);
            response_.SetBody(json);
        }
        else  {
            response_.Init(srcDir, request_.path(), request_.IsKeepAlive(), 200);   // 初始化响应对象
        }
    }
    else {
        response_.Init(srcDir, request_.path(), false, 400);    // 解析失败
    }

    // 生成响应数据，写入写缓冲区
    response_.MakeResponse(writeBuff_);
    /* 响应头 */
    iov_[0].iov_base = const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len = writeBuff_.ReadableBytes();
    iovCnt_ = 1;    // 当前只有一个缓冲区

    // 如果有文件内容，也加入iovec
    if(response_.FileLen() > 0  && response_.File()) {
        iov_[1].iov_base = response_.File();
        iov_[1].iov_len = response_.FileLen();
        iovCnt_ = 2;
    } else if (response_.BodyLen() > 0) {
        iov_[1].iov_base = const_cast<char*>(response_.Body());
        iov_[1].iov_len = response_.BodyLen();
        iovCnt_ = 2;
    }
    LOG_DEBUG("filesize:%d, %d  to %d", response_.FileLen() , iovCnt_, ToWriteBytes());
    return true;
}