#pragma once
#include <hiredis/hiredis.h>
#include <string>
#include <vector>
#include "../log/log.h"

struct Item {
    std::string product_id;
    long score;
};

class RankingService {
public:
    RankingService(const std::string& host = "127.0.0.1", int port = 6379);
    ~RankingService();

    void updateScore(int product_id, int count);
    std::string getTopN(int n);
    std::string BuildRankJson(const std::vector<Item>& items);
    int getRank(int product_id);
    std::vector<Item> getRange(int start, int end);
    void InitTestData();

private:
    redisContext* conn_;
    std::string key_;   // ZSET的key
};