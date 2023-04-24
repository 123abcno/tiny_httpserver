#include "http_conn.h"


// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char* doc_root = "/home/ddl/linux/httpserver/resources";

//var
int http_conn::m_epfd=-1;
int http_conn::m_user_count=0;

//function
// 设置非阻塞
void setnoblocking(int fd){
    int flags=fcntl(fd,F_GETFL);
    flags|=O_NONBLOCK;
    fcntl(fd,F_SETFL,flags);
}

//epoll操作
//连接断开会触发 EPOLLRDHUP,EPOLLONESHOT一个socket在任何时刻被一个socket处理
int addfd(int epfd,int fd,bool one_shot){
    epoll_event event;
    memset(&event,0,sizeof(event));
    event.data.fd=fd;
    event.events=EPOLLIN |EPOLLET| EPOLLRDHUP;
    // event.events=EPOLLIN | EPOLLRDHUP;
    if(one_shot){
        event.events|=EPOLLONESHOT;
    }
    setnoblocking(fd);
    epoll_ctl(epfd,EPOLL_CTL_ADD,fd,&event);
    //ET模式设置非阻塞
}

int delfd(int epfd,int fd){
    epoll_ctl(epfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

//待完善。。。。。
int modfd(int epfd,int fd,int flag){
    epoll_event event;
    memset(&event,0,sizeof(event));
    event.data.fd=fd;
    event.events=flag|EPOLLONESHOT |EPOLLRDHUP;
    epoll_ctl(epfd,EPOLL_CTL_MOD,fd,&event);
}

http_conn::http_conn(){
    m_sockfd=-1;
}

http_conn::~http_conn(){
    
}

void http_conn::init(const int &sockfd,const sockaddr_in &client_address){//初始化
    m_sockfd=sockfd;
    address=client_address;
    addfd(m_epfd,m_sockfd,true);
    m_user_count++;
    

    init();
}

void http_conn::init(){
    bzero(m_read_buf,READ_BUFFER_SIZE);
    bzero(m_real_file,FILENAME_LEN);
    m_check_index=0;
    m_check_state=CHECK_STATE_REQUESTLINE;
    m_start_line=0;
    m_read_idx=0;

    m_url=NULL;
    m_version=NULL;
    m_method=GET;

    m_host=NULL;
    m_linger=false;
    m_content_length = 0;
    
    m_file_address=NULL;

    m_write_idx=0;
}

void http_conn::close_conn(){
    if(m_sockfd!=-1){
        delfd(m_epfd,m_sockfd);
        m_sockfd==-1;
        m_user_count--;
    }
}

bool http_conn::read(){//读取报数据
    if(m_read_idx>=READ_BUFFER_SIZE)
        return false;
    int read_len=0;
    while(1){
        // printf("%d\n",m_sockfd);
        read_len=recv(m_sockfd,m_read_buf+m_read_idx,sizeof(m_read_buf)-m_read_idx,0);//非阻塞
        if(read_len==-1){
            if(errno==EWOULDBLOCK || errno==EAGAIN)
                break;
            perror("recv");
            return false;
        }else if(read_len==0){
            printf("connection closed\n");
            return false;
        }
        m_read_idx+=read_len;
    }
    printf("%s\n",m_read_buf);
    return true;
}

bool http_conn::write(){
    int temp = 0;
    int bytes_have_send = 0;    // 已经发送的字节
    int bytes_to_send = m_write_idx+m_file_stat.st_size;// 将要发送的字节 （m_write_idx）写缓冲区中待发送的字节数
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epfd, m_sockfd, EPOLLIN ); 
        init();
        return true;
    }
    while(1) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        // printf("%d:%d:%d\n",bytes_to_send,bytes_have_send,m_write_idx);
        if ( bytes_to_send <= bytes_have_send ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            
            unmap();
            if(m_linger) {
                init();
                modfd( m_epfd, m_sockfd, EPOLLIN );
                return true;
            } else {
                modfd( m_epfd, m_sockfd, EPOLLIN );
                return false;
            } 
        }
    }
}

//由线程池中工作线程调用，处理http请求的入口函数
void http_conn::process(){

    //解析请求
    HTTP_CODE read_ret=process_read();
    if(read_ret==NO_REQUEST){
        //请求不完整
        modfd(m_epfd,m_sockfd,EPOLLIN);
        return;
    }
    
    //生成响应
    bool write_ret=process_write(read_ret);
    if(!write_ret){
        close_conn();
        
    }
    modfd(m_epfd,m_sockfd,EPOLLOUT);
    printf("生成响应结束\n");
}

//主状态：解析请求
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS lines_status=LINE_OK;
    HTTP_CODE ret=NO_REQUEST;

    char *text=0;
    while((m_check_state==CHECK_STATE_CONTENT && (lines_status==LINE_OK)) 
            || (lines_status=parse_line())==LINE_OK){//解析一行

        //获取一行数据
        text=get_line();
        m_start_line=m_check_index;//修改行起始
        printf("got 1 http line: %s\n",text);

        switch (m_check_state){
            case CHECK_STATE_REQUESTLINE:
                ret=parse_request_line(text);
                if(ret==BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            case CHECK_STATE_HEADER:
                ret=parse_header(text);
                if(ret==BAD_REQUEST){
                    return BAD_REQUEST;
                }else if(ret==GET_REQUEST){
                    return do_request();//具体信息
                }
                break;
            case CHECK_STATE_CONTENT:
                ret==parse_content(text);
                if(ret==GET_REQUEST){
                    return do_request();
                }else
                    lines_status=LINE_OPEN;
                break;
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

//解析请求行获取方法，目标URL，协议
http_conn::HTTP_CODE http_conn::parse_request_line(char *text){
    //GET URL HTTP
    m_url=strpbrk(text," \t");
    if(!m_url){
        return BAD_REQUEST;
    }
    *m_url++='\0';
    //GET\0/index.html HTTP1.1
    char *method=text;
    if(strcasecmp(method,"GET")==0){
        m_method=GET;
    }else{
        return BAD_REQUEST;
    }
    //GET\0/index.html\0HTTP1.1
    m_version=strpbrk(m_url," \t");
    if(!m_version){
        return BAD_REQUEST;
    }
    *m_version++='\0';
    if(strcasecmp(m_version,"HTTP/1.1")!=0){
        return BAD_REQUEST;
    }

    //http://192.168.49.122:10000/index.html
    if(strncasecmp(m_url,"http://",7)==0){
        m_url+=7;
        m_url=strchr(m_url,'/');
    }
    if(!m_url || m_url[0]!='/'){
        return BAD_REQUEST;
    }

    m_check_state=CHECK_STATE_HEADER;//请求行完毕，修改状态为头部
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_header(char *text){
 // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 ) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
        printf( "oop! unknow header %s\n", text );
    }
    return NO_REQUEST;    
}
//只是判断它是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_content(char *text){
    if ( m_read_idx >= ( m_content_length + m_check_index ) )
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::LINE_STATUS http_conn::parse_line(){
    char tmp;
    for(;m_check_index<m_read_idx;++m_check_index){
        tmp=m_read_buf[m_check_index];
        if(tmp=='\r'){
            if((m_check_index+1)==m_read_idx){//数据不全
                return LINE_OPEN;
            }else if(m_read_buf[m_check_index+1]=='\n'){
                m_read_buf[m_check_index++]='\0';
                m_read_buf[m_check_index++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }else if(tmp=='\n'){
            if((m_check_index>1) && (m_read_buf[m_check_index-1]=='\r')){//有数据
                m_read_buf[m_check_index-1]='\0';
                m_read_buf[m_check_index++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

char * http_conn::get_line(){
    return m_read_buf + m_start_line;
}

http_conn::HTTP_CODE http_conn::do_request(){
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    // 创建内存映射
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

// 对内存映射区执行munmap操作
void http_conn::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}

bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ... ) {
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

