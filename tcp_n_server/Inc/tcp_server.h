#pragma once 

#include <arpa/inet.h>

#define RECV_DIR "recv"

typedef struct 
{
    int fd;
    struct sockaddr_in addr;

}client_ctx_t;

ssize_t fwrite_all(FILE *fp, const uint8_t *p, size_t n);