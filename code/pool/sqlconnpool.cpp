/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */ 

#include "sqlconnpool.h"
using namespace std;

SqlConnPool::SqlConnPool() {
    useCount_ = 0;
    freeCount_ = 0;
}

// 单例实现，需要的时候才创建，且全局只创建一个，返回类对象
SqlConnPool* SqlConnPool::Instance() {
    static SqlConnPool connPool;
    return &connPool;
}

// 初始化
void SqlConnPool::Init(const char* host, int port,
            const char* user,const char* pwd, const char* dbName,
            int connSize = 10) {
    assert(connSize > 0);
    // 循环创建connSize个sql连接
    for (int i = 0; i < connSize; i++) {
        MYSQL *sql = nullptr;
        sql = mysql_init(sql);
        if (!sql) {     // 创建失败
            LOG_ERROR("MySql init error!");
            assert(sql);    // 中断程序运行
        }
        sql = mysql_real_connect(sql, host,
                                 user, pwd,
                                 dbName, port, nullptr, 0);
        if (!sql) {
            LOG_ERROR("MySql Connect error!");
        }
        connQue_.push(sql);
    }
    MAX_CONN_ = connSize;
    // 初始化信号量
    // （信号量，信号量是否在进程间共享（0表示线程内共享），信号量初始值）
    sem_init(&semId_, 0, MAX_CONN_);
}

// 从队列里取一个空闲的 MySQL 连接给调用者使用
MYSQL* SqlConnPool::GetConn() {
    MYSQL *sql = nullptr;
    if(connQue_.empty()){       // 没有空闲连接
        LOG_WARN("SqlConnPool busy!");
        return nullptr;
    }
    sem_wait(&semId_);  // 等待信号量，尝试获取资源
    // 加大括号是为了限定lock_guard的作用域
    {
        lock_guard<mutex> locker(mtx_);
        sql = connQue_.front();
        connQue_.pop();
    }
    return sql;
}

// 用完后把连接放回队列
void SqlConnPool::FreeConn(MYSQL* sql) {
    assert(sql);
    lock_guard<mutex> locker(mtx_); // 加锁
    connQue_.push(sql);
    sem_post(&semId_);      // 释放资源，通知等待的线程
}

// 关闭所有连接并释放资源
void SqlConnPool::ClosePool() {
    lock_guard<mutex> locker(mtx_);
    while(!connQue_.empty()) {
        auto item = connQue_.front();
        connQue_.pop();
        mysql_close(item);
    }
    mysql_library_end();  // 释放客户端库占用的全局资源
}

// 获取空闲连接数
int SqlConnPool::GetFreeConnCount() {
    lock_guard<mutex> locker(mtx_); // 加锁
    return connQue_.size();
}

SqlConnPool::~SqlConnPool() {
    ClosePool();
}
