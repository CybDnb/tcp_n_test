#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main(int argc, char const *argv[])
{
    int socket_fd = socket(AF_INET,SOCK_STREAM,0);
    if(socket_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int bind_res = bind(socket_fd,(struct sockaddr *)&addr,(socklen_t)sizeof(addr));
    if(bind_res < 0)
    {
        perror("bind");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    /**
     * int listen (int __fd, int __n)
     * int __fd:套接字描述符
     * int __n:等待队列长度：指定操作系统内核可以为该套接字排队的最大未完成连接数量。
     * return: Returns 0 on success, -1 for errors.
     */
    int ls_res = listen(socket_fd,SOMAXCONN);
    if(ls_res < 0)
    {
        perror("listen");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    for(;;)
    {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);
        
        /**
         * int accept (int __fd, __SOCKADDR_ARG __addr,
		   socklen_t *__restrict __addr_len);
         * int __fd:监听套接字：由 socket() 创建，并经过 bind() 和 listen() 调用的套接字。
         * __SOCKADDR_ARG __addr:客户端地址 (输出参数)：
         *                      指向一个 struct sockaddr 结构体（通常是 struct sockaddr_in 经过类型转换），
         *                      用于存储连接客户端的 IP 地址和端口号。
         * socklen_t *__restrict __addr_len:地址长度 (输入/输出参数)： 
         *                                  - 输入时：必须设置为 addr 结构体的最大长度（例如 sizeof(struct sockaddr_in)）。 
         *                                  - 输出时：函数会修改此值，表示实际存储在 addr 中的客户端地址的字节数。
         * return:成功返回一个新的、非负整数的套接字描述符
         *        失败返回-1
         */
        int conn_fd = accept(socket_fd,(struct sockaddr *)&cli,&len);
        if(conn_fd < 0)
        {
            perror("accept");
            continue;
        }

        char ip[INET_ADDRSTRLEN];
        const char * ntop_res = inet_ntop(AF_INET,&cli.sin_addr,ip,sizeof(ip));
        if(ntop_res == NULL)
        {
            perror("inet_ntop");
            close(conn_fd);
            continue;
        }

        printf("accrpt from %s:%d\n",ip,ntohs(cli.sin_port));

        for(;;)
        {
            char buf[2048];
            ssize_t n = recv(conn_fd,buf,sizeof(buf),0);
            if(n < 0)
            {
                perror("recv");
                break;
            }
            if(n == 0)
            {
                printf("client closed \n");
                break;
            }

            ssize_t m = send(conn_fd,buf,(size_t)n,0);
            if(m < 0)
            {
                perror("send");
                break;
            }
        }

        close(conn_fd);
        
    }

    close(socket_fd);
    return 0;
}
