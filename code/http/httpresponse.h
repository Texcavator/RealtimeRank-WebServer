/*
 * @Author       : mark
 * @Date         : 2020-06-25
 * @copyleft Apache 2.0
 */ 
#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <unordered_map>
#include <fcntl.h>       // open
#include <unistd.h>      // close
#include <sys/stat.h>    // stat
#include <sys/mman.h>    // mmap, munmap

#include "../buffer/buffer.h"
#include "../log/log.h"

class HttpResponse {
public:
    HttpResponse();
    ~HttpResponse();

    void Init(const std::string& srcDir, const std::string& path, bool isKeepAlive = false, int code = -1);
    void MakeResponse(Buffer& buff);
    void UnmapFile();
    char* File();
    size_t FileLen() const;
    void ErrorContent(Buffer& buff, std::string message);
    int Code() const { return code_; }
    void SetBody(const std::string& body);
    const int BodyLen();

    const char *Body() const;

private:
    void AddStateLine_(Buffer &buff);
    void AddHeader_(Buffer &buff);
    void AddContent_(Buffer &buff);

    void ErrorHtml_();
    std::string GetFileType_();

    int code_;                  // HTTP状态码
    bool isKeepAlive_;          // 是否长连接

    std::string path_;          // 请求的文件路径
    std::string srcDir_;        // 服务器静态文件目录
    
    char* mmFile_;              // 映射到内存后的文件内容
    struct stat mmFileStat_;    // 文件信息

    std::string body_;          // 动态设置的响应体

    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;  // 文件后缀->Content-Type（决定如何显示内容） 映射
    static const std::unordered_map<int, std::string> CODE_STATUS;          // 状态码->状态描述 映射
    static const std::unordered_map<int, std::string> CODE_PATH;            // 状态码->错误页路径 映射
};


#endif //HTTP_RESPONSE_H