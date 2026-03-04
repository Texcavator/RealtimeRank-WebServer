/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */ 
#include "buffer.h"

Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0) {}

// 返回可读区域大小
size_t Buffer::ReadableBytes() const {
    return writePos_ - readPos_;
}

// 返回可写区域大小
size_t Buffer::WritableBytes() const {
    return buffer_.size() - writePos_;
}

// 返回预留区域大小，在需要扩容时可能会被利用
size_t Buffer::PrependableBytes() const {
    return readPos_;
}

// 返回当前可读数据的起始地址
const char* Buffer::Peek() const {
    return BeginPtr_() + readPos_;
}

// 将读指针置于还没读的第一个位置处（删除已经读过的，逻辑删除代替物理删除）
void Buffer::Retrieve(size_t len) {
    assert(len <= ReadableBytes());
    readPos_ += len;
}

// 将读指针挪到end后的第一个位置
void Buffer::RetrieveUntil(const char* end) {
    assert(Peek() <= end );
    Retrieve(end - Peek());
}

// buffer全部清空
void Buffer::RetrieveAll() {
    bzero(&buffer_[0], buffer_.size());
    readPos_ = 0;
    writePos_ = 0;
}

// 把当前缓冲区里所有可读数据拷贝成一个string，然后清空缓冲区
std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes());  // 从s指向的内存开始，拷贝n个字节，构造一个字符串。
    RetrieveAll();
    return str;
}

// 返回当前可写区的起始地址
const char* Buffer::BeginWriteConst() const {
    return BeginPtr_() + writePos_;
}
char* Buffer::BeginWrite() {
    return BeginPtr_() + writePos_;
}

// 写指针后移len
void Buffer::HasWritten(size_t len) {
    writePos_ += len;
} 

/*
    Append：将数据写入buffer
*/
void Buffer::Append(const std::string& str) {   // 这里的&是传引用
    Append(str.data(), str.length());
}

void Buffer::Append(const void* data, size_t len) {
    assert(data);
    Append(static_cast<const char*>(data), len);    // 强转类型
}

void Buffer::Append(const char* str, size_t len) {
    assert(str);
    EnsureWriteable(len);                       // 确保可写区域够写
    std::copy(str, str + len, BeginWrite());    // 写数据
    HasWritten(len);                            // 更改写指针
}

void Buffer::Append(const Buffer& buff) {       // 将一个buffer接到另一个buffer后
    Append(buff.Peek(), buff.ReadableBytes());
}

// 检查可写区域剩余空间，不够就扩容
void Buffer::EnsureWriteable(size_t len) {
    if(WritableBytes() < len) {     // 剩余可写区域不够写
        MakeSpace_(len);
    }
    assert(WritableBytes() >= len);
}

// 把fd的数据读到buffer里
ssize_t Buffer::ReadFd(int fd, int* saveErrno) {
    char buff[65535];   // 65535理论最大单次报文级别大小
    struct iovec iov[2];
    const size_t writable = WritableBytes();
    // [0] 写入Buffer剩余可写区
    iov[0].iov_base = BeginPtr_() + writePos_;
    iov[0].iov_len = writable;
    // [1] 栈上的临时数组
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    // 分散读：一次系统调用，把数据写入多个不连续内存区域
    // 返回写了多少数据
    const ssize_t len = readv(fd, iov, 2);  
    if(len < 0) {   // 读失败
        *saveErrno = errno;
    }
    else if(static_cast<size_t>(len) <= writable) {     // 数据全部写入 buffer_ 的可写区
        writePos_ += len;
    }
    else {      // 有一部分写到了 buff
        writePos_ = buffer_.size();     // buffer 可写区填满
        Append(buff, len - writable);   // 把 buff 里的“溢出数据”追加到 buffer
    }
    return len;
}

// 把buffer可读区的数据写入fd（返回写了多少）
ssize_t Buffer::WriteFd(int fd, int* saveErrno) {
    size_t readSize = ReadableBytes();
    // 返回已经写进去的数据长度（不一定一次就能把readSize写完）
    ssize_t len = write(fd, Peek(), readSize);
    if(len < 0) {    // 错了
        *saveErrno = errno;
        return len;
    } 
    readPos_ += len;    // 更新读指针
    return len;
}

// 返回vector底层数组的起始地址
char* Buffer::BeginPtr_() {
    return &*buffer_.begin();
}
const char* Buffer::BeginPtr_() const {
    return &*buffer_.begin();
}

// 扩容使这个buffer可以写进去len的数据
void Buffer::MakeSpace_(size_t len) {
    // 就算利用已读区也不够：重新分配空间
    if(WritableBytes() + PrependableBytes() < len) {   
        buffer_.resize(writePos_ + len + 1);
    } 
    else {
        size_t readable = ReadableBytes();
        // 把可读区整体往前挪到buffer最前面
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());
        readPos_ = 0;
        writePos_ = readPos_ + readable;
        assert(readable == ReadableBytes());
    }
}