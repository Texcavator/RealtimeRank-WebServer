/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */ 
#include "httprequest.h"
using namespace std;

// 允许默认访问的html
const unordered_set<string> HttpRequest::DEFAULT_HTML{
            "/index", "/register", "/login",
             "/welcome", "/video", "/picture", };

// 页面类型标记（就是把页面路径转换成数字）
const unordered_map<string, int> HttpRequest::DEFAULT_HTML_TAG {
            {"/register.html", 0}, {"/login.html", 1},  };

// 构造函数初始化
void HttpRequest::Init() {
    method_ = path_ = version_ = body_ = "";
    state_ = REQUEST_LINE;
    header_.clear();
    post_.clear();
}

// 判断HTTP请求是否要求使用长连接
bool HttpRequest::IsKeepAlive() const {
    if(header_.count("Connection") == 1) {
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

// 状态机解析：从huffer中解析http请求
bool HttpRequest::parse(Buffer& buff) {
    const char CRLF[] = "\r\n";     // 寻找每行结束的位置
    if(buff.ReadableBytes() <= 0) { // buffer没有数据
        return false;
    }
    // buffer还有数据可读 + 状态机没到finish
    while(buff.ReadableBytes() && state_ != FINISH) {
        // 在前面的区间里找后面的子区间 找到返回起始位置
        const char* lineEnd = search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);
        // 这一行的字符串（不包括\r\n）
        std::string line(buff.Peek(), lineEnd);
        switch(state_)
        {
        case REQUEST_LINE:
            if(!ParseRequestLine_(line)) {
                return false;
            }
            ParsePath_();
            break;    
        case HEADERS:
            ParseHeader_(line);
            if(buff.ReadableBytes() <= 2) { // buffer只剩下\r\n
                state_ = FINISH;            // 说明请求头结束
            }
            break;
        case BODY:
            ParseBody_(line);
            break;
        default:
            break;
        }
        // 在当前buffer没找到一整行（\r\n），数据没接收完整，先退出解析等下一次epoll继续
        if(lineEnd == buff.BeginWrite()) { break; }
        // 把已解析的这一行从buffer里删掉
        buff.RetrieveUntil(lineEnd + 2);
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}

// 补全网址（加.html）
void HttpRequest::ParsePath_() {
    if(path_ == "/") {
        path_ = "/index.html"; 
    }
    else {
        for(auto &item: DEFAULT_HTML) {
            if(item == path_) {
                path_ += ".html";
                break;
            }
        }
    }
}

// 标准HTTP请求：
// 请求行\r\n
// 头1\r\n
// 头2\r\n
// 头3\r\n
// \r\n
// Body（可选）

// 解析请求行
bool HttpRequest::ParseRequestLine_(const std::string& line) {
    // 找第一个空格
    size_t pos1 = line.find(' ');
    if(pos1 == std::string::npos) return false;

    // 找第二个空格
    size_t pos2 = line.find(' ', pos1 + 1);
    if(pos2 == std::string::npos) return false;

    // 拆分
    method_ = line.substr(0, pos1);
    path_ = line.substr(pos1 + 1, pos2 - pos1 - 1);

    // HTTP/1.1 → 只取 1.1
    if(line.compare(pos2 + 1, 5, "HTTP/") != 0)
        return false;

    version_ = line.substr(pos2 + 6);

    state_ = HEADERS;
    return true;
}

// 解析请求头
void HttpRequest::ParseHeader_(const std::string& line) {
    // 空行，说明 header 结束
    if(line.empty()) {
        state_ = BODY;
        return;
    }

    // 找冒号
    size_t pos = line.find(':');
    if(pos == std::string::npos) {
        return;  // 格式错误可以选择忽略
    }

    // 拆 key
    std::string key = line.substr(0, pos);

    // 跳过冒号后的空格
    size_t valueStart = pos + 1;
    if(valueStart < line.size() && line[valueStart] == ' ') {
        valueStart++;
    }

    // 拆 value
    std::string value = line.substr(valueStart);

    header_[key] = value;
}

// 解析请求体
void HttpRequest::ParseBody_(const string& line) {
    body_ = line;
    ParsePost_();
    state_ = FINISH;
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}

// 十六进制转换成整数
int HttpRequest::ConverHex(char ch) {
    if(ch >= 'A' && ch <= 'F') return ch -'A' + 10;
    if(ch >= 'a' && ch <= 'f') return ch -'a' + 10;
    return ch;
}

// 处理POST请求的表单数据，根据业务逻辑决定跳转哪个界面
void HttpRequest::ParsePost_() {
    // 是 POST 请求 + 表单类型是普通表单（不是 JSON）
    if(method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded") {
        // 解析表单数据
        ParseFromUrlencoded_();
        // 如果当前访问的路径在表单里
        if(DEFAULT_HTML_TAG.count(path_)) {
            // 取出当前页面对应的业务类型
            int tag = DEFAULT_HTML_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d", tag);
            if(tag == 0 || tag == 1) {  // 判断登录还是注册
                bool isLogin = (tag == 1);
                if(UserVerify(post_["username"], post_["password"], isLogin)) {
                    path_ = "/welcome.html";
                } 
                else {
                    path_ = "/error.html";
                }
            }
        }
    }   
}

// 处理这种格式的body:username=zhangsan&password=123456
void HttpRequest::ParseFromUrlencoded_() {
    // body为空
    if(body_.size() == 0) { return; }

    string key, value;
    int num = 0;
    int n = body_.size();
    // j: 起始位置 i: 结束位置
    int i = 0, j = 0;

    for(; i < n; i++) {
        char ch = body_[i];
        switch (ch) {
        case '=':   // 前面是key
            key = body_.substr(j, i - j);
            j = i + 1;
            break;
        case '+':   // 此编码格式里，空格会被编码成+
            body_[i] = ' ';
            break;
        case '%':   // URL编码里会将特殊字符编码成%和两个十六进制数
            num = ConverHex(body_[i + 1]) * 16 + ConverHex(body_[i + 2]);
            body_[i + 2] = num % 10 + '0';
            body_[i + 1] = num / 10 + '0';
            i += 2;
            break;
        case '&':   // 前面是value
            value = body_.substr(j, i - j);
            j = i + 1;
            post_[key] = value;
            LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
            break;
        default:
            break;
        }
    }
    assert(j <= i);
    // 补充最后一个键值对
    if(post_.count(key) == 0 && j < i) {
        value = body_.substr(j, i - j);
        post_[key] = value;
    }
}

// 用户验证，返回是否成功
bool HttpRequest::UserVerify(const string &name, const string &pwd, bool isLogin) {
    if(name == "" || pwd == "") { return false; }
    LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());

    // 从连接池里拿一个 MySQL 连接
    // RAII管理数据库连接:保证函数结束时自动归还
    MYSQL* sql;
    SqlConnRAII(&sql,  SqlConnPool::Instance());
    assert(sql);
    
    bool flag = false;              // 用户名是否可用
    unsigned int j = 0;             // 查询结果有多少列
    char order[256] = { 0 };        // 查询语句
    MYSQL_FIELD *fields = nullptr;  // 查询结果每列代表什么
    MYSQL_RES *res = nullptr;       // 查询结果
    
    if(!isLogin) { flag = true; }   // 如果是注册的话，默认用户名可用
    // 查询用户名是否存在，结果放在order里，最大写入长度256
    snprintf(order, 256, "SELECT username, password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    LOG_DEBUG("%s", order);

    // 返回非0表示查询失败
    if(mysql_query(sql, order)) { 
        mysql_free_result(res);
        return false; 
    }
    res = mysql_store_result(sql);      // 获取查询结果
    j = mysql_num_fields(res);          // 获取列的数量
    fields = mysql_fetch_fields(res);   // 获取列结构信息

    while(MYSQL_ROW row = mysql_fetch_row(res)) {   // 逐行读取数据
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);
        string password(row[1]);
        // 登录
        if(isLogin) {
            if(pwd == password) { flag = true; }
            else {
                flag = false;
                LOG_DEBUG("pwd error!");
            }
        } // 注册（但用户名已经被使用）
        else { 
            flag = false; 
            LOG_DEBUG("user used!");
        }
    }
    mysql_free_result(res);

    // 注册行为 且 用户名未被使用
    if(!isLogin && flag == true) {
        LOG_DEBUG("regirster!");
        // sql insert命令
        bzero(order, 256);
        snprintf(order, 256,"INSERT INTO user(username, password) VALUES('%s','%s')", name.c_str(), pwd.c_str());
        LOG_DEBUG( "%s", order);
        if(mysql_query(sql, order)) { 
            LOG_DEBUG( "Insert error!");
            flag = false; 
        }
        flag = true;
    }
    SqlConnPool::Instance()->FreeConn(sql);
    LOG_DEBUG( "UserVerify success!!");
    return flag;
}

// 获取path（只读）
std::string HttpRequest::path() const{
    return path_;
}
// 获取path（可写）
std::string& HttpRequest::path(){
    return path_;
}
// 获取method
std::string HttpRequest::method() const {
    return method_;
}
// 获取version
std::string HttpRequest::version() const {
    return version_;
}

// 获取post请求体中key对应的value
std::string HttpRequest::GetPost(const std::string& key) const {
    assert(key != "");
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}
std::string HttpRequest::GetPost(const char* key) const {
    assert(key != nullptr);
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}