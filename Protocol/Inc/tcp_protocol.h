#pragma once

#include <stdint.h>

typedef struct protocol_header {
    uint8_t  version_major;
    uint8_t  version_minor;
    uint16_t message_type;
    uint32_t payload_length; 
    uint32_t seq;
}protocol_header;

typedef struct protocol_msg
{
    protocol_header hdr;
    void *payload;
}protocol_msg;

enum 
{
    MSG_ECHO = 1,
    MSG_FILE_START = 2,
    MSG_FILE_DATA = 3,
    MSG_FILE_END = 4, 
};

int send_message(int fd,protocol_msg *msg);

int read_message(int fd,protocol_msg *msg);

uint32_t u8_to_u32_be(const uint8_t buf[4]);

void u32_to_u8_be(uint32_t value, uint8_t buf[4]);