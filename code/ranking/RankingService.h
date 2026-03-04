#pragma once
#include <hiredis/hiredis.h>
#include <string>
#include <vector>

struct Item {
    std::string product_id;
    long score;
};

class RankingService {
public:
    RankingService(const std::string& host = "127.0.0.1", int port = 6379);
    ~RankingService();

    void updateScore(int product_id, int count);
    std::vector<Item> getTopN(int n);
    int getRank(int product_id);
    std::vector<Item> getRange(int start, int end);

private:
    redisContext* conn_;
};