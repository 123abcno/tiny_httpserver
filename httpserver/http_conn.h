#ifndef HTTPCONN_H
#define HTTPCONN_H

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdarg.h>

int addfd(int epfd,int fd,bool one_shot);
int delfd(int epfd,int fd);
int modfd(int epfd,int fd,int flag);

class http_conn{
public:
    static int m_epfd;//所有socket注册到同一个epfd上
    static int m_user_count;//用户数目
    static const int READ_BUFFER_SIZE=2048;
    static const int WRITE_BUFFER_SIZE=1024;
    static const int FILENAME_LEN=1000;

    //HTTP请求方法，支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

   /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };

    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */   
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };   


    http_conn();
    ~http_conn();
    //处理客户端请求
    void process();
    //初始化
    void init(const int &sockfd,const sockaddr_in &client_address);
    void close_conn();
    bool read();
    bool write();




private:
    void init();  //初始化连接其余的信息
    //主状态机
    HTTP_CODE process_read();//解析http请求
    HTTP_CODE parse_request_line(char *text);//解析请求首行
    HTTP_CODE parse_header(char *text);//解析请求头
    HTTP_CODE parse_content(char *text);//请求体
    HTTP_CODE do_request();//
    //从状态
    LINE_STATUS parse_line();
    char *get_line();

    bool process_write(HTTP_CODE ret);
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
    

    /* data */
    //数据io
    int m_sockfd;
    sockaddr_in address;
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;
    char m_write_buf[WRITE_BUFFER_SIZE];  //响应头信息
    int m_write_idx;
    //http解析
    int m_check_index;  //当前分析字符位置
    int m_start_line;   //当前行起始位置
    CHECK_STATE m_check_state;  //主状态机状态

    //解析结果
    char *m_url;//目标文件名
    char *m_version;//目标版本
    METHOD m_method; //方法

    char *m_host;//主机名
    bool m_linger;//是否保持连接
    long m_content_length;

    ///home/linux//httpserver/resources
    char m_real_file[ FILENAME_LEN ];//文件名
    struct stat m_file_stat;
    char* m_file_address;             // 客户请求的目标文件被mmap到内存中的起始位置

    //写
    struct iovec m_iv[2];             // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iv_count;

};


#endif