/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */ 
#include "log.h"

using namespace std;

Log::Log() {
    lineCount_ = 0;
    isAsync_ = false;
    writeThread_ = nullptr;
    deque_ = nullptr;
    toDay_ = 0;
    fp_ = nullptr;
}

Log::~Log() {
    // 如果当前thread拥有一个正在执行的线程
    if(writeThread_ && writeThread_->joinable()) {
        while(!deque_->empty()) {
            deque_->flush();    // 唤醒线程，让日志能被写完
        };
        deque_->Close();        // 清空队列
        writeThread_->join();   // 主线程等待当前线程执行结束
    }
    if(fp_) {
        lock_guard<mutex> locker(mtx_); // 加锁
        flush();                        // 把缓冲区数据立刻写到磁盘
        fclose(fp_);                    // 关闭文件
    }
}

// 获取日志级别
int Log::GetLevel() {
    lock_guard<mutex> locker(mtx_);
    return level_;
}

// 设置日志级别
void Log::SetLevel(int level) {
    lock_guard<mutex> locker(mtx_);
    level_ = level;
}

// 初始化
void Log::init(int level = 1, const char* path, const char* suffix,
    int maxQueueSize) {
    isOpen_ = true;
    level_ = level;
    if(maxQueueSize > 0) {  // 异步
        isAsync_ = true;
        if(!deque_) {
            unique_ptr<BlockDeque<std::string>> newDeque(new BlockDeque<std::string>);
            deque_ = move(newDeque);
            
            std::unique_ptr<std::thread> NewThread(new thread(FlushLogThread));
            writeThread_ = move(NewThread);
        }
    } else {                // 同步
        isAsync_ = false;
    }

    lineCount_ = 0;

    time_t timer = time(nullptr);           // 获取当前时间戳
    struct tm *sysTime = localtime(&timer); // 把时间戳转换为本地时间结构体
    struct tm t = *sysTime;
    path_ = path;       // 保存路径
    suffix_ = suffix;   // 保存后缀
    // 拼接文件名
    char fileName[LOG_NAME_LEN] = {0};
    snprintf(fileName, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s", 
            path_, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, suffix_);
    toDay_ = t.tm_mday;

    {
        lock_guard<mutex> locker(mtx_);
        buff_.RetrieveAll();    // 清空日志缓冲区，防止旧日志残留
        if(fp_) {           // 如果文件已经打开
            flush();        // 旧文件缓冲区刷掉
            fclose(fp_);    // 关闭旧文件
        }

        fp_ = fopen(fileName, "a");     // 打开新文件，追加写入
        if(fp_ == nullptr) {            // 目录不存在
            mkdir(path_, 0777);         // 创建目录(0777是执行权限，表示任何人都可以读写执行)
            fp_ = fopen(fileName, "a"); // dakaiwenjian 
        } 
        assert(fp_ != nullptr);
    }
}

void Log::write(int level, const char *format, ...) {
    // 获取当前时间（秒、微秒）
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    // 把时间转换为年月日时分秒
    time_t tSec = now.tv_sec;
    struct tm *sysTime = localtime(&tSec);
    struct tm t = *sysTime;
    va_list vaList;

    // 日期变了或者日志行数超过上限：新建一个日志文件
    if (toDay_ != t.tm_mday || (lineCount_ && (lineCount_  %  MAX_LINES == 0)))
    {
        unique_lock<mutex> locker(mtx_);
        locker.unlock();    // 先解锁再上锁，因为拼接字符串不需要上锁
        
        // 文件命名
        char newFile[LOG_NAME_LEN];
        char tail[36] = {0};
        snprintf(tail, 36, "%04d_%02d_%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

        if (toDay_ != t.tm_mday)
        {
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s%s", path_, tail, suffix_);
            toDay_ = t.tm_mday;
            lineCount_ = 0;
        }
        else {
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s-%d%s", path_, tail, (lineCount_  / MAX_LINES), suffix_);
        }
        
        // 处理好之前还没写完的 
        locker.lock();
        flush();
        fclose(fp_);

        fp_ = fopen(newFile, "a");
        assert(fp_ != nullptr);
    }

    {
        unique_lock<mutex> locker(mtx_);
        lineCount_++;
        // n是应该写入的字符数，将日期时间写入buffer
        int n = snprintf(buff_.BeginWrite(), 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                    t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);
                    
        buff_.HasWritten(n);
        // 写日志等级
        AppendLogLevelTitle_(level);

        // 让vaList指向format后面的第一个参数
        va_start(vaList, format);
        // 把format+可变参数，格式化后写入字符串，返回长度
        int m = vsnprintf(buff_.BeginWrite(), buff_.WritableBytes(), format, vaList);
        // 结束可变参数访问（释放内部状态）
        va_end(vaList);

        buff_.HasWritten(m);
        buff_.Append("\n\0", 2);

        if(isAsync_ && deque_ && !deque_->full()) { // 开启异步且队列存在且队列没满：日志丢进队列
            deque_->push_back(buff_.RetrieveAllToStr());
        } else {    // 直接写入文件
            fputs(buff_.Peek(), fp_);
        }
        buff_.RetrieveAll();
    }
}

// 写日志等级
void Log::AppendLogLevelTitle_(int level) {
    switch(level) {
    case 0:
        buff_.Append("[debug]: ", 9);
        break;
    case 1:
        buff_.Append("[info] : ", 9);
        break;
    case 2:
        buff_.Append("[warn] : ", 9);
        break;
    case 3:
        buff_.Append("[error]: ", 9);
        break;
    default:
        buff_.Append("[info] : ", 9);
        break;
    }
}

// 强制刷新（运行期加速）
void Log::flush() {
    if(isAsync_) {          // 异步
        deque_->flush();    // 把deque的内容写到file缓冲区
    }
    fflush(fp_);    // 强制将file缓冲区的内容写到内核
}

// 从队列里不断取出日志字符串，写入文件
void Log::AsyncWrite_() {
    string str = "";
    // 如果队列为空 → 等待
    // 如果有数据 → 取出一条放到 str
    // 如果队列关闭 → 返回 false
    while(deque_->pop(str)) {
        lock_guard<mutex> locker(mtx_);
        fputs(str.c_str(), fp_);
    }
}

// 单例模式
Log* Log::Instance() {
    static Log inst;
    return &inst;
}

// 单例模式调用写日志线程
void Log::FlushLogThread() {
    Log::Instance()->AsyncWrite_();
}