#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <stdio.h>
#include "tcp_protocol.h"

static inline int seq_before(uint32_t a,uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

static inline int seq_after(uint32_t a,uint32_t b)
{
    return seq_before(b,a);
}

static inline uint32_t seq_next(uint32_t s)
{
    return s + 1u;
}

uint32_t u8_to_u32_be(const uint8_t buf[4]) 
{
    uint32_t result = 0;
    result = ((uint32_t)buf[0] << 24) |
             ((uint32_t)buf[1] << 16) |
             ((uint32_t)buf[2] << 8)  |
             ((uint32_t)buf[3]);

    return result;
}

void u32_to_u8_be(uint32_t value, uint8_t buf[4]) 
{
    buf[0] = (uint8_t)(value >> 24); 
    buf[1] = (uint8_t)(value >> 16); 
    buf[2] = (uint8_t)(value >> 8);  
    buf[3] = (uint8_t)value;         
}

int recv_all(int fd,void *buf,size_t n)
{
    uint8_t *p = (uint8_t *)buf;
    size_t off = 0;
    while(off < n)
    {
        ssize_t m = recv(fd,(void *)(p + off),(size_t)(n - off),0);
        if(m < 0)
        {
            if(errno == EINTR) continue;
            return -1;
        }
        if(m == 0)
        {
            // 客户端关闭了连接
            if (off == 0)
            {
                return 0; 
            }
            else
            {
                errno = EPIPE; 
                return -1;
            }
        }
        off += m;
    }
    return off;
}

int send_all(int fd,const void *buf,size_t n)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t off = 0;
    while(off < n)
    {
        ssize_t m = send(fd,(void *)(p + off),(size_t)(n - off),0);
        if(m < 0)
        {
            if(errno == EINTR) continue;
            return -1;
        }
        off += (size_t)m;
    }
    return 0;
}

int send_message(int fd,protocol_msg *msg)
{
    protocol_msg msg_copy;
    msg_copy.hdr.version_major = msg->hdr.version_major;
    msg_copy.hdr.version_minor = msg->hdr.version_minor;
    msg_copy.hdr.payload_length = htonl(msg->hdr.payload_length);
    msg_copy.hdr.message_type = htons(msg->hdr.message_type);
    msg_copy.hdr.seq = htonl(msg->hdr.seq);
    int res = send_all(fd,&msg_copy.hdr,sizeof(protocol_header));
    if(res < 0)
    {
        return -1;
    }
    int data_res = send_all(fd,msg->payload,msg->hdr.payload_length);
    if(data_res < 0)
    {
        return -1;
    }
    return 0;
}

int read_message(int fd,protocol_msg *msg)
{
    protocol_header hdr_copy;
    int rec_res = recv_all(fd,&hdr_copy,sizeof(protocol_header));
    if(rec_res < 0)
    {
        return -1;
    }
    else if(rec_res == 0)
    {
        return 1;
    }
    msg->hdr.message_type = ntohs(hdr_copy.message_type);
    msg->hdr.seq = ntohl(hdr_copy.seq);
    msg->hdr.payload_length = ntohl(hdr_copy.payload_length);
    msg->hdr.version_major = hdr_copy.version_major;
    msg->hdr.version_minor = hdr_copy.version_minor;
    uint8_t *data = malloc(sizeof(uint8_t) * msg->hdr.payload_length);
    if(data == NULL)
    {
        return -2;
    }
    int res = recv_all(fd,data,sizeof(uint8_t) * msg->hdr.payload_length);
    if (res < 0)
    {
        free(data);
        return -1;
    }
    msg->payload = data;
    
    return 0;
}