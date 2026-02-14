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
#include "knet_dtoe_api.h"

/** 日志接口必须在knet_init之后才可以调用 */
#define KBDTOE_ERR(fmt, args...) knet_log(__func__, __LINE__, LOG_ERR, "[ERR] " fmt, ##args)
#define KBDTOE_WARN(fmt, args...) knet_log(__func__, __LINE__, LOG_WARNING, "[WARN] " fmt, ##args)
#define KBDTOE_INFO(fmt, args...) knet_log(__func__, __LINE__, LOG_INFO, "[INFO] " fmt, ##args)
#define KBDTOE_DEBUG(fmt, args...) knet_log(__func__, __LINE__, LOG_DEBUG, "[DEBUG] " fmt, ##args)
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
    struct knet_iovec iov;
    int data_remain;
    struct knet_iovec  iov_origin;
}libdtoe_recv_desc_s;

typedef struct libdtoe_conn {
    void* node;
    libdtoe_conn_status_e conn_status;
    uint32_t recv_status;
    char *send_buf;
    char *long_buf;
    uint16_t recv_desc_num;
    uint16_t pad;
    int recv_len;
    struct knet_send_channel *send_channel;
    struct knet_recv_channel *recv_channel;
    libdtoe_req_node_s req;
    libdtoe_recv_desc_s recv_desc;
    void (*process_cb) (void *, int);
    int data_recv;
    int data_batch;
    uint64_t io_cnt;
    libdtoe_recv_hdr_s recv_hdr;
    void* thread_pool_ptr;
    uint32_t offload_status;
    TAILQ_ENTRY(libdtoe_conn) free_node;
    int fd;
    int mask;
    ssize_t leaked_size;
    ssize_t read_leaked_offset;
    void* leaked_buff;
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

typedef struct libdtoe_thread_pool {
    void *node;
    libdtoe_conn_pool_s connection;
    uint32_t channel_num;
    struct knet_send_channel* send_channel[DTOE_CHANNEL_NUM_MAX];
    struct knet_recv_channel* recv_channel[DTOE_CHANNEL_NUM_MAX];
    struct knet_mr *send_mr;
    uint32_t epoch; /*当前轮询到的channel位置*/
} libdtoe_thread_pool_s;

libdtoe_thread_pool_s* get_thread_pool(int idx);
#endif
