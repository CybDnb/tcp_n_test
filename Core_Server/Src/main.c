#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char const *argv[])
{
    /**
     * int socket (int __domain, int __type, int __protocol) __THROW;
     * int __domain:地址族 指定套接字用于哪种协议 AF_INET:ipv4 AF_INET6:ipv6 AF_UNIX:本地通信
     * __type:套接字了类型指定套接字提供何种服务类型 
     *                                       SOCK_STREAM:  流式套接字：提供一个可靠、面向连接的服务（基于 TCP）
     *                                       SOCK_DGRAM：数据报套接字：提供一个不可靠、无连接的服务（基于 UDP）
     * 协议（Protocol）：通常设为 0，表示根据 domain 和 type 自动选择合适的协议。
     * return:成功返回一个新的套接字描述符(非负整数)
     *        失败返回-1,并且设置全局变量 errno 以指示错误原因。
    */
    int socket_fd = socket(AF_INET,SOCK_DGRAM,0);
    if(socket_fd == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    /**
        struct sockaddr_in {
            sa_family_t    sin_family; // 地址族 (Address Family)，总是 AF_INET
            in_port_t      sin_port;   // 端口号 (Port Number)，必须是网络字节序
            struct in_addr sin_addr;   // IPv4 地址 (Internet Address)
            char           sin_zero[8]; // 填充字节，使其与 struct sockaddr 大小相同
        };
        struct in_addr {
            uint32_t s_addr; // 32位 IPv4 地址，必须是网络字节序
        };
     */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(9000); 
    /**
     * int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
     * int sockfd:套接字描述符
     * const struct sockaddr *addr：本地地址结构体：指向一个包含 IP 地址和端口号的通用地址结构体
     *                            （通常是 struct sockaddr_in 或 struct sockaddr_in6 经过强制类型转换后的指针）。
     * addrlen：地址结构体长度：即 addr 所指向结构体的实际大小。对于 IPv4，通常是 sizeof(struct sockaddr_in)。
     * return:成功返回0
     *        失败返回-1 并设置全局变量errno
     *        EADDRINUSE：端口已被其他进程占用。
     *        EACCES：试图绑定到需要超级用户权限的端口（如 1023 以下的端口）但权限不足。
     */
    int bind_res = bind(socket_fd,(struct sockaddr *)&addr,sizeof(addr));
    if(bind_res == -1)
    {
        perror("bind");
        close(socket_fd);
        exit(EXIT_FAILURE);
    } 
    /**
     * ssize_t recvfrom(int sockfd, 
                 void *buf, 
                 size_t len, 
                 int flags, 
                 struct sockaddr *src_addr, 
                 socklen_t *addrlen);
     *int sockfd:套接字文件描述符
     *void *buf:存储接收数据的内存区域
     *size_t len:buf的最大字节数
     *int flags:通常设置为0
     *struct sockaddr *src_addr:发送方地址
     *socklen_t *addrlen:地址长度 输入时：必须设置为 src_addr 结构体的最大长度（例如 sizeof(struct sockaddr_in)）。 
                                - 输出时：函数会修改此值，表示实际存储在 src_addr 中的发送方地址的字节数
     *return:成功返回收到的字节数,对于数据报套接字(UDP)如果接收到的数据报长度超过len，多余部分被丢弃,并且可能返回错误
     *       连接关闭:如果用于面向连接的套接字 (TCP)，返回 0 表示连接已正常关闭。
     *       失败： 返回 -1，并设置全局变量 errno。
     */

    /**
     * const char *inet_ntop(int af, 
                      const void *restrict src, 
                      char *restrict dst, 
                      socklen_t size);
        int af:地址族：指定要转换的 IP 地址类型。 必须是 AF_INET (IPv4) 或 AF_INET6 (IPv6)。
        const void *restrict src:源地址：指向包含 IP 地址的二进制结构体
                                （如 struct in_addr 或 struct in6_addr）的指针。
        char *restrict dst:目标缓冲区：指向用于存储结果文本字符串的缓冲区。
        socklen_t size: 缓冲区大小：dst 缓冲区的大小.   对于 IPv4，应使用常量 INET_ADDRSTRLEN (至少 16 字节)；
                                                    对于 IPv6，应使用 INET6_ADDRSTRLEN (至少 46 字节)。
        return:成功返回指向dst的非NULL指针
               失败返回NULL  并设置全局变量 errno。常见的错误是 ENOSPC（缓冲区大小不足）。            
     */

    /**
     *  ssize_t sendto(int sockfd, 
               const void *buf, 
               size_t len, 
               int flags, 
               const struct sockaddr *dest_addr, 
               socklen_t addrlen);
        int sockfd:套接字文件描述符
        const void *buf:发送缓冲区
        size_t len:要发送的字节数
        int flags:通常为0
        const struct sockaddr *dest_addr:目标地址：指向包含目标 IP 地址和端口号的通用地址结构体
                                        （通常是 struct sockaddr_in 经过类型转换后的指针）。
        socklen_t addrlen:地址结构体长度：dest_addr 结构体的实际大小（对于 IPv4 是 sizeof(struct sockaddr_in)）。
        return:返回实际发送的字节数。对于数据报套接字（UDP），这通常等于请求发送的长度 len。
                                              失败： 返回 -1，并设置全局变量 errno。
     */
    for(;;)
    {
        char data[1024];
        socklen_t addrlen = sizeof(struct sockaddr_in);
        ssize_t rev_res = recvfrom(socket_fd,data,sizeof(data),0,(struct sockaddr *)&addr,&addrlen);
        if(rev_res < 0)
        {
            perror("recvfrom");
            continue;
        }
        char ip[INET_ADDRSTRLEN];
        const char *dst = inet_ntop(AF_INET,&addr.sin_addr,ip,sizeof(ip));
        if(dst == NULL)
        {
            perror("inet_notp");
            continue;
        }
        printf("recv %ld bytes from %s:%d\n", rev_res, ip, ntohs(addr.sin_port));
        int send_res = sendto(socket_fd,data,rev_res,0,(struct sockaddr *)&addr,sizeof(struct sockaddr_in));
        if(send_res == -1)
        {
            perror("sendto");
            continue;
        }
    }
    close(socket_fd);
    return 0;
}
