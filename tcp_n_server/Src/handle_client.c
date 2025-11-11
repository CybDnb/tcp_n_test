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

#ifndef SEQ_WINDOW
#define SEQ_WINDOW 8
#endif

typedef struct {
    int      present;
    uint32_t seq;
    uint64_t offset;
    uint8_t *data;
    uint32_t len;
} seq_chunk_t;

typedef struct 
{
    uint64_t cnt_in;
    uint64_t cnt_flush;
    uint64_t cnt_drop_old;
    uint64_t cnt_drop_far; 
}log_t;


static inline int seq_before(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}
static inline uint32_t seq_distance(uint32_t a, uint32_t b) {
    return (uint32_t)(a - b);
}

static int drain_inorder(FILE *out, seq_chunk_t *win,
                         uint32_t *expected_seq, uint64_t *wrote,
                         uint64_t *cnt_flush) 
{
    for (;;) {
        seq_chunk_t *slot = &win[*expected_seq % SEQ_WINDOW];
        if (!slot->present || slot->seq != *expected_seq) break;

        if (fseeko(out, (off_t)slot->offset, SEEK_SET) != 0) { perror("fseeko"); return -1; }
        ssize_t wn = fwrite_all(out, slot->data, slot->len);  
        if (wn < 0 || (size_t)wn != slot->len) { perror("fwrite_all"); return -1; }

        *wrote = slot->offset + slot->len;
        free(slot->data); slot->data = NULL;
        slot->present = 0;
        *expected_seq = (*expected_seq + 1u) & 0xFFFFFFFFu;

        if (cnt_flush) *cnt_flush += 1;
    }
    return 0;
}

static void free_window(seq_chunk_t *win) {
    for (int i = 0; i < SEQ_WINDOW; ++i) {
        if (win[i].present) { free(win[i].data); win[i].data = NULL; }
        win[i].present = 0;
    }
}

void *handle_client(void *arg)
{
    client_ctx_t *ctx = arg;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ctx->addr.sin_addr, ip, sizeof(ip));
    int port = ntohs(ctx->addr.sin_port);
    fprintf(stderr, "[thread %lu] accepted %s:%d\n",
            (unsigned long)pthread_self(), ip, port);

    FILE *out = NULL;           
    char  out_name[512] = {0};  
    uint64_t expect_size = 0;
    uint64_t wrote = 0;
    uint32_t expected_seq = 0;
    log_t log = {0};
    seq_chunk_t window[SEQ_WINDOW] = {0};
    for (;;) {
        protocol_msg msg = {0};
        int r = read_message(ctx->fd, &msg);
        if (r == 1) { printf("client closed\n"); break; }
        if (r < 0)   { perror("read_message"); break; }

        fprintf(stderr, "[thread %lu] recv type=%u len=%u seq=%u\n",
                (unsigned long)pthread_self(),
                msg.hdr.message_type,
                msg.hdr.payload_length,
                msg.hdr.seq);

        switch (msg.hdr.message_type) {
        case MSG_ECHO: {
            protocol_msg rep = msg; 
            if (send_message(ctx->fd, &rep) < 0) perror("send_message");
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

            expected_seq = (msg.hdr.seq + 1u) & 0xFFFFFFFFu;
            free_window(window);
            char safe_name[520];
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
            uint32_t seq = msg.hdr.seq;
            uint32_t dist = seq_distance(seq, expected_seq);

            log.cnt_in++;

            if (seq_before(seq, expected_seq)) {
                log.cnt_drop_old++;
                fprintf(stderr, "[%lu] DROP old seq=%u expect=%u\n",
                        (unsigned long)pthread_self(), seq, expected_seq);
                free(msg.payload);
                break;
            }
            if (dist >= SEQ_WINDOW) {
                log.cnt_drop_far++;
                fprintf(stderr, "[%lu] DROP too-far seq=%u expect=%u (dist=%u >= %d)\n",
                        (unsigned long)pthread_self(), seq, expected_seq, dist, SEQ_WINDOW);
                free(msg.payload);
                break;
            }

            seq_chunk_t *slot = &window[seq % SEQ_WINDOW];
            if (slot->present && slot->seq != seq) {
                free(slot->data);
                slot->present = 0;
            }
            slot->seq = seq;
            slot->offset = offset;
            slot->len = data_len;
            slot->data = (uint8_t*)malloc(data_len);
            if (!slot->data) { perror("malloc"); free(msg.payload); break; }
            memcpy(slot->data, data_ptr, data_len);
            slot->present = 1;

            if (drain_inorder(out, window, &expected_seq, &wrote,&log.cnt_flush) < 0) {
                free(msg.payload);
                break;
            }

            free(msg.payload);
            break;
        }
        case MSG_FILE_END: {
            if (out) {
                drain_inorder(out, window, &expected_seq, &wrote,&log.cnt_flush);
                fflush(out);
                fclose(out);
                out = NULL;

                fprintf(stderr,
                    "[summary] file='%s' recv=%llu flushed≈%llu drop_old=%llu drop_far=%llu wrote=%llu/%llu win=%d\n",
                    out_name,
                    (unsigned long long)log.cnt_in,
                    (unsigned long long)log.cnt_flush,   // 如果你没有传计数器，这里用 0 或先去掉
                    (unsigned long long)log.cnt_drop_old,
                    (unsigned long long)log.cnt_drop_far,
                    (unsigned long long)wrote,
                    (unsigned long long)expect_size,
                    SEQ_WINDOW
                );
                if (expect_size != 0 && wrote != expect_size) { 
                    fprintf(stderr, "WARN: size mismatch\n");
                }
            } else {
                fprintf(stderr,"END without open file\n");
            }
            free(msg.payload);
            free_window(window);
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
    close(ctx->fd);
    free(ctx);
    fprintf(stderr, "[thread %lu] exit\n", (unsigned long)pthread_self());
    return NULL;
}