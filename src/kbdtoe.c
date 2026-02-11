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
#include "kbdtoe.h"
#include "kbdtoe_base.h"
#include "dtoe_mempool_mr.h"
#include "securec.h"

static uint8_t g_thread_num = 1; // 默认单线程
static uint8_t g_channel_num = 1; // 默认每个线程1对信道,redis 单实例最大支持1W，单对信道支持8k
static libdtoe_thread_pool_s g_thread_pool[DTOE_THREAD_MAX];

inline libdtoe_thread_pool_s* get_thread_pool(int idx)
{
    if (idx < 0 || idx >= DTOE_THREAD_MAX) {
        KBDTOE_ERR("invalid thread pool index, idx=%d", idx);
        return NULL;
    }
    return &g_thread_pool[idx];
}

/*****************************  callback function start *****************************/
void libdtoe_close_done(int sockfd)
{
    close(sockfd);
    libdtoe_conn_s *conn = (libdtoe_conn_s *)knet_get_ulp_user_data(sockfd);
    conn->fd = -1;
}

void libdtoe_prepare_close_done(int sockfd)
{
    libdtoe_conn_s *conn = (libdtoe_conn_s *)knet_get_ulp_user_data(sockfd);
    conn->conn_status = DTOE_CONN_CLOSED;
    libdtoe_thread_pool_s *thread_info = (libdtoe_thread_pool_s *)conn->thread_pool_ptr;
    thread_info->connection.offload_num--;
    pthread_spin_lock(&(thread_info->connection.offload_lock));
    TAILQ_INSERT_TAIL(&thread_info->connection.free_conns,conn, free_node);
    pthread_spin_unlock(&(thread_info->connection.offload_lock));
    knet_close(sockfd);
}

void libdtoe_conn_async_offload_done(int sockfd, uint8_t rsp_status)
{
    libdtoe_conn_s *conn = (libdtoe_conn_s*)knet_get_ulp_user_data(sockfd);
    libdtoe_thread_pool_s *thread_info = (libdtoe_thread_pool_s*) conn->thread_pool_ptr;
    if (rsp_status != KNET_ASYNC_OFFLOAD_SUCCESS || conn->conn_status > DTOE_CONN_WORKING) {
        KBDTOE_ERR("sockfd=%d async offload failed, rsp_status:%d  conn_status:%d", sockfd, rsp_status, conn->conn_status);
        knet_prepare_close(sockfd);
        conn->offload_status = DTOE_OFFLOAD_FAIL;
        return;
	}
    if (conn->conn_status == DTOE_CONN_CREATING) {
        conn->conn_status = DTOE_CONN_WORKING;
        thread_info->connection.offload_num++;
    }
    conn->offload_status = DTOE_OFFLOAD_SUCCESS;
}
/*****************************  callback function end *****************************/

struct knet_ulp_ops g_dtoe_ulp_ops = {
    .close_done = libdtoe_close_done,
    .prepare_close_done = libdtoe_prepare_close_done,
    .conn_async_offload_done = libdtoe_conn_async_offload_done,
};

static int libdtoe_init_conn_pool_per_thread()
{
    int conn_num = DTOE_MAX_CONN_PER_THREAD;
    for (int i = 0; i < g_thread_num; i++) {
        g_thread_pool[i].connection.conn_pool = (libdtoe_conn_s*)malloc(conn_num * sizeof(libdtoe_conn_s));
        if (!g_thread_pool[i].connection.conn_pool) {
            return DTOE_FAIL;
        }
        (void)memset_s((void*)g_thread_pool[i].connection.conn_pool, conn_num * sizeof(libdtoe_conn_s), 0, conn_num * sizeof(libdtoe_conn_s));
    }
    libdtoe_conn_s *conn = NULL;
    for (int i = 0; i < g_thread_num; ++i) {
        TAILQ_INIT(&g_thread_pool[i].connection.free_conns);
        pthread_spin_init(&g_thread_pool[i].connection.offload_lock, PTHREAD_PROCESS_PRIVATE);
        for (int j = 0; j < conn_num; ++j) {
            conn = &g_thread_pool[i].connection.conn_pool[j];
            TAILQ_INSERT_TAIL(&g_thread_pool[i].connection.free_conns, conn, free_node);
        }
    }
    return DTOE_SUCCESS;
}

static int libdtoe_destory_mbuf(libdtoe_thread_pool_s *thread_info)
{
    for (int i = 0; i < DTOE_MAX_CONN_PER_THREAD; ++i) {
        if (thread_info->connection.conn_pool[i].send_buf == NULL) {
            continue;
        }
        thread_info->connection.conn_pool[i].send_buf = NULL;
    }
    free(thread_info->send_mr->addr);
    return DTOE_SUCCESS;
}

static int libdtoe_prepare_mbuf(libdtoe_thread_pool_s *thread_info)
{
    char *buf = NULL;
    int buf_size = (DTOE_SEND_BUF_LEN * DTOE_MAX_CONN_PER_THREAD);
    buf_size += DTOE_PAGE_SIZE;
    buf = (char* )aligned_alloc(DTOE_PAGE_SIZE, buf_size);
    if (buf == NULL) {
        return DTOE_FAIL;
    }
    (void)memset_s(buf, buf_size, 0, buf_size);
    thread_info->send_mr = knet_reg_mr(buf, buf_size);
    if (thread_info->send_mr == NULL) {
        free(buf);
        libdtoe_destory_mbuf(thread_info);
        return DTOE_FAIL;
    }
    KBDTOE_INFO("libdtoe_prepare_mbuf is success");
    return 0;
}

static int libdtoe_all_threads_create_channel()
{
    int ret; 
    for (int i = 0; i < g_thread_num; ++i) {
        ret = libdtoe_prepare_mbuf(&g_thread_pool[i]);
        if (ret != DTOE_SUCCESS) {
            KBDTOE_ERR("libdtoe prepare mbuf failed");
            return DTOE_FAIL;
        }
        for (int j = 0; j < (g_channel_num / g_thread_num); ++j) {
            ret = knet_create_send_channel(KNET_POLL_SCHD, DTOE_RING_SSQ_DEPTH, &g_thread_pool[i].send_channel[j]);
            if (ret != 0) {
                KBDTOE_ERR("create send channel failed, ret %d", ret);
                goto cleanup;
            }
            ret = knet_create_recv_channel(KNET_POLL_SCHD, DTOE_RING_SRQ_DEPTH, &g_thread_pool[i].recv_channel[j]);
            if (ret != 0) {
                KBDTOE_ERR("create recv channel failed, ret %d", ret);
                goto cleanup;

            }
        }
        g_thread_pool[i].channel_num++;
    }
    return DTOE_SUCCESS;
    cleanup:
    for (int i = 0; i < g_thread_num; ++i){
        libdtoe_destory_mbuf(&g_thread_pool[i]);
    }
    return DTOE_FAIL;
}

int kbdtoe_init(const char* dtoe_ip)
{
    int ret = 0;
    knet_ulp_ops_register(&g_dtoe_ulp_ops);
    ret = knet_init(dtoe_ip);
    if (ret != 0) {
        KBDTOE_ERR("knet init failed, ret %d, dtoe_ip %s", ret, dtoe_ip);
        return DTOE_FAIL;
    }
    ret = libdtoe_init_conn_pool_per_thread();
    if (ret != DTOE_SUCCESS) {
        KBDTOE_ERR("kbdtoe init conn pool failed");
        return DTOE_FAIL;
    }
    ret = libdtoe_all_threads_create_channel();
    if (ret != DTOE_SUCCESS) {
        KBDTOE_ERR("kbdtoe init thread channel failed");
        return DTOE_FAIL;
    }
    ret = dtoe_mempool_init();
    if (ret != DTOE_SUCCESS) {
        KBDTOE_ERR("kbdtoe init memory pool failed");
        return DTOE_FAIL;
    }
    KBDTOE_INFO("kbdtoe_init success !!!\n");
    return DTOE_SUCCESS;
}

int libdtoe_conn_init(libdtoe_thread_pool_s *thread_info, libdtoe_conn_s* libdtoe_conn)
{
    libdtoe_conn->recv_status = DTOE_RECV_PREPARE;
    libdtoe_conn->recv_desc_num = 0;
    libdtoe_conn->thread_pool_ptr = (void*) thread_info;
    return DTOE_SUCCESS;
}

int kbdtoe_conn_start_offload(int sockfd)
{
    int ret;
    libdtoe_thread_pool_s *thread_info = (libdtoe_thread_pool_s *)get_thread_pool(0);
    pthread_spin_lock(&(thread_info->connection.offload_lock));
    libdtoe_conn_s *conn = TAILQ_FIRST(&thread_info->connection.free_conns);
    if (conn == NULL) {
        pthread_spin_unlock(&(thread_info->connection.offload_lock));
        KBDTOE_ERR("kbdtoe start offload failed for no free conns");
        return DTOE_FAIL;
    }
    TAILQ_REMOVE(&thread_info->connection.free_conns, conn, free_node);
    pthread_spin_unlock(&(thread_info->connection.offload_lock));
    libdtoe_conn_init(thread_info, conn);

    thread_info->epoch %= thread_info->channel_num;
    struct knet_offload_in in = {0};
	in.user_data = conn;
    conn->offload_status = DTOE_OFFLOAD_START;
    conn->conn_status = DTOE_CONN_CREATING;
    conn->fd = sockfd;
	in.send_channel = thread_info->send_channel[thread_info->epoch];
	in.recv_channel = thread_info->recv_channel[thread_info->epoch];
	ret = knet_start_chimney_general(sockfd, &in);
    if (ret != 0) {
        return DTOE_FAIL;
    }
    conn->send_channel = in.send_channel;
    conn->recv_channel = in.recv_channel;
    conn->leaked_size = 0;
    conn->read_leaked_offset = 0;
    return DTOE_SUCCESS;
}

inline bool kbdtoe_is_conn_offload_success(int sockfd)
{
    libdtoe_conn_s *conn = (libdtoe_conn_s *)knet_get_ulp_user_data(sockfd);
    if (conn == NULL) {
        return false;
    }
   return conn->offload_status == DTOE_OFFLOAD_SUCCESS;
}

void kbdtoe_conn_status_for_close(int sockfd)
{
    libdtoe_conn_s *conn = (libdtoe_conn_s *)knet_get_ulp_user_data(sockfd);
    if (conn == NULL) {
        return;
    }
    conn->offload_status = DTOE_OFFLOAD_PRECLOSE;
}

bool kbdtoe_is_conn_offload(int sockfd)
{
    libdtoe_conn_s *conn = (libdtoe_conn_s *)knet_get_ulp_user_data(sockfd);
    if (conn == NULL) {
        return false;
    }
    return conn->offload_status == DTOE_OFFLOAD_START;
}

int kbdtoe_close(int fd)
{
   knet_prepare_close(fd);
   return 0;
}

void kbdtoe_uninit()
{
    knet_uninit();
    dtoe_mempool_destroy();
}