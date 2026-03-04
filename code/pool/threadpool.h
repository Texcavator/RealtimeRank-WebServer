/*
 * @Author       : mark
 * @Date         : 2020-06-15
 * @copyleft Apache 2.0
 */ 

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <functional>
class ThreadPool {
public:
    explicit ThreadPool(size_t threadCount = 8): pool_(std::make_shared<Pool>()) {  // 创建共享pool（所有线程都用这个pool）
            assert(threadCount > 0);
            // 循环创建threadCount个线程
            for(size_t i = 0; i < threadCount; i++) {
                // 创建线程，线程对象调用detach
                std::thread([pool = pool_] {
                    // 带生命周期的锁管理器
                    std::unique_lock<std::mutex> locker(pool->mtx);
                    while(true) {
                        if(!pool->tasks.empty()) {      // 队列里有任务
                            // 取出队头
                            auto task = std::move(pool->tasks.front());
                            pool->tasks.pop();
                            // 解锁 -> 处理任务（其他线程还可以访问tasks和isClosed） -> 加锁
                            locker.unlock();
                            task();
                            locker.lock();
                        } 
                        else if(pool->isClosed) break;   // 已关闭
                        // 没有任务 or 线程池没关闭
                        // 解锁 -> 线程进入阻塞态 -> 等待叫醒（被叫醒之前一直卡在这，CPU去运行别的线程）
                        else pool->cond.wait(locker);    
                    }
                }).detach();    // 创建一个工作线程，让它一直循环取任务执行，并让它在后台独立运行
            }
    }

    ThreadPool() = default;

    // 移动构造函数
    ThreadPool(ThreadPool&&) = default;
    
    ~ThreadPool() {
        if(static_cast<bool>(pool_)) {
            {
                std::lock_guard<std::mutex> locker(pool_->mtx); // 自动上锁（lock_guard出作用域自动解锁）
                pool_->isClosed = true;   // 告诉线程池不要再取新任务了，准备退出
            }
            // 唤醒所有被wait()阻塞的线程
            // 每个线程醒来后会检查：
            // else if(pool->isClosed) break;
            // 条件成立 → 线程退出 while(true) 循环 → 线程结束
            pool_->cond.notify_all();   
        }
    }

    template<class F>
    void AddTask(F&& task) {    // F&&万能引用（既可传左值也可传右值）
        {
            std::lock_guard<std::mutex> locker(pool_->mtx);     // 上锁
            // forward让task保持左值or右值的属性
            pool_->tasks.emplace(std::forward<F>(task));        // 任务放进队列
        }
        pool_->cond.notify_one();   // 唤醒一个正在wait的线程让他去执行任务
    }

private:
    struct Pool {
        std::mutex mtx;                             // 保证tasks和isClosed的原子访问
        std::condition_variable cond;               // 任务队列为空时，优雅等待
        bool isClosed;                              // 标记线程池是否关闭
        std::queue<std::function<void()>> tasks;    // 存储待执行任务
    };
    // pool=pool_ 会增加shared_ptr的引用计数，即使原threadpool析构了，pool_内存不会立刻释放，pool还可以安全访问
    std::shared_ptr<Pool> pool_;
};


#endif //THREADPOOL_H