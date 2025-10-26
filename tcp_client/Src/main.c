#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main(int argc, char const *argv[])
{
    if(argc < 3)
    {
        fprintf(stderr, "用法: %s <SERVER_IP> <PORT>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int socket_fd = socket(AF_INET,SOCK_STREAM,0);
    if(socket_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct  sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    
    int pton_res = inet_pton(AF_INET,argv[1],&addr.sin_addr);
    if(pton_res != 1)
    {
        perror("inet_pton");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    //Return 0 on success, -1 for errors.
    int conn_res = connect(socket_fd,(struct sockaddr *)&addr,(socklen_t)sizeof(addr));
    if(conn_res < 0)
    {
        perror("connect");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    char line[1024];
    while(fgets(line,sizeof(line),stdin))
    {
        size_t len = strlen(line);
        uint8_t len_buf[4];
        len_buf[0] = (len >> 24) & 0xFF;
        len_buf[1] = (len >> 16) & 0xFF;
        len_buf[2] = (len >> 8) & 0xFF;
        len_buf[3] = len & 0xFF;


        send(socket_fd, len_buf, 4, 0);
        send(socket_fd, line, len, 0);


        uint8_t recv_len_buf[4];
        recv(socket_fd, recv_len_buf, 4, 0);
        uint32_t recv_len = (recv_len_buf[0] << 24) | (recv_len_buf[1] << 16) |
                            (recv_len_buf[2] << 8)  | recv_len_buf[3];

        char buf[4096];
        recv(socket_fd, buf, recv_len, 0);
        write(STDOUT_FILENO, buf, recv_len);
    }
    close(socket_fd);
    return 0;
}
