#pragma once
#include <stdint.h>
#include <stddef.h>

#define TLV_TYPE_LEN (1)
#define TLV_LEN_LEN  (4)
#define TLV_HEADER_LEN (TLV_TYPE_LEN + TLV_LEN_LEN) 

#define TLV_U32_LEN (4)
#define TLV_U64_LEN (8)

enum {
    TLV_FILENAME = 0x01, 
    TLV_FILESIZE = 0x02,  
    TLV_OFFSET   = 0x03, 
    TLV_DATA     = 0x04,  
    TLV_CRC32    = 0x05,  
};


uint64_t htonll_u64(uint64_t x);
uint64_t ntohll_u64(uint64_t x);

uint8_t* tlv_put(uint8_t *out, uint8_t type, const void *val, uint32_t len);

uint8_t* tlv_put_u64(uint8_t *out, uint8_t type, uint64_t v);
uint8_t* tlv_put_u32(uint8_t *out, uint8_t type, uint32_t v);

int tlv_walk(const uint8_t *buf, uint32_t total_len,
             void (*cb)(uint8_t, const uint8_t*, uint32_t, void*),
             void *user);


int build_payload_file_start(const char *filename, uint64_t file_size,
                             uint8_t *out_buf, uint32_t out_cap, uint32_t *out_len);


int build_payload_file_data(uint64_t offset, const uint8_t *data, uint32_t data_len,
                            uint8_t *out_buf, uint32_t out_cap, uint32_t *out_len);

static inline int build_payload_file_end(uint8_t *out_buf, uint32_t out_cap, uint32_t *out_len) {
    (void)out_buf; (void)out_cap; *out_len = 0; return 0;
}

int parse_payload_file_start(const uint8_t *p, uint32_t L,
                             char *filename_buf, uint32_t fname_cap,
                             uint64_t *file_size);

int parse_payload_file_data(const uint8_t *p, uint32_t L,
                            uint64_t *offset,
                            const uint8_t **data_ptr, uint32_t *data_len);
