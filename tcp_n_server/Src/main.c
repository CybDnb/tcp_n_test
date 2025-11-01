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
#include <sys/stat.h>
#include "tcp_protocol.h"
#include "tcp_tlv.h"

#define PORT 9000
#define BUFSZ 8192
#define backlog_limit 128

static ssize_t fwrite_all(FILE *fp, const uint8_t *p, size_t n) {
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

    const char *RECV_DIR = "recv";
    struct stat st = {0};
    if (stat(RECV_DIR, &st) == -1) {
        if (mkdir(RECV_DIR, 0755) == -1) {
            perror("mkdir recv/");
            exit(EXIT_FAILURE);
        }
        printf("Created directory: %s\n", RECV_DIR);
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

    FILE *out = NULL;           
    char  out_name[512] = {0};  
    uint64_t expect_size = 0;
    uint64_t wrote = 0;

    for (;;) {
        protocol_msg msg = {0};
        int r = read_message(cli_fd, &msg);
        if (r == 1) { printf("client closed\n"); break; }
        if (r < 0)   { perror("read_message"); break; }

        switch (msg.hdr.message_type) {
        case MSG_ECHO: {
            protocol_msg rep = msg; 
            if (send_message(cli_fd, &rep) < 0) perror("send_message");
            free(msg.payload);
            break;
        }
        case MSG_FILE_START: {
            if (out) { fclose(out); out = NULL; }
            memset(out_name, 0, sizeof(out_name));
            expect_size = 0;
            wrote = 0;
            
            int parse_r = parse_payload_file_start(msg.payload, msg.hdr.payload_length,
                                                   out_name, sizeof(out_name), 
                                                   &expect_size);
            
            if (parse_r < 0) {
                fprintf(stderr, "FILE_START invalid payload, code=%d\n", parse_r);
                free(msg.payload); 
                break;
            }
            char safe_name[512];
            const char *fname = strrchr(out_name, '/');
            fname = fname ? fname + 1 : out_name;
            snprintf(safe_name, sizeof(safe_name), "%s/%s", RECV_DIR, fname);

            out = fopen(safe_name, "wb");
            if (!out) {
                perror("fopen");
            } else {
                fprintf(stderr, "START file='%s' size=%llu\n", safe_name,
                        (unsigned long long)expect_size);
            }
            free(msg.payload);
            break;
        }
        case MSG_FILE_DATA: {
            if (!out) { fprintf(stderr,"FILE_DATA without START\n"); free(msg.payload); break; }

            uint64_t offset = 0;
            const uint8_t *data_ptr = NULL;
            uint32_t data_len = 0;

            int parse_r = parse_payload_file_data(msg.payload, msg.hdr.payload_length,
                                                  &offset, &data_ptr, &data_len);
            
            if (parse_r < 0) {
                fprintf(stderr,"FILE_DATA invalid payload, code=%d\n", parse_r); 
                free(msg.payload); 
                break; 
            }

            if (offset != wrote) {
                fseeko(out, (off_t)offset, SEEK_SET);
            }

            ssize_t wn = fwrite_all(out, data_ptr, data_len);
            if (wn < 0 || (size_t)wn != data_len) {
                perror("fwrite_all");
            }

            wrote = offset + data_len; 

            free(msg.payload); 
            break;
        }
        case MSG_FILE_END: {
            if (out) {
                fflush(out);
                fclose(out);
                out = NULL;
                fprintf(stderr, "END file='%s' wrote=%llu/%llu\n",
                        out_name,
                        (unsigned long long)wrote,
                        (unsigned long long)expect_size);
                if (expect_size != 0 && wrote != expect_size) { 
                    fprintf(stderr, "WARN: size mismatch\n");
                }
            } else {
                fprintf(stderr,"END without open file\n");
            }
            free(msg.payload);
            break;
        }
        default:
            fprintf(stderr,"unknown msg_type=%u\n", msg.hdr.message_type);
            free(msg.payload);
            break;
        }
    }

    if (out) { 
        fclose(out);
    }
    close(cli_fd);
    close(socket_fd);
    return 0;
}