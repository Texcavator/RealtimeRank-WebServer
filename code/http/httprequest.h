/*
 * @Author       : mark
 * @Date         : 2020-06-25
 * @copyleft Apache 2.0
 */ 
#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>
#include <errno.h>     
#include <mysql/mysql.h>  //mysql

#include "../buffer/buffer.h"
#include "../log/log.h"
#include "../pool/sqlconnpool.h"
#include "../pool/sqlconnRAII.h"

class HttpRequest {
public:
    // 主状态机：当前解析进度
    enum PARSE_STATE {
        REQUEST_LINE,   // 请求行
        HEADERS,        // 请求头
        BODY,
        FINISH,        
    };

    // 解析结果
    enum HTTP_CODE {
        NO_REQUEST = 0,         // 请求还没解析完整
        GET_REQUEST,            // 请求已经解析完成，可以进行业务处理阶段
        BAD_REQUEST,            // 请求语法错误
        NO_RESOURCE,            // 请求资源不存在
        FORBIDDENT_REQUEST,     // 有资源但无权访问
        FILE_REQUEST,           // 请求的是静态文件，可以正常返回
        INTERNAL_ERROR,         // 服务器内部错误
        CLOSED_CONNECTION,      // 客户端主动关闭连接
    };
    
    HttpRequest() { Init(); }
    ~HttpRequest() = default;

    void Init();
    bool parse(Buffer& buff);

    std::string path() const;
    std::string& path();
    std::string method() const;
    std::string version() const;
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;

    bool IsKeepAlive() const;

    /* 
    todo :
    void HttpConn::ParseFormData() {}
    void HttpConn::ParseJson() {}
    */

private:
    bool ParseRequestLine_(const std::string& line);
    void ParseHeader_(const std::string& line);
    void ParseBody_(const std::string& line);

    void ParsePath_();
    void ParsePost_();
    void ParseFromUrlencoded_();

    static bool UserVerify(const std::string& name, const std::string& pwd, bool isLogin);

    PARSE_STATE state_;                                     // 记录当前解析进度
    std::string method_, path_, version_, body_;            // 请求方法、请求路径、http版本、请求体
    std::unordered_map<std::string, std::string> header_;   // 存储请求头
    std::unordered_map<std::string, std::string> post_;     // 存储POST表单字段

    static const std::unordered_set<std::string> DEFAULT_HTML;          // 默认允许访问的HTML页面
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG; // 页面类型标记，根据页面名做不同处理
    static int ConverHex(char ch);  // 十六进制转换成整数
};


#endif //HTTP_REQUEST_H