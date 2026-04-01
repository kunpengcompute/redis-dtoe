/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
* redis dtoe is licensed under the Mulan PSL v2.
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
#include <unistd.h>
#include <inttypes.h>
#include <stddef.h>
#include <sys/queue.h>
#include <pthread.h>
#include "flexda_dtoe_interface.h"
#include "kbdtoe.h" 
#include "kbdtoe_log.h"

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#define DTOE_FAIL               (-1)
#define DTOE_SUCCESS            (0)
#define DTOE_THREAD_MAX         (16)
#define DTOE_SOCKET_IP_LEN      (16)
#define DTOE_CHANNEL_NUM_MAX    (16)
#define DTOE_SEND_BUF_LEN       (4096)
#define DTOE_PAGE_SIZE          (4096)
#define DTOE_SEND_POLL_CNT       (32)
#define DTOE_RECV_POLL_CNT       (32)
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

typedef struct libdtoe_tx_desc_node {
    TAILQ_ENTRY(libdtoe_tx_desc_node) tx_desc_node;
    uint16_t send_sn;
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
    libdtoe_conn_status_e conn_status;
    uint16_t recv_desc_num;
    flexda_send_channel_s *send_channel;
    flexda_recv_channel_s *recv_channel;
    libdtoe_recv_desc_s recv_desc;
    void (*process_cb) (void *, int);
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
uint64_t get_dtoe_dev_sn();
#endif
