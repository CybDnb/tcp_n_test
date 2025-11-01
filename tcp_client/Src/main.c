// client_msg.c — 兼容 ECHO + 发送文件（TLV 协议）
// 用法：
//  1) 交互式回显： ./client_msg <IP> <PORT>
//  2) 发送文件：   ./client_msg <IP> <PORT> sendfile <PATH>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "tcp_protocol.h"  // 提供 protocol_msg / read_message / send_message
#include "tcp_tlv.h"       // 提供 TLV 构造与解析（build_payload_*）

static void ignore_sigpipe(void) {
    signal(SIGPIPE, SIG_IGN);
}

#define CHUNK_SZ (64 * 1024)

static int send_file(int fd, const char *path) {
    // 1) 打开并获取大小
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror("fopen"); return -1; }

    if (fseek(fp, 0, SEEK_END) != 0) { perror("fseek"); fclose(fp); return -1; }
    long long sz_ll = ftell(fp);
    if (sz_ll < 0) { perror("ftell"); fclose(fp); return -1; }
    rewind(fp);
    uint64_t fsize = (uint64_t)sz_ll;

    // 2) 发送 FILE_START（TLV: FILENAME + FILESIZE）
    const char *fname = strrchr(path, '/');  // 只取文件名部分
    fname = fname ? fname + 1 : path;

    uint8_t start_payload[1024];  // 文件名一般不长，足够用；若可能很长可动态分配
    uint32_t start_len = 0;
    if (build_payload_file_start(fname, fsize, start_payload, sizeof(start_payload), &start_len) < 0) {
        fprintf(stderr, "build FILE_START payload failed\n");
        fclose(fp);
        return -1;
    }

    protocol_msg mstart = {0};
    mstart.hdr.version_major  = 1;
    mstart.hdr.version_minor  = 0;
    mstart.hdr.message_type   = MSG_FILE_START;
    mstart.hdr.payload_length = start_len;
    mstart.payload            = start_payload;

    if (send_message(fd, &mstart) < 0) {
        perror("send FILE_START");
        fclose(fp);
        return -1;
    }

    // 3) 循环发送 FILE_DATA（TLV: OFFSET + DATA）
    uint8_t data_buf[CHUNK_SZ];
    uint8_t data_payload[CHUNK_SZ + 32]; // 预留 TLV 头开销
    uint64_t offset = 0;
    uint64_t sent_total = 0;

    for (;;) {
        size_t r = fread(data_buf, 1, CHUNK_SZ, fp);
        if (r == 0) break;

        uint32_t payload_len = 0;
        if (build_payload_file_data(offset, data_buf, (uint32_t)r,
                                    data_payload, sizeof(data_payload), &payload_len) < 0) {
            fprintf(stderr, "build FILE_DATA payload failed\n");
            fclose(fp);
            return -1;
        }

        protocol_msg mdata = {0};
        mdata.hdr.version_major  = 1;
        mdata.hdr.version_minor  = 0;
        mdata.hdr.message_type   = MSG_FILE_DATA;
        mdata.hdr.payload_length = payload_len;
        mdata.payload            = data_payload;

        if (send_message(fd, &mdata) < 0) {
            perror("send FILE_DATA");
            fclose(fp);
            return -1;
        }

        offset     += r;
        sent_total += r;

        // 简单进度打印（每发送一块打印一次）
        fprintf(stderr, "\r[client] sent %llu / %llu bytes (%.1f%%)",
                (unsigned long long)sent_total,
                (unsigned long long)fsize,
                fsize ? (sent_total * 100.0 / fsize) : 100.0);
        fflush(stderr);
    }
    fclose(fp);
    fprintf(stderr, "\n");

    // 4) 发送 FILE_END（空载荷）
    protocol_msg mend = {0};
    mend.hdr.version_major  = 1;
    mend.hdr.version_minor  = 0;
    mend.hdr.message_type   = MSG_FILE_END;
    mend.hdr.payload_length = 0;
    mend.payload            = NULL;

    if (send_message(fd, &mend) < 0) {
        perror("send FILE_END");
        return -1;
    }

    fprintf(stderr, "[client] send file done.\n");
    return 0;
}

int main(int argc, char const *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "用法:\n  %s <SERVER_IP> <PORT>\n  %s <SERVER_IP> <PORT> sendfile <PATH>\n",
                argv[0], argv[0]);
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

    // 文件发送模式： ./client_msg <IP> <PORT> sendfile <PATH>
    if (argc >= 4 && strcmp(argv[3], "sendfile") == 0) {
        if (argc < 5) {
            fprintf(stderr, "缺少文件路径\n");
            close(fd);
            return 1;
        }
        int sr = send_file(fd, argv[4]);
        close(fd);
        return (sr == 0) ? 0 : 1;
    }

    // 否则：交互式 ECHO
    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);

        protocol_msg out = {0};
        out.hdr.version_major  = 1;
        out.hdr.version_minor  = 0;
        out.hdr.message_type   = MSG_ECHO;           // 1
        out.hdr.payload_length = (uint32_t)len;
        out.payload            = (void*)line;

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

        if (in.hdr.payload_length > 0 && in.payload) {
            write(STDOUT_FILENO, in.payload, in.hdr.payload_length);
        }
        free(in.payload);
    }

    close(fd);
    return 0;
}
