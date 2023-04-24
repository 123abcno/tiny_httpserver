#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include "locker.h"
#include "thread_pool.h"
#include <signal.h>
#include "http_conn.h"

#define MAX_FD 10000
#define MAX_EVENT 10000
//信号处理
//添加信号捕捉
void addsig(int sig,void(handler)(int)){//信号与信号处理函数
    struct sigaction act;
    memset(&act,0,sizeof(act));
    act.sa_handler=handler;
    sigfillset(&act.sa_mask);
    sigaction(sig,&act,NULL);

}



int main(int argc,char *argv[]){
    if(argc<=1){
        printf("运行格式为： %s port_number\n",basename(argv[0]));
        exit(-1);
    }
    int port=atoi(argv[1]);

    //对sigpipe信号做处理
    addsig(SIGPIPE,SIG_IGN);
    //创建线程池
    thread_pool<http_conn> *pool=NULL;
    try{
        pool=new thread_pool<http_conn>;
    }catch(...){
        exit(-1);
    }

    //创建数组保存所有客户端信息
    http_conn* users=new http_conn[MAX_FD];

    int lfd=socket(AF_INET,SOCK_STREAM,0);
    if(lfd==-1){
        perror("socket");
        exit(-1);
    }

    //设置端口复用
    int reuse=1;
    int yes=setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    if(yes==-1){
        perror("setsockopt");
        exit(-1);
    }
    //服务器地址
    struct sockaddr_in server_addr;
    server_addr.sin_family=AF_INET;
    server_addr.sin_addr.s_addr=INADDR_ANY;
    server_addr.sin_port=htons(port);

    //绑定地址
    int ret=bind(lfd,(struct sockaddr*)&server_addr,sizeof(server_addr));
    if(ret==-1){
        perror("bind");
        exit(-1);
    }

    listen(lfd,5);

    //创建epoll数组
    epoll_event events[MAX_EVENT];

    //创建epoll对象
    int epfd=epoll_create(100);
    
    if(epfd==-1){
        perror("epoll_create");
        exit(-1);
    }
    //增加描述符
    addfd(epfd,lfd,false);
    http_conn::m_epfd=epfd;//静态成员赋值
    

    while(1){
        int ret=epoll_wait(epfd,events,MAX_EVENT,-1);
        
        if(ret==-1){
            if(errno!=EINTR){
                perror("epoll_wait");
                break;
            }
        }
        // printf("%d\n",ret);
        for(int i=0;i<ret;i++){
            if(events[i].data.fd==lfd){
                //有连接到来
                struct sockaddr_in client_addr;
                socklen_t client_addrlen=sizeof(client_addr);
                int connfd=accept(lfd,(struct sockaddr*)&client_addr,&client_addrlen);
                if(connfd==-1){
                    perror("accept");
                    break;
                }
                if(http_conn::m_user_count>=MAX_FD){
                    //连接数满
                    //提示客户端服务器忙
                    close(connfd);
                    continue;
                }
                // printf("已连接\n");
                users[connfd].init(connfd,client_addr);//初始化对象
                // printf("connfd:%d\n",connfd);
            }else if(events[i].events &(EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                //异常断开
                users[events[i].data.fd].close_conn();

            }else if(events[i].events & EPOLLIN){
                if(users[events[i].data.fd].read()){
                    pool->append(users+events[i].data.fd);
                }else{
                    users->close_conn();
                }
            }else if(events[i].events & EPOLLOUT){
                if(users[events[i].data.fd].write()){
                
                }else{
                    users->close_conn();
                }
            }
        }
    }
        
    delete[] users;
    delete pool;
    close(epfd);
    close(lfd);
}