#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>

#include "tcp_protocol.h"

#define PORT 9000
#define BUFSZ 8192
#define backlog_limit 128

ssize_t recv_all(int fd, void *buf, size_t n);
int send_all(int fd, const void *buf, size_t n);
int read_message(int fd, protocol_msg *msg);
int send_message(int fd, protocol_msg *msg);



int main(void) {
    signal(SIGPIPE, SIG_IGN);  // 防止 send 触发 SIGPIPE 崩溃

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    int optval = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(socket_fd, backlog_limit) < 0) {
        perror("listen");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on 0.0.0.0:%d ...\n", PORT);

    struct sockaddr_in cli;
    socklen_t len = sizeof(cli);
    int cli_fd = accept(socket_fd, (struct sockaddr *)&cli, &len);
    if (cli_fd < 0) {
        perror("accept");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
    printf("Accepted from %s:%d\n", ip, ntohs(cli.sin_port));

    for (;;) {
        protocol_msg msg;
        memset(&msg, 0, sizeof(msg));

        int res = read_message(cli_fd, &msg);
        if (res == 1) {  // 对端关闭
            printf("Client closed connection.\n");
            break;
        } else if (res < 0) {
            perror("read_message");
            break;
        }

        printf("[server]recv version = %d.%d recv type=%u len=%u\n",
               msg.hdr.version_major,msg.hdr.version_minor, msg.hdr.message_type, msg.hdr.payload_length);

        protocol_msg reply;
        reply.hdr.version_major = msg.hdr.version_major;
        reply.hdr.version_minor = msg.hdr.version_minor;
        reply.hdr.message_type = msg.hdr.message_type;  
        reply.hdr.payload_length = msg.hdr.payload_length;
        reply.payload = msg.payload;  

        if (send_message(cli_fd, &reply) < 0) {
            perror("send_message");
            free(msg.payload);
            break;
        }

        free(msg.payload); 
    }

    close(cli_fd);
    close(socket_fd);
    return 0;
}
