/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
* kraio is licensed under the Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*     http://license.coscl.org.cn/MulanPSL2
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
* PURPOSE.
* See the Mulan PSL v2 for more details.
*
* Encapsulate dtoe interface
*/
#ifndef KB_DTOE_BASE_H
#define KB_DTOE_BASE_H
#include <string.h>
#include <sched.h>
#include <unistd.h>
#include <inttypes.h>
#include <stddef.h>
#include <sys/queue.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include "flexda_dtoe_interface.h"
#include "kbdtoe.h"

extern int g_kbdtoe_log_lvl;
static inline void kbdtoe_log(const char *function, int line, int level, const char *format, ...)
{
    char buffer[1024];
    va_list args;

    if (level > g_kbdtoe_log_lvl)
        return;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    switch (level) {
        case LOG_ERR:
            fprintf(stderr, "[ERR] %s:%d - %s\n", function, line, buffer);
            break;
        case LOG_WARNING:
            fprintf(stderr, "[WARN] %s:%d - %s\n", function, line, buffer);
            break;
        case LOG_INFO:
            fprintf(stdout, "[INFO] %s:%d - %s\n", function, line, buffer);
            break;
        case LOG_DEBUG:
            fprintf(stdout, "[DEBUG] %s:%d - %s\n", function, line, buffer);
            break;
        default:
            break;
    }
}

#define KBDTOE_ERR(fmt, args...) kbdtoe_log(__func__, __LINE__, LOG_ERR, fmt, ##args)
#define KBDTOE_WARN(fmt, args...) kbdtoe_log(__func__, __LINE__, LOG_WARNING, fmt, ##args)
#define KBDTOE_INFO(fmt, args...) kbdtoe_log(__func__, __LINE__, LOG_INFO, fmt, ##args)
#define KBDTOE_DEBUG(fmt, args...) kbdtoe_log(__func__, __LINE__, LOG_DEBUG, fmt, ##args)
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define DTOE_FAIL               (-1)
#define DTOE_SUCCESS            (0)
#define IPSTR_LEN               (32)
#define DTOE_THREAD_MIN         (1)
#define DTOE_THREAD_MAX         (16)
#define DTOE_SOCKET_IP_LEN      (16)
#define DTOE_CHANNEL_NUM_MAX    (16)
#define DTOE_HASH_ARRAY_NUM     (16)
#define DTOE_EVERY_BATCH        (10)
#define DTOE_EPOLL_EVENT_NUM    DTOE_EVERY_BATCH
#define DTOE_SEND_BUF_LEN       (4096)
#define DTOE_PAGE_SIZE          (4096)
#define DTOE_SEND_POLL_CNT       (32)
#define DTOE_RECV_POLL_CNT       (32)
#define DTOE_LARGE_BD_NUM         (20480)
#define DTOE_RING_SSQ_DEPTH       (6144)
#define DTOE_RING_SRQ_DEPTH       (6144)
#define VBS_SEG_NUM                   (128)
#define DTOE_MAX_CONN_NUM             (2000)
#define DTOE_PENDING_REQ_MAX_NUM      (20480U + 4096U)
#define DTOE_CORE_NUM                 (16)
#define DTOE_ASYNC_OFFLOAD_MAX_NUM    (1024)
#define DTOE_MAX_CONN_PER_THREAD      10000 // redis单个实例
#define DTOE_CONN_PER_CHNL             (1024)
#define DTOE_RECV_MAX_DESC_NUM         (DTOE_RECV_POLL_CNT)
#define DTOE_UNUSED(a)           ((a)=(a))

TAILQ_HEAD(libdtoe_conn_head, libdtoe_conn);
TAILQ_HEAD(libdtoe_conn_readable_event_head, libdtoe_conn);
typedef enum {
    DTOE_OFFLOAD_WAIT = 0,
    DTOE_OFFLOAD_START,
    DTOE_OFFLOAD_SUCCESS,
    DTOE_OFFLOAD_FAIL,
    DTOE_OFFLOAD_PRECLOSE,
} libdtoe_thread_offload_status_e;

typedef enum {
    DTOE_CONN_CREATING = 0,
    DTOE_CONN_WORKING,
    DTOE_CONN_PRE_CLOSING,
    DTOE_CONN_CLOSING,
    DTOE_CONN_CLOSED,
} libdtoe_conn_status_e;

typedef enum {
    DTOE_SEND_PREPARE,
    DTOE_SEND_PENDING,
    DTOE_SEND_WORKING,
    DTOE_SEND_DONE, 
    DTOE_SEND_END,
} libdtoe_send_status_e;

typedef enum {
    DTOE_RECV_PREPARE,
    DTOE_RECV_PENDING,
    DTOE_RECV_DONE,
    DTOE_RECV_END,
} libdtoe_recv_status_e;

typedef enum {
    DTOE_HEADER_PENDING = 0,
    DTOE_HEADER_RECVED,
    DTOE_HEADER_NEED_RESOLVED,
    DTOE_HEADER_RESOLVED,
} libdtoe_hdr_status_e;

typedef enum {
    DTOE_NODE_DETACH = 0,
    DTOE_NODE_ATTACH
} libdtoe_node_status_e;

typedef struct libdtoe_recv_hdr {
    char hdr[100];
    char data[100];
    char* large_data;
    uint32_t offset;
    uint32_t data_send_back;
    libdtoe_hdr_status_e hdr_status;
} libdtoe_recv_hdr_s;

typedef struct libdtoe_tx_desc_node {
    TAILQ_ENTRY(libdtoe_tx_desc_node) tx_desc_node;
    uint16_t send_sn;
    libdtoe_send_status_e send_status;
} libdtoe_tx_desc_node_s;

typedef struct libdtoe_req_node {
    uint64_t request_id;
    uint8_t try_id;
    uint64_t wr_id;
    uint16_t send_sn;
    uint32_t dlen;
    libdtoe_tx_desc_node_s *send_desc;
} libdtoe_req_node_s;

TAILQ_HEAD(libdtoe_tx_desc_head, libdtoe_tx_desc_node);

typedef struct libdtoe_recv_desc {
    flexda_dtoe_iovec_s iov;
    int data_remain;
    flexda_dtoe_iovec_s iov_origin;
}libdtoe_recv_desc_s;

typedef void (*kbdtoe_tx_req_free_cb_t)(int sockfd, uint64_t wr_id);
typedef struct libdtoe_pending_send {
    TAILQ_ENTRY(libdtoe_pending_send) node;
    kbdtoe_tx_req_free_cb_t free_cb;
    uint64_t wr_id;
    int sockfd;
    uint16_t send_sn;
} __attribute__((packed)) libdtoe_pending_send_s;
TAILQ_HEAD(libdtoe_pending_send_head, libdtoe_pending_send);

typedef struct libdtoe_conn {
    void* node;
    libdtoe_conn_status_e conn_status;
    uint32_t recv_status;
    char *send_buf;
    uint16_t recv_desc_num;
    flexda_send_channel_s *send_channel;
    flexda_recv_channel_s *recv_channel;
    libdtoe_req_node_s req;
    libdtoe_recv_desc_s recv_desc;
    void (*process_cb) (void *, int);
    libdtoe_recv_hdr_s recv_hdr;
    void* thread_pool_ptr;
    uint32_t offload_status;
    TAILQ_ENTRY(libdtoe_conn) free_node;
    int fd;
    void *dtoe_conn;
    struct {
        uint32_t last_send_sn;
        uint32_t comp_sn;
        uint32_t last_ack_sn;
        struct libdtoe_pending_send_head unack_req;
        struct libdtoe_pending_send_head free_req;
        pthread_spinlock_t pending_send_lock;
    } send;
    int recv_event_index;
    int recv_pending_iov_cnt;

    ssize_t leaked_size;
    ssize_t read_leaked_offset;
    void *leaked_buff;
    int has_readable_event;
    int poll_mask;
    TAILQ_ENTRY(libdtoe_conn) readable_event_node;
} libdtoe_conn_s;

typedef struct libdtoe_conn_pool {
    libdtoe_conn_s *conn_pool;
    uint32_t pool_idx;
    uint32_t offload_num;
    pthread_spinlock_t offload_lock;
    struct libdtoe_conn_head free_conns;
} libdtoe_conn_pool_s;

typedef struct libdtoe_node {
    void *info;
    libdtoe_node_status_e node_status;
    struct libdtoe_node *pre_node;
    struct libdtoe_node *next_node;
} libdtoe_node_s;

typedef struct libdtoe_recv_channel_wrapper {
    flexda_recv_channel_s channel;
    struct kbdtoe_recv_events *events;
    uint32_t next_event_idx;
    uint32_t maxevents;
} libdtoe_recv_channel_wrapper_s;

typedef struct libdtoe_thread_pool {
    void *node;
    libdtoe_conn_pool_s connection;
    uint32_t channel_num;
    flexda_send_channel_s* send_channel[DTOE_CHANNEL_NUM_MAX];
    libdtoe_recv_channel_wrapper_s* recv_channel[DTOE_CHANNEL_NUM_MAX];
    int send_channel_fd[DTOE_CHANNEL_NUM_MAX];
    int recv_channel_fd[DTOE_CHANNEL_NUM_MAX];
    flexda_dtoe_mr_s *send_mr;
    uint32_t epoch; /*当前轮询到的channel位置*/
} libdtoe_thread_pool_s;

libdtoe_thread_pool_s* get_thread_pool(int idx);

void* get_conn_by_fd(int fd);

#endif
