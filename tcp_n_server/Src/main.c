#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9000
#define BUFSZ 8192
#define backlog_limit 128

uint32_t u8_to_u32_be(const uint8_t buf[4]) 
{
    uint32_t result = 0;
    result = ((uint32_t)buf[0] << 24) |
             ((uint32_t)buf[1] << 16) |
             ((uint32_t)buf[2] << 8)  |
             ((uint32_t)buf[3]);

    return result;
}

void u32_to_u8_be(uint32_t value, uint8_t buf[4]) 
{
    buf[0] = (uint8_t)(value >> 24); 
    buf[1] = (uint8_t)(value >> 16); 
    buf[2] = (uint8_t)(value >> 8);  
    buf[3] = (uint8_t)value;         
}

int recv_all(int fd,uint8_t *buf,size_t n)
{
    size_t off = 0;
    while(off < n)
    {
        ssize_t m = recv(fd,(uint8_t *)(buf + off),(size_t)(n - off),0);
        if(m < 0)
        {
            if(errno == EINTR) continue;
            return -1;
        }
        if(m == 0)
        {
            return m;
        }
        off += m;
    }
    return off;
}

int send_all(int fd,const uint8_t *p,size_t n)
{
    size_t off = 0;
    while(off < n)
    {
        ssize_t m = send(fd,(uint8_t *)(p + off),(size_t)(n - off),0);
        if(m < 0)
        {
            if(errno == EINTR) continue;
            return -1;
        }
        off += (size_t)m;
    }
    return 0;
}

int read_exact(int fd,uint8_t *buf,size_t n)
{
    uint32_t len;
    uint8_t len_buf[4] = {0};
    int m = recv_all(fd,len_buf,sizeof(len_buf));
    if(m == 0)
    {
        return 0;
    } 
    else if(m < 4)
    {
        return -1;
    }
    len = u8_to_u32_be(len_buf);

    if(len == 0)
    {
        return 0;
    }
    else if(len > n)
    {
        errno = EMSGSIZE;
        return -1;
    }

    ssize_t payload_read = recv_all(fd,buf,len);
    if(payload_read < 0)
    {
        return -1;
    }
    else if(payload_read < len)
    {
        return -1;
    }

    return (int)len;
}

int write_exact(int fd,uint8_t *p,size_t n)
{
    uint8_t len[4];
    u32_to_u8_be((uint32_t)n,len);
    int m = send_all(fd,len,sizeof(len));
    if(m < 0)
    {
        return -1;
    }
    int payload_send_res = send_all(fd,p,n);
    if(payload_send_res < 0)
    {
        return -1;
    }
    return 0;
}

int main(int argc, char const *argv[])
{
    int socket_fd = socket(AF_INET,SOCK_STREAM,0);
    if(socket_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    int optval = 1;
    int set_res = setsockopt(socket_fd,SOL_SOCKET,SO_REUSEADDR,&optval,(socklen_t)sizeof(optval));
    if(set_res < 0)
    {
        perror("socket option");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int bind_res = bind(socket_fd,(const struct sockaddr *)&addr,(socklen_t)sizeof(addr));
    if(bind_res < 0)
    {
        perror("bind");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    int lis_res = listen(socket_fd,backlog_limit);
    if(lis_res < 0)
    {
        perror("listen");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in cli;
    memset(&cli, 0, sizeof(cli));
    socklen_t len = sizeof(cli);
    int cli_fd = accept(socket_fd,(struct sockaddr *)&cli,&len);
    if(cli_fd < 0)
    {
        perror("accept");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET,&cli.sin_addr,ip,sizeof(ip));
    printf("accepted %s:%d\n",ip,ntohs(cli.sin_port));

    uint8_t buf[BUFSZ]; 
    for(;;)
    {
        ssize_t n = read_exact(cli_fd, buf, BUFSZ);

        if(n == 0)
        {
            printf("client closed\n");
            break; 
        } 
        else if(n < 0)
        {
            perror("read_exact");
            break; 
        }
        int m = write_exact(cli_fd, buf, (size_t)n);
        if(m < 0)
        {
            perror("write_exact");
            break; 
        }
    }
    close(cli_fd);
    close(socket_fd);
    return 0;
}
