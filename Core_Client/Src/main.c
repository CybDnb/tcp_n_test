#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

int main(int argc, char const *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "用法: %s <SERVER_IP> <PORT>\n", argv[0]);
        return 1;
    }
    int socket_fd = socket(AF_INET,SOCK_DGRAM,0);
    if(socket_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    if(inet_pton(AF_INET,argv[1],&addr.sin_addr) != 1)
    {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }
    addr.sin_port = htons(atoi(argv[2]));
    char line[1024];
    while(fgets(line, sizeof(line), stdin))
    {
        size_t len = strlen(line);
        socklen_t addrlen = sizeof(addr);
        int send_res = sendto(socket_fd,line,len,0,(struct sockaddr *)&addr,addrlen);
        if(send_res == -1)
        {
            perror("sendto");
            break;
        }
        char buf[1024];
        ssize_t n = recvfrom(socket_fd,buf,sizeof(buf),0,(struct sockaddr *)&addr,&addrlen);
        if(n < 0)
        {
            perror("recvfrom");
            break;
        }
        write(STDOUT_FILENO, buf, n);
    }
    close(socket_fd);
    return 0;
}
