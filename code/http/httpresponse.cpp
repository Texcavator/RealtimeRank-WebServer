/*
 * @Author       : mark
 * @Date         : 2020-06-27
 * @copyleft Apache 2.0
 */ 
#include "httpresponse.h"

using namespace std;

// 文件后缀->Content-Type（决定如何显示内容） 映射
const unordered_map<string, string> HttpResponse::SUFFIX_TYPE = {
    { ".html",  "text/html" },
    { ".xml",   "text/xml" },
    { ".xhtml", "application/xhtml+xml" },
    { ".txt",   "text/plain" },
    { ".rtf",   "application/rtf" },
    { ".pdf",   "application/pdf" },
    { ".word",  "application/nsword" },
    { ".png",   "image/png" },
    { ".gif",   "image/gif" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".au",    "audio/basic" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".avi",   "video/x-msvideo" },
    { ".gz",    "application/x-gzip" },
    { ".tar",   "application/x-tar" },
    { ".css",   "text/css "},
    { ".js",    "text/javascript "},
};

// 状态码->状态描述 映射
const unordered_map<int, string> HttpResponse::CODE_STATUS = {
    { 200, "OK" },
    { 400, "Bad Request" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
};

// 状态码->错误页路径 映射
const unordered_map<int, string> HttpResponse::CODE_PATH = {
    { 400, "/400.html" },
    { 403, "/403.html" },
    { 404, "/404.html" },
};

HttpResponse::HttpResponse() {
    code_ = -1;
    path_ = srcDir_ = "";
    isKeepAlive_ = false;
    mmFile_ = nullptr; 
    mmFileStat_ = { 0 };
};

HttpResponse::~HttpResponse() {
    UnmapFile();
}

// 初始化
void HttpResponse::Init(const string& srcDir, string& path, bool isKeepAlive, int code){
    assert(srcDir != "");
    if(mmFile_) { UnmapFile(); }
    code_ = code;
    isKeepAlive_ = isKeepAlive;
    path_ = path;
    srcDir_ = srcDir;
    mmFile_ = nullptr; 
    mmFileStat_ = { 0 };
}

// 根据请求的路径准备HTTP响应，把状态码、头信息和内容写入buffer
void HttpResponse::MakeResponse(Buffer& buff) {
    // 文件不存在在or是一个目录 -> 返回404
    // (srcDir_ + path_) 是请求的完整路径
    // stat()用于获取文件信息，返回<0表示文件不存在或无法访问
    // S_ISDIR(mmFileStat_.st_mode) 判断路径是否是一个目录
    // mmFileStat_.st_mode存储文件类型和权限
    if(stat((srcDir_ + path_).data(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode)) {
        code_ = 404;
    }
    // 没有其他用户可读权限，不能给客户端访问
    else if(!(mmFileStat_.st_mode & S_IROTH)) {
        code_ = 403;
    }
    // 没设置状态码，一切正常
    else if(code_ == -1) { 
        code_ = 200; 
    }
    ErrorHtml_();           // 处理错误页面
    AddStateLine_(buff);    // 写状态行
    AddHeader_(buff);       // 写响应头前半部分
    AddContent_(buff);      // 写响应头后半部分，把文件内容映射到内存
}

// 返回文件内容
char* HttpResponse::File() {
    return mmFile_;
}

// 返回文件大小
size_t HttpResponse::FileLen() const {
    return mmFileStat_.st_size;
}

// 把状态码对应的错误页面路径设置好，获取该页面信息
void HttpResponse::ErrorHtml_() {
    if(CODE_PATH.count(code_) == 1) {                   // 判断是否有对应的错误页面
        path_ = CODE_PATH.find(code_)->second;          // 设置路径
        stat((srcDir_ + path_).data(), &mmFileStat_);   // 获取错误页面文件信息
    }
}

// 写状态行
void HttpResponse::AddStateLine_(Buffer& buff) {
    string status;
    if(CODE_STATUS.count(code_) == 1) {                 // 判断是否有对应的状态描述
        status = CODE_STATUS.find(code_)->second;       // 有就设置
    }
    else {                                              // 没有对应的状态描述
        code_ = 400;
        status = CODE_STATUS.find(400)->second;
    }
    // 把状态行写入buffer
    buff.Append("HTTP/1.1 " + to_string(code_) + " " + status + "\r\n");
}

// 写响应头前半部分
void HttpResponse::AddHeader_(Buffer& buff) {
    // 是否保持长连接
    buff.Append("Connection: ");
    if(isKeepAlive_) {
        buff.Append("keep-alive\r\n");
        buff.Append("keep-alive: max=6, timeout=120\r\n");
    } else{
        buff.Append("close\r\n");
    }
    // 文件类型
    buff.Append("Content-type: " + GetFileType_() + "\r\n");
}

// 写响应头的Content-length，把文件内容映射到内存
void HttpResponse::AddContent_(Buffer& buff) {
    // 只读打开文件
    int srcFd = open((srcDir_ + path_).data(), O_RDONLY);
    if(srcFd < 0) {     // 打开失败，直接生成错误界面
        ErrorContent(buff, "File NotFound!");
        return; 
    }

    LOG_DEBUG("file path %s", (srcDir_ + path_).data());
    // mmap: 把文件映射到内存地址空间，提高访问速度，减少系统调用
    // 返回映射区域所在起始地址
        // 0: 内核自己选择映射地址
        // mmFileStat_.st_size: 映射文件大小
        // PROT_READ: 只读
        // MAP_PRIVATE: 写入时拷贝（不修改原文件）
        // srcFd: 文件描述符
        // 0: 从文件开头映射
    mmFile_ = (char*)mmap(0, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
    if(mmFile_ == MAP_FAILED) {   // 映射失败，直接生成错误界面
        ErrorContent(buff, "File NotFound!");
        return; 
    }
    close(srcFd);
    // 把响应头补充完整
    buff.Append("Content-length: " + to_string(mmFileStat_.st_size) + "\r\n\r\n");
}

// 解除文件内容到内存的映射
void HttpResponse::UnmapFile() {
    if(mmFile_) {
        munmap(mmFile_, mmFileStat_.st_size);   // 解除内存映射
        mmFile_ = nullptr;
    }
}

string HttpResponse::GetFileType_() {
    // 找到最后一个.的位置
    // string::size_type专门用来处理字符串长度
    string::size_type idx = path_.find_last_of('.');
    // 文件名没有. 说明没有后缀，浏览器按普通文本处理
    if(idx == string::npos) {
        return "text/plain";
    }
    // 提取后缀
    string suffix = path_.substr(idx);
    if(SUFFIX_TYPE.count(suffix) == 1) {    // 有这样的映射
        return SUFFIX_TYPE.find(suffix)->second;
    }
    return "text/plain";    // 没有这样的映射
}

// 生成错误界面
void HttpResponse::ErrorContent(Buffer& buff, string message) 
{
    string body;        // 最终html界面内容
    string status;      // HTTP状态文本
    body += "<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    if(CODE_STATUS.count(code_) == 1) {             // 有这样的状态映射
        status = CODE_STATUS.find(code_)->second;
    } else {                                        // 没有这样的状态映射
        status = "Bad Request";
    }
    // 拼接错误信息
    body += to_string(code_) + " : " + status  + "\n";
    body += "<p>" + message + "</p>";
    body += "<hr><em>TinyWebServer</em></body></html>";

    // 写剩余的响应头
    buff.Append("Content-length: " + to_string(body.size()) + "\r\n\r\n");
    // 写响应体
    buff.Append(body);
}
