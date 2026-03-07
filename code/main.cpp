/*
 * @Author       : mark
 * @Date         : 2020-06-18
 * @copyleft Apache 2.0
 */ 
#include <unistd.h>
#include "server/webserver.h"
#include "ranking/RankingService.h"

RankingService rankingService;

int main() {
    /* 守护进程 后台运行 */
    //daemon(1, 0); 

    // 初始化随机数种子
    srand(time(nullptr));

    // 启动自动更新销量线程
    std::thread([](){
        while(true) {
            auto items = rankingService.getRange(0, 99);  // 获取排行榜前100个商品
            if (!items.empty()) {
                int idx = rand() % items.size();
                int product_id = std::stoi(items[idx].product_id);  // 获取商品ID
                int delta = rand() % 10 + 1;  // 每次增加1~10销量
                rankingService.updateScore(product_id, delta);
            }
            std::this_thread::sleep_for(std::chrono::seconds(1)); // 每秒更新一次
        }
    }).detach(); // 分离线程，主线程继续跑HTTP服务器

    WebServer server(
        1316, 3, 60000, false,             /* 端口 ET模式 timeoutMs 优雅退出  */
        3306, "root", "root", "webserver", /* Mysql配置 */
        12, 6, true, 0, 1024);             /* 连接池数量 线程池数量 日志开关 日志等级 日志异步队列容量 */
    server.Start();
} 
  