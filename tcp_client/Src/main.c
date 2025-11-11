#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdatomic.h>
#include "tcp_protocol.h" 
#include "tcp_tlv.h"       

static _Atomic uint32_t g_seq = 0;

static inline uint32_t next_seq(void)
{
    return atomic_fetch_add_explicit(&g_seq, 1u, memory_order_relaxed);
}

static void ignore_sigpipe(void) {
    signal(SIGPIPE, SIG_IGN);
}

#define CHUNK_SZ (64 * 1024)

static int send_file(int fd, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror("fopen"); return -1; }

    if (fseek(fp, 0, SEEK_END) != 0) { perror("fseek"); fclose(fp); return -1; }
    long long sz_ll = ftell(fp);
    if (sz_ll < 0) { perror("ftell"); fclose(fp); return -1; }
    rewind(fp);
    uint64_t fsize = (uint64_t)sz_ll;

    const char *fname = strrchr(path, '/');  
    fname = fname ? fname + 1 : path;

    uint8_t start_payload[1024]; 
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
    mstart.hdr.seq            = next_seq(); 
    mstart.payload            = start_payload;

    if (send_message(fd, &mstart) < 0) {
        perror("send FILE_START");
        fclose(fp);
        return -1;
    }

    uint8_t data_buf[CHUNK_SZ];
    uint8_t data_payload[CHUNK_SZ + 32];
    uint64_t offset = 0;
    uint64_t sent_total = 0;

    for (;;) {
        // 读取 A
        static uint8_t rawA[CHUNK_SZ];
        size_t r1 = fread(rawA, 1, CHUNK_SZ, fp);
        if (r1 == 0) break;

        // “偷看”再读 B
        static uint8_t rawB[CHUNK_SZ];
        size_t r2 = fread(rawB, 1, CHUNK_SZ, fp);

        if (r2 > 0) {
            // 预留两个连续序号：A=base, B=base+1
            uint32_t base = atomic_fetch_add_explicit(&g_seq, 2u, memory_order_relaxed);
            uint32_t seqA = base;
            uint32_t seqB = (base + 1u) & 0xFFFFFFFFu;

            // 先发 B（offset_B = offset + r1）
            static uint8_t payloadB[CHUNK_SZ + 32];
            uint32_t lenB = 0;
            if (build_payload_file_data(offset + r1, rawB, (uint32_t)r2,
                                        payloadB, sizeof(payloadB), &lenB) < 0) {
                fprintf(stderr, "build FILE_DATA B failed\n"); fclose(fp); return -1;
            }
            protocol_msg mB = {0};
            mB.hdr.version_major = 1; mB.hdr.version_minor = 0;
            mB.hdr.message_type = MSG_FILE_DATA; mB.hdr.payload_length = lenB;
            mB.hdr.seq = seqB;
            mB.payload = payloadB;
            if (send_message(fd, &mB) < 0) { perror("send FILE_DATA B"); fclose(fp); return -1; }

            // 再发 A（offset_A = offset）
            static uint8_t payloadA[CHUNK_SZ + 32];
            uint32_t lenA = 0;
            if (build_payload_file_data(offset, rawA, (uint32_t)r1,
                                        payloadA, sizeof(payloadA), &lenA) < 0) {
                fprintf(stderr, "build FILE_DATA A failed\n"); fclose(fp); return -1;
            }
            protocol_msg mA = {0};
            mA.hdr.version_major = 1; mA.hdr.version_minor = 0;
            mA.hdr.message_type = MSG_FILE_DATA; mA.hdr.payload_length = lenA;
            mA.hdr.seq = seqA;               // 注意：A 的 seq 比 B 小
            mA.payload = payloadA;
            if (send_message(fd, &mA) < 0) { perror("send FILE_DATA A"); fclose(fp); return -1; }

            offset     += r1 + r2;
            sent_total += r1 + r2;
        } else {
            // 最后一块只有 A：正常顺序即可
            static uint8_t payload[CHUNK_SZ + 32];
            uint32_t len = 0;
            if (build_payload_file_data(offset, rawA, (uint32_t)r1,
                                        payload, sizeof(payload), &len) < 0) {
                fprintf(stderr, "build FILE_DATA failed\n"); fclose(fp); return -1;
            }
            protocol_msg m = {0};
            m.hdr.version_major = 1; m.hdr.version_minor = 0;
            m.hdr.message_type = MSG_FILE_DATA; m.hdr.payload_length = len;
            m.hdr.seq = next_seq();          // 单块时随便取一个新 seq
            m.payload = payload;
            if (send_message(fd, &m) < 0) { perror("send FILE_DATA"); fclose(fp); return -1; }

            offset     += r1;
            sent_total += r1;
        }

        fprintf(stderr, "\r[client] sent %llu bytes", (unsigned long long)sent_total);
        fflush(stderr);
    }
    fclose(fp);
    fprintf(stderr, "\n");

    protocol_msg mend = {0};
    mend.hdr.version_major  = 1;
    mend.hdr.version_minor  = 0;
    mend.hdr.message_type   = MSG_FILE_END;
    mend.hdr.payload_length = 0;
    mend.hdr.seq            = next_seq();
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

    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);

        protocol_msg out = {0};
        out.hdr.version_major  = 1;
        out.hdr.version_minor  = 0;
        out.hdr.message_type   = MSG_ECHO;         
        out.hdr.payload_length = (uint32_t)len;
        out.hdr.seq            = next_seq(); 
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
