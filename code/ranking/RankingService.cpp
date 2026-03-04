#include "RankingService.h"
#include <iostream>

RankingService::RankingService(const std::string& host, int port) 
    : key_("rank:product:realtime") {
    conn_ = redisConnect(host.c_str(), port);   // 连接redis
    if (conn_ == nullptr || conn_->err) {
        if (conn_) LOG_ERROR("Redis connection error: %s", conn_->errstr)
        else LOG_ERROR("Redis connection allocation error")
        exit(1);
    }
}

RankingService::~RankingService() {
    if (conn_) redisFree(conn_);    // 释放redis
}

void RankingService::updateScore(int product_id, int count) {
    // Redis ZSET的成员
    std::string member = "product_" + std::to_string(product_id);
    // redisCommand: 通过Redis连接conn_发命令
    redisReply* reply = (redisReply*)redisCommand(conn_, "ZINCRBY %s %d %s",
                                                  key_.c_str(), count, member.c_str());
    // 用完Redis返回的对象必须释放，否则会内存泄漏
    freeReplyObject(reply);
}

std::vector<Item> RankingService::getTopN(int n) {
    std::vector<Item> result;
    redisReply* reply = (redisReply*)redisCommand(conn_, "ZREVRANGE %s 0 %d WITHSCORES",
                                                  key_.c_str(), n - 1);
    // reply不为空 且 返回类型为数组
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
    std::string member = "product_" + std::to_string(product_id);
    redisReply* reply = (redisReply*)redisCommand(conn_, "ZREVRANK %s %s", key_.c_str(), member.c_str());
    int rank = -1;
    // reply不为空 且 返回类型为整数
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        rank = reply->integer;
    }
    freeReplyObject(reply);
    return rank; // 0 是第一名
}

std::vector<Item> RankingService::getRange(int start, int end) {
    std::vector<Item> result;
    redisReply* reply = (redisReply*)redisCommand(conn_, "ZREVRANGE %s %d %d WITHSCORES",
                                                  key_.c_str(), start, end);
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