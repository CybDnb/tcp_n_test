#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/stat.h>

#include "tcp_server.h"
#include "tcp_protocol.h"
#include "tcp_tlv.h"

#define PORT 9000
#define BUFSZ 8192
#define backlog_limit 128

extern void *handle_client(void *arg);

ssize_t fwrite_all(FILE *fp, const uint8_t *p, size_t n) {
    size_t off = 0;
    while (off < n) {
        size_t m = fwrite(p + off, 1, n - off, fp);
        if (m == 0) {
            if (ferror(fp)) return -1;   // I/O 出错
            return -1;
        }
        off += m;
    }
    return (ssize_t)off;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);  

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

    struct stat st = {0};
    if (stat(RECV_DIR, &st) == -1) {
        if (mkdir(RECV_DIR, 0755) == -1) {
            perror("mkdir recv/");
            exit(EXIT_FAILURE);
        }
        printf("Created directory: %s\n", RECV_DIR);
    }

    printf("Server listening on 0.0.0.0:%d ...\n", PORT);
    for(;;)
    {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);
        int cli_fd = accept(socket_fd, (struct sockaddr *)&cli, &len);
        if (cli_fd < 0) {
            perror("accept");
            if(errno == EINTR) continue;
            continue;
        }
        client_ctx_t *ctx = malloc(sizeof(client_ctx_t));
        ctx->addr = cli;
        ctx->fd = cli_fd;

        pthread_t th;
        if (pthread_create(&th, NULL, handle_client, ctx) != 0) 
        {
            perror("pthread_create");
            close(cli_fd); 
            free(ctx);     
            continue;      
        }
        pthread_detach(th);
    }
}