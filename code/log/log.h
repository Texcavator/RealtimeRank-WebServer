/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */ 
#ifndef LOG_H
#define LOG_H

#include <mutex>
#include <string>
#include <thread>
#include <sys/time.h>
#include <string.h>
#include <stdarg.h>           // vastart va_end
#include <assert.h>
#include <sys/stat.h>         //mkdir
#include "blockqueue.h"
#include "../buffer/buffer.h"

class Log {
public:
    void init(int level, const char* path = "./log", 
                const char* suffix =".log",
                int maxQueueCapacity = 1024);

    static Log* Instance();         // 单例模式
    static void FlushLogThread();

    void write(int level, const char *format,...);
    void flush();

    int GetLevel();
    void SetLevel(int level);
    bool IsOpen() { return isOpen_; }
    
private:
    Log();
    void AppendLogLevelTitle_(int level);
    virtual ~Log();
    void AsyncWrite_();

private:
    static const int LOG_PATH_LEN = 256;    // 目录路径最大长度
    static const int LOG_NAME_LEN = 256;    // 文件名最大长度
    static const int MAX_LINES = 50000;     // 单个文件最大行数

    const char* path_;      // 日志存放目录
    const char* suffix_;    // 文件后缀，默认.log，可改成.txt

    int MAX_LINES_;     // 运行使用时最大行数

    int lineCount_;     // 当前日志文件写了多少行
    int toDay_;         // 当前日志文件属于哪一天

    bool isOpen_;       // 日志系统是否已经初始化
 
    Buffer buff_;       // 临时拼接日志内容的缓冲区

    // 日志级别（只输出大于等于level的）
    // LOG_DEBUG  → 0
    // LOG_INFO   → 1
    // LOG_WARN   → 2
    // LOG_ERROR  → 3
    int level_;

    // 是否开启异步日志
    // true → 使用阻塞队列 + 后台线程
    // false → 直接 fwrite
    bool isAsync_;      

    FILE* fp_;  // 文件控制

    // 阻塞队列 BlockDeque保护线程安全 内部自带加锁机制
    // 生产者（业务线程） → 放日志字符串
    // 消费者（后台线程） → 取日志写文件
    std::unique_ptr<BlockDeque<std::string>> deque_; 
    // 后台写进程，循环从队列取出日志写入文件
    std::unique_ptr<std::thread> writeThread_;

    // 保护共享资源
    std::mutex mtx_;
};

#define LOG_BASE(level, format, ...) \
    do {\
        Log* log = Log::Instance();\
        if (log->IsOpen() && log->GetLevel() <= level) {\
            log->write(level, format, ##__VA_ARGS__); \
            log->flush();\
        }\
    } while(0);

// 更高效 
// do-while把宏包成一个单条语句，防止在 if-else 等语法环境中出错
// ##__VA_ARGS__ 可变参数宏的参数占位符
#define LOG_DEBUG(format, ...) do {LOG_BASE(0, format, ##__VA_ARGS__)} while(0);
#define LOG_INFO(format, ...) do {LOG_BASE(1, format, ##__VA_ARGS__)} while(0);
#define LOG_WARN(format, ...) do {LOG_BASE(2, format, ##__VA_ARGS__)} while(0);
#define LOG_ERROR(format, ...) do {LOG_BASE(3, format, ##__VA_ARGS__)} while(0);

#endif //LOG_H