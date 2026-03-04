/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */ 
#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <semaphore.h>
#include <thread>
#include "../log/log.h"

class SqlConnPool {
public:
    // 全局只维护一个连接池
    static SqlConnPool *Instance();

    MYSQL *GetConn();
    void FreeConn(MYSQL * conn);
    int GetFreeConnCount();

    void Init(const char* host, int port,
              const char* user,const char* pwd, 
              const char* dbName, int connSize);
    void ClosePool();

private:
    SqlConnPool();
    ~SqlConnPool();

    int MAX_CONN_;                  // 连接池最大连接数
    int useCount_;                  // 已使用连接数
    int freeCount_;                 // 空闲连接数

    std::queue<MYSQL *> connQue_;   // 连接队列
    std::mutex mtx_;                // 队列互斥锁
    sem_t semId_;                   // 信号量，用于控制可用连接数量
};


#endif // SQLCONNPOOL_H