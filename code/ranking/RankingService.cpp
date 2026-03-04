#include "RankingService.h"
#include <iostream>

RankingService::RankingService(const std::string& host, int port) {
    conn_ = redisConnect(host.c_str(), port);
    if (conn_ == nullptr || conn_->err) {
        if (conn_) {
            std::cerr << "Redis connection error: " << conn_->errstr << std::endl;
        } else {
            std::cerr << "Redis connection allocation error" << std::endl;
        }
        exit(1);
    }
}

RankingService::~RankingService() {
    if (conn_) redisFree(conn_);
}

void RankingService::updateScore(int product_id, int count) {
    std::string key = "rank:product:realtime";
    std::string member = "product_" + std::to_string(product_id);

    redisReply* reply = (redisReply*)redisCommand(conn_, "ZINCRBY %s %d %s",
                                                  key.c_str(), count, member.c_str());
    freeReplyObject(reply);
}

std::vector<Item> RankingService::getTopN(int n) {
    std::vector<Item> result;
    std::string key = "rank:product:realtime";
    redisReply* reply = (redisReply*)redisCommand(conn_, "ZREVRANGE %s 0 %d WITHSCORES",
                                                  key.c_str(), n - 1);
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; i += 2) {
            Item item;
            item.product_id = reply->element[i]->str;
            item.score = std::stol(reply->element[i+1]->str);
            result.push_back(item);
        }
    }
    freeReplyObject(reply);
    return result;
}

int RankingService::getRank(int product_id) {
    std::string key = "rank:product:realtime";
    std::string member = "product_" + std::to_string(product_id);
    redisReply* reply = (redisReply*)redisCommand(conn_, "ZREVRANK %s %s", key.c_str(), member.c_str());
    int rank = -1;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        rank = reply->integer;
    }
    freeReplyObject(reply);
    return rank; // 0 是第一名
}

std::vector<Item> RankingService::getRange(int start, int end) {
    std::vector<Item> result;
    std::string key = "rank:product:realtime";
    redisReply* reply = (redisReply*)redisCommand(conn_, "ZREVRANGE %s %d %d WITHSCORES",
                                                  key.c_str(), start, end);
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; i += 2) {
            Item item;
            item.product_id = reply->element[i]->str;
            item.score = std::stol(reply->element[i+1]->str);
            result.push_back(item);
        }
    }
    freeReplyObject(reply);
    return result;
}