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
#include "kbdtoe.h"
#include <sys/resource.h>
#include <sys/epoll.h>
#include "kbdtoe_base.h"
#include "kbdtoe_mempool_mr.h"
#include "securec.h"

static uint8_t g_thread_num = 1; // 默认单线程
static uint8_t g_channel_num = 1; // 默认每个线程1对信道,redis 单实例最大支持1W，单对信道支持8k
static libdtoe_thread_pool_s g_thread_pool[DTOE_THREAD_MAX];
dtoe_close_done_callback_t g_dtoe_close_done_callback;
struct libdtoe_conn_readable_event_head g_readable_event_head;
static unsigned int g_max_conn_num = 0;
static uint64_t g_dev_sn = 0;
static uint32_t g_numa_id = 0;
static void **g_fd_to_conn;
static int g_libdtoe_fd_max = 0;

static int libdtoe_fd_init(void)
{
    if (g_libdtoe_fd_max > 0) {
        KBDTOE_INFO("Dtoe fd module reinit");
        return 0;
    }

    struct rlimit limit = {0};
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
        KBDTOE_ERR("Get Linux fd limit fail");
        return -1;
    }

    g_fd_to_conn = (void **)calloc(limit.rlim_cur, sizeof(void *));
    if (g_fd_to_conn == NULL) {
        KBDTOE_ERR("Alloc Dtoe fd mng failed, fd cur limit %u!", limit.rlim_cur);
        return -1;
    }
    g_libdtoe_fd_max = limit.rlim_cur;
    KBDTOE_INFO("Dtoe fd max %d", g_libdtoe_fd_max);

    return 0;
}

static void libdtoe_fd_dinit(void)
{
    if (g_fd_to_conn) {
        free(g_fd_to_conn);
        g_fd_to_conn = NULL;
    }
}

static inline void set_conn_by_fd(int fd, void *conn)
{
    if (fd >= 0 && fd < g_libdtoe_fd_max) {
        if ((g_fd_to_conn[fd] && conn != NULL) || (g_fd_to_conn[fd] == NULL && conn == NULL)) {
            KBDTOE_ERR("set_conn_by_fd error, fd:%d, oldconn:%p, conn:%p\n", fd, g_fd_to_conn[fd], conn);
        }
        g_fd_to_conn[fd] = conn;
    } else {
        KBDTOE_ERR("set_conn_by_fd error, invalid fd:%d, conn:%p\n", fd, conn);
    }
}

void* get_conn_by_fd(int fd)
{
    if (fd >= 0 && fd < g_libdtoe_fd_max) {
        return g_fd_to_conn[fd]; // maybe NULL
    }
    return NULL;
}

void register_dtoe_close_done_callback(dtoe_close_done_callback_t cb)
{
    if (cb == NULL) {
       KBDTOE_ERR("dtoe close done callback is null");
       return;
    }
    g_dtoe_close_done_callback = cb;
}

inline libdtoe_thread_pool_s* get_thread_pool(int idx)
{
    if (idx < 0 || idx >= DTOE_THREAD_MAX) {
        KBDTOE_ERR("invalid thread pool index, idx=%d", idx);
        return NULL;
    }
    return &g_thread_pool[idx];
}

/*****************************  callback function start *****************************/
void libdtoe_close_done(void *dtoe_conn)
{
    libdtoe_conn_s *conn = (libdtoe_conn_s *)flexda_dtoe_get_ulp_user_data(dtoe_conn);
    if (conn == NULL) {
        KBDTOE_ERR("dtoe close done, conn is null");
        return;
    }
    libdtoe_thread_pool_s *thread_info = (libdtoe_thread_pool_s *)conn->thread_pool_ptr;

    if (g_dtoe_close_done_callback != NULL) {
        g_dtoe_close_done_callback(conn->fd);
    } else {
        KBDTOE_ERR("dtoe close done callback need register");
    }

    pthread_spin_lock(&(thread_info->connection.offload_lock));
    set_conn_by_fd(conn->fd, NULL);
    TAILQ_INSERT_TAIL(&thread_info->connection.free_conns, conn, free_node);
    pthread_spin_unlock(&(thread_info->connection.offload_lock));
}

void libdtoe_prepare_close_done(void *dtoe_conn)
{
    libdtoe_conn_s *conn = (libdtoe_conn_s *)flexda_dtoe_get_ulp_user_data(dtoe_conn);
    conn->conn_status = DTOE_CONN_CLOSED;
    libdtoe_thread_pool_s *thread_info = (libdtoe_thread_pool_s *)conn->thread_pool_ptr;
    thread_info->connection.offload_num--;
    if (conn->has_readable_event) {
        TAILQ_REMOVE(&g_readable_event_head, conn, readable_event_node);
        conn->has_readable_event = 0;
    }

    pthread_spin_lock(&conn->send.pending_send_lock);
    libdtoe_pending_send_s *req = TAILQ_FIRST(&conn->send.unack_req);
    while (req != NULL) {
        if (likely(req->free_cb != NULL)) {
            req->free_cb(req->sockfd, req->wr_id);
        }
        TAILQ_REMOVE(&conn->send.unack_req, req, node);
        free(req);
        req = TAILQ_FIRST(&conn->send.unack_req);
    }
    req = TAILQ_FIRST(&conn->send.free_req);
    while (req != NULL) {
        TAILQ_REMOVE(&conn->send.free_req, req, node);
        free(req);
        req = TAILQ_FIRST(&conn->send.free_req);
    }
    pthread_spin_unlock(&conn->send.pending_send_lock);

    flexda_dtoe_close(conn->fd, dtoe_conn);
}

void libdtoe_conn_async_offload_done(void *dtoe_conn, uint8_t rsp_status)
{
    libdtoe_conn_s *conn = (libdtoe_conn_s*)flexda_dtoe_get_ulp_user_data(dtoe_conn);
    libdtoe_thread_pool_s *thread_info = (libdtoe_thread_pool_s*) conn->thread_pool_ptr;
    if (rsp_status != 0 || conn->conn_status > DTOE_CONN_WORKING) {
        KBDTOE_ERR("sockfd=%d async offload failed, rsp_status:%d  conn_status:%d", conn->fd, rsp_status, conn->conn_status);
        flexda_dtoe_prepare_close(dtoe_conn);
        conn->offload_status = DTOE_OFFLOAD_FAIL;
        return;
    }
    if (conn->conn_status == DTOE_CONN_CREATING) {
        conn->conn_status = DTOE_CONN_WORKING;
        thread_info->connection.offload_num++;
    }
    conn->offload_status = DTOE_OFFLOAD_SUCCESS;
    if (flexda_dtoe_get_leaked_packet_size(conn->dtoe_conn)) {
        conn->has_readable_event = 1;
        conn->poll_mask = 0;
        TAILQ_INSERT_TAIL(&g_readable_event_head, conn, readable_event_node);
    }
}

static inline bool is_send_complete(libdtoe_conn_s *conn, libdtoe_pending_send_s *rnode)
{
    bool complete = false;
    if (likely(conn->send.last_ack_sn < conn->send.comp_sn)) {
        if (rnode->send_sn > conn->send.last_ack_sn && rnode->send_sn <= conn->send.comp_sn) {
            complete = true;
        }
    } else if (conn->send.last_ack_sn > conn->send.comp_sn) {
        if (rnode->send_sn <= conn->send.comp_sn || rnode->send_sn > conn->send.last_ack_sn) {
            complete = true;
        }
    }
    return complete;
}

static void libdtoe_send_complete(void* dtoe_conn, flexda_dtoe_tx_event_s* event)
{
    libdtoe_conn_s *conn = (libdtoe_conn_s *)flexda_dtoe_get_ulp_user_data(dtoe_conn);
    if (unlikely(conn == NULL || event == NULL)) {
        return;
    }

    conn->send.comp_sn = event->finish_msn;

    pthread_spin_lock(&conn->send.pending_send_lock);

    libdtoe_pending_send_s *req = TAILQ_FIRST(&conn->send.unack_req);
    while (req != NULL) {
        if (!is_send_complete(conn, req)) {
            break;
        }

        if (likely(req->free_cb != NULL)) {
            req->free_cb(req->sockfd, req->wr_id);
        }

        conn->send.last_ack_sn = req->send_sn;

        TAILQ_REMOVE(&conn->send.unack_req, req, node);
        TAILQ_INSERT_TAIL(&conn->send.free_req, req, node);

        req = TAILQ_FIRST(&conn->send.unack_req);
    }

    pthread_spin_unlock(&conn->send.pending_send_lock);
}

static void libdtoe_receive_notify(void* dtoe_conn, int iov_cnt)
{
    libdtoe_conn_s *conn = (libdtoe_conn_s *)flexda_dtoe_get_ulp_user_data(dtoe_conn);
    if (unlikely(conn == NULL)) {
        return;
    }
    libdtoe_recv_channel_wrapper_s *recv_channel = (libdtoe_recv_channel_wrapper_s *)conn->recv_channel;

    if (likely(iov_cnt > 0)) {
        if (conn->recv_event_index == -1) {
            recv_channel->events[recv_channel->next_event_idx].sockfd = conn->fd;
            recv_channel->events[recv_channel->next_event_idx].iov_cnt = iov_cnt;
            recv_channel->events[recv_channel->next_event_idx].type = KBDTOE_RX_EVENT_NORMAL;
            conn->recv_event_index = recv_channel->next_event_idx;
            ++recv_channel->next_event_idx;
        } else {
            recv_channel->events[conn->recv_event_index].iov_cnt += iov_cnt;
        }
    } else if (iov_cnt == 0) {
        recv_channel->events[recv_channel->next_event_idx].sockfd = conn->fd;
        recv_channel->events[recv_channel->next_event_idx].iov_cnt = iov_cnt;
        recv_channel->events[recv_channel->next_event_idx].type = KBDTOE_RX_EVENT_NORMAL;
        ++recv_channel->next_event_idx;
    }
}

/*****************************  callback function end *****************************/

static flexda_dtoe_ulp_ops_s g_dtoe_ulp_ops = {
    .send_complete = libdtoe_send_complete,
    .recv_notify = libdtoe_receive_notify,
    .close_done = libdtoe_close_done,
    .prepare_close_done = libdtoe_prepare_close_done,
    .conn_async_offload_done = libdtoe_conn_async_offload_done,
};

static int libdtoe_init_conn_pool_per_thread()
{
    int conn_num = g_max_conn_num;
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
    thread_info->send_mr = (flexda_dtoe_mr_s*)malloc(sizeof(flexda_dtoe_mr_s));
    if (thread_info->send_mr == NULL) {
        free(buf);
        return DTOE_FAIL;
    }
    int ret = flexda_dtoe_reg_mr(g_dev_sn, buf, buf_size, thread_info->send_mr);
    if (ret != 0) {
        free(buf);
        free(thread_info->send_mr);
        thread_info->send_mr = NULL;
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
            g_thread_pool[i].send_channel[j] = (flexda_send_channel_s*)malloc(sizeof(flexda_send_channel_s));
            if (g_thread_pool[i].send_channel[j] == NULL) {
                KBDTOE_ERR("malloc send channel wrapper failed");
                goto cleanup;
            }
            ret = flexda_dtoe_create_send_channel(g_dev_sn, FLEXDA_EPOLL_SCHD, g_thread_pool[i].send_channel[j]);
            if (ret != 0) {
                KBDTOE_ERR("create send channel failed, ret %d", ret);
                free(g_thread_pool[i].send_channel[j]);
                g_thread_pool[i].send_channel[j] = NULL;
                goto cleanup;
            }
            g_thread_pool[i].send_channel_fd[j] = g_thread_pool[i].send_channel[j]->epoll_fd;
            g_thread_pool[i].recv_channel[j] = (libdtoe_recv_channel_wrapper_s*)malloc(sizeof(libdtoe_recv_channel_wrapper_s));
            if (g_thread_pool[i].recv_channel[j] == NULL) {
                KBDTOE_ERR("malloc recv channel wrapper failed");
                goto cleanup;
            }
            g_thread_pool[i].recv_channel[j]->next_event_idx = 0;
            ret = flexda_dtoe_create_receive_channel(g_dev_sn, FLEXDA_EPOLL_SCHD, &g_thread_pool[i].recv_channel[j]->channel);
            if (ret != 0) {
                KBDTOE_ERR("create recv channel failed, ret %d", ret);
                free(g_thread_pool[i].recv_channel[j]);
                g_thread_pool[i].recv_channel[j] = NULL;
                goto cleanup;
            }
            g_thread_pool[i].recv_channel_fd[j] = g_thread_pool[i].recv_channel[j]->channel.epoll_fd;

            g_thread_pool[i].channel_num++;
        }
    }
    return DTOE_SUCCESS;
cleanup:
    for (int i = 0; i < g_thread_num; ++i) {
        for (int j = 0; j < (g_channel_num / g_thread_num); ++j) {
            if (g_thread_pool[i].send_channel[j]) {
                flexda_dtoe_destroy_send_channel(g_thread_pool[i].send_channel[j]);
                free(g_thread_pool[i].send_channel[j]);
                g_thread_pool[i].send_channel[j] = NULL;
            }
            if (g_thread_pool[i].recv_channel[j]) {
                flexda_dtoe_destroy_receive_channel(&g_thread_pool[i].recv_channel[j]->channel);
                free(g_thread_pool[i].recv_channel[j]);
                g_thread_pool[i].recv_channel[j] = NULL;
            }
        }
        libdtoe_destory_mbuf(&g_thread_pool[i]);
    }
    return DTOE_FAIL;
}

int kbdtoe_init(const char* dtoe_ip, unsigned int max_conn_num)
{
    kbdtoe_log_init();
    int ret = 0;
    g_max_conn_num = max_conn_num;
    ret = libdtoe_fd_init();
    if (ret != 0) {
        KBDTOE_ERR("dtoe fd init failed, ret %d", ret);
        return DTOE_FAIL;
    }

    flexda_dtoe_ulp_ops_register(&g_dtoe_ulp_ops);
    ret = flexda_dtoe_ulp_config_set(g_channel_num);
    if (ret != 0) {
        KBDTOE_ERR("dtoe ulp config set failed, ret %d", ret);
        goto fail_fd_init;
    }

    ret = flexda_dtoe_init();
    if (ret != 0) {
        KBDTOE_ERR("dtoe init failed, ret %d", ret);
        goto fail_fd_init;
    }

#define BIND_INTERVAL 1
#define BIND_TIMES 10
    uint8_t times = 0;
    do {
        sleep(BIND_INTERVAL);
        ret = flexda_dtoe_bind_addr(dtoe_ip, &g_dev_sn, &g_numa_id);
        ++times;
    } while (times <= BIND_TIMES && ret != 0);
    if (ret != 0) {
        KBDTOE_ERR("dtoe bind addr failed, ret %d, dtoe_ip %s", ret, dtoe_ip);
        goto fail_dtoe_init;
    }

    ret = libdtoe_init_conn_pool_per_thread();
    if (ret != DTOE_SUCCESS) {
        KBDTOE_ERR("kbdtoe init conn pool failed");
        goto fail_dtoe_init;
    }
    TAILQ_INIT(&g_readable_event_head);
    ret = libdtoe_all_threads_create_channel();
    if (ret != DTOE_SUCCESS) {
        KBDTOE_ERR("kbdtoe init thread channel failed");
        goto fail_dtoe_init;
    }
    ret = kbdtoe_mempool_init();
    if (ret != DTOE_SUCCESS) {
        KBDTOE_ERR("kbdtoe init memory pool failed");
        goto fail_dtoe_init;
    }
    KBDTOE_INFO("kbdtoe_init success !!!\n");
    return DTOE_SUCCESS;

fail_dtoe_init:
    flexda_dtoe_uninit();
fail_fd_init:
    libdtoe_fd_dinit();
    return DTOE_FAIL;
}

uint64_t get_dtoe_dev_sn()
{
  return g_dev_sn;
}

int libdtoe_conn_init(libdtoe_thread_pool_s *thread_info, libdtoe_conn_s* libdtoe_conn)
{
    libdtoe_conn->recv_desc_num = 0;
    libdtoe_conn->thread_pool_ptr = (void*) thread_info;
    libdtoe_conn->poll_mask = 0;
    libdtoe_conn->recv_event_index = -1;
    libdtoe_conn->send.last_send_sn = 0;
    libdtoe_conn->send.comp_sn = 0;
    libdtoe_conn->send.last_ack_sn = 0;
    TAILQ_INIT(&libdtoe_conn->send.unack_req);
    TAILQ_INIT(&libdtoe_conn->send.free_req);
    pthread_spin_init(&libdtoe_conn->send.pending_send_lock, PTHREAD_PROCESS_PRIVATE);
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

    conn->offload_status = DTOE_OFFLOAD_START;
    conn->conn_status = DTOE_CONN_CREATING;
    conn->fd = sockfd;

    flexda_dtoe_offload_in_s in = {0};
    in.user_data = conn;
    in.send_channel = thread_info->send_channel[thread_info->epoch];
    in.rev_channel = (flexda_recv_channel_s *)thread_info->recv_channel[thread_info->epoch];

    flexda_dtoe_offload_out_s out = {0};
    ret = flexda_dtoe_start_conn_offload(sockfd, &in, &out);
    if (ret != 0) {
        KBDTOE_ERR("kbdtoe start offload failed, sockfd:%d, ret:%d", sockfd, ret);
        return DTOE_FAIL;
    }

    set_conn_by_fd(sockfd, conn);
    conn->dtoe_conn = out.dtoe_conn;
    /* Initialize last_ack_sn and comp_sn for is_send_comlete() */
    conn->send.comp_sn = out.send_sn - 1;
    conn->send.last_ack_sn = out.send_sn - 1;
    conn->send_channel = in.send_channel;
    conn->recv_channel = (flexda_recv_channel_s *)in.rev_channel;
    conn->leaked_size = 0;
    conn->read_leaked_offset = 0;
    conn->has_readable_event = 0;
    return DTOE_SUCCESS;
}

inline bool kbdtoe_is_conn_offload_success(int sockfd)
{
    libdtoe_conn_s *conn = (libdtoe_conn_s *)get_conn_by_fd(sockfd);
    if (conn == NULL) {
        return false;
    }
   return conn->offload_status == DTOE_OFFLOAD_SUCCESS;
}

void kbdtoe_conn_status_for_close(int sockfd)
{
    libdtoe_conn_s *conn = (libdtoe_conn_s *)get_conn_by_fd(sockfd);
    if (conn == NULL) {
        return;
    }
    conn->offload_status = DTOE_OFFLOAD_PRECLOSE;
}

bool kbdtoe_is_conn_offload(int sockfd)
{
    libdtoe_conn_s *conn = (libdtoe_conn_s *)get_conn_by_fd(sockfd);
    if (conn == NULL) {
        return false;
    }
    return true;
}

int kbdtoe_close(int fd)
{
    libdtoe_conn_s *conn = (libdtoe_conn_s *)get_conn_by_fd(fd);
    if (conn != NULL) {
        flexda_dtoe_prepare_close(conn->dtoe_conn);
    }
    return 0;
}

void kbdtoe_uninit()
{
    flexda_dtoe_uninit();
    kbdtoe_mempool_destroy();
    kbdtoe_log_uninit();
}

bool kbdtoe_is_channel_epoll_fd(int thread_idx, int fd)
{
    if (thread_idx < 0 || thread_idx >= g_thread_num) {
        KBDTOE_ERR("Invalid thread_idx %d", thread_idx);
        return false;
    }

    libdtoe_thread_pool_s* thread_pool = get_thread_pool(thread_idx);
    if (thread_pool == NULL) {
        KBDTOE_ERR("get thread pool failed for thread_idx %d", thread_idx);
        return false;
    }

    for (int i = 0; i < thread_pool->channel_num; i++) {
        if (thread_pool->send_channel_fd[i] == fd || thread_pool->recv_channel_fd[i] == fd) {
            return true;
        }
    }

    return false;
}

int kbdtoe_register_channel_fd_to_epoll(int thread_idx, int epoll_fd)
{
    if (thread_idx < 0 || thread_idx >= g_thread_num) {
        KBDTOE_ERR("Invalid thread_idx %d", thread_idx);
        return DTOE_FAIL;
    }

    libdtoe_thread_pool_s* thread_pool = get_thread_pool(thread_idx);
    if (thread_pool == NULL) {
        KBDTOE_ERR("get thread pool failed for thread_idx %d", thread_idx);
        return DTOE_FAIL;
    }

    for (int i = 0; i < thread_pool->channel_num; i++) {
        struct epoll_event ee = {0};
        ee.events = EPOLLIN;
        ee.data.fd = thread_pool->send_channel_fd[i];
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, thread_pool->send_channel_fd[i], &ee) != 0) {
            KBDTOE_ERR("Failed to add send channel fd %d to epoll, error: %s", thread_pool->send_channel_fd[i], strerror(errno));
            return DTOE_FAIL;
        }
        ee.data.fd = thread_pool->recv_channel_fd[i];
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, thread_pool->recv_channel_fd[i], &ee) != 0) {
            KBDTOE_ERR("Failed to add recv channel fd %d to epoll, error: %s", thread_pool->recv_channel_fd[i], strerror(errno));
            return DTOE_FAIL;
        }
    }

    return DTOE_SUCCESS;
}
