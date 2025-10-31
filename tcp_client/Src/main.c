#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "tcp_protocol.h" 

static void ignore_sigpipe(void) {
    signal(SIGPIPE, SIG_IGN);
}

int main(int argc, char const *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "用法: %s <SERVER_IP> <PORT>\n", argv[0]);
        return 1;
    }

    ignore_sigpipe();

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in svr;
    memset(&svr, 0, sizeof(svr));
    svr.sin_family = AF_INET;
    svr.sin_port   = htons(atoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &svr.sin_addr) != 1) {
        perror("inet_pton"); close(fd); return 1;
    }

    if (connect(fd, (struct sockaddr*)&svr, sizeof(svr)) < 0) {
        perror("connect"); close(fd); return 1;
    }

    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);


        protocol_msg out = {0};
        out.hdr.version_major  = 1;
        out.hdr.version_minor  = 0;
        out.hdr.message_type   = 1;                   // 约定 1=ECHO
        out.hdr.payload_length = (uint32_t)len;
        out.payload            = (void*)line;         // 直接用行缓冲区发

        if (send_message(fd, &out) < 0) {
            perror("send_message");
            break;
        }

        protocol_msg in = {0};
        int r = read_message(fd, &in);
        if (r == 1) {
            fprintf(stderr, "server closed\n");
            break;
        } else if (r < 0) {
            perror("read_message");
            break;
        }

        if (in.hdr.message_type != out.hdr.message_type) {
            fprintf(stderr, "warn: unexpected msg_type=%u\n", in.hdr.message_type);
        }

        if (in.hdr.payload_length > 0 && in.payload) {
            write(STDOUT_FILENO, in.payload, in.hdr.payload_length);
        }
        free(in.payload);
    }

    close(fd);
    return 0;
}

