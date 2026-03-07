/*
 * @Author       : mark
 * @Date         : 2020-06-15
 * @copyleft Apache 2.0
 */ 

#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/types.h>
#include <sys/uio.h>     // readv/writev
#include <arpa/inet.h>   // sockaddr_in
#include <stdlib.h>      // atoi()
#include <errno.h>      

#include "../log/log.h"
#include "../pool/sqlconnRAII.h"
#include "../buffer/buffer.h"
#include "httprequest.h"
#include "httpresponse.h"
#include "../ranking/RankingService.h"

class HttpConn {
public:
    HttpConn();

    ~HttpConn();

    void init(int sockFd, const sockaddr_in& addr);

    ssize_t read(int* saveErrno);

    ssize_t write(int* saveErrno);

    void Close();

    int GetFd() const;

    int GetPort() const;

    const char* GetIP() const;
    
    sockaddr_in GetAddr() const;
    
    bool process();

    bool HandleRankTop();

    // 返回还有多少字节没写
    int ToWriteBytes() { 
        return iov_[0].iov_len + iov_[1].iov_len; 
    }

    bool IsKeepAlive() const {
        return request_.IsKeepAlive();
    }

    static bool isET;                   // 是否ET模式
    static const char* srcDir;          // 静态资源目录
    // atpmic 原子类型 对它的读写不可被打断，一次只能有一个线程修改，相当于加锁了
    static std::atomic<int> userCount;  // 当前连接数
    
private:
   
    int fd_;                    // 保存socket
    struct  sockaddr_in addr_;  // 保存客户端地址

    bool isClose_;              // 是否已经关闭
    
    int iovCnt_;                // 当前有效的iovec（内含内存起始地址、内存长度）数量
    // iov[0]: http响应头header
    // iov[1]: 文件内容file
    struct iovec iov_[2];
    
    Buffer readBuff_;           // 读缓冲区
    Buffer writeBuff_;          // 写缓冲区

    HttpRequest request_;       // 解析客户端请求
    HttpResponse response_;     // 构造响应报文
};


#endif //HTTP_CONN_H