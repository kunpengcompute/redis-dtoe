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
#include "kbdtoe_base.h"
#include "kbdtoe_mempool_mr.h"
#include "securec.h"

ssize_t kbdtoe_write(int fd, const void *buf, size_t nbyte)
{
    int ret = 0;
    int memcpy_ret;
    unsigned char *ptr = NULL;
    ptr = (unsigned char*)kbdtoe_mempool_alloc(nbyte);
    if (ptr == NULL) {
        KBDTOE_ERR("kbdtoe write mempool alloc fail");
        errno = EAGAIN;
        return -1; 
    }
    memcpy_ret = memcpy_s(ptr, nbyte, buf, nbyte);
    if (memcpy_ret != EOK) {
        KBDTOE_ERR("kbdtoe write memcpy_s failed");
        kbdtoe_mempool_free(0, (uint64_t)ptr);
        errno = EFAULT;
        return -1;
    }
    struct iovec iov[1];
    iov[0].iov_base = ptr;
    iov[0].iov_len = nbyte;

    libdtoe_conn_s *conn = (libdtoe_conn_s *)get_conn_by_fd(fd);
    if (conn == NULL) {
        KBDTOE_ERR("kbdtoe write, conn is null for fd %d", fd);
        kbdtoe_mempool_free(0, (uint64_t)ptr);
        errno = EINVAL;
        return -1;
    }

    flexda_dtoe_mr_s *mr = get_dtoe_mr_s();
    flexda_dtoe_tx_info_s info = {
        .tx_in.op_code = 0,
        .tx_in.lkey = mr ? mr->lkey : 0,
    };
    ret = flexda_dtoe_send(conn->dtoe_conn, iov, 1, &info);
    if (ret < 0) {
        kbdtoe_mempool_free(0, (uint64_t)ptr);
        errno = -ret;
        return -1;
    }

    pthread_spin_lock(&conn->send.pending_send_lock);
    libdtoe_pending_send_s *pending = TAILQ_FIRST(&conn->send.free_req);
    if (pending != NULL) {
        TAILQ_REMOVE(&conn->send.free_req, pending, node);
    }
    pthread_spin_unlock(&conn->send.pending_send_lock);

    if (pending == NULL) {
        pending = (libdtoe_pending_send_s*)malloc(sizeof(libdtoe_pending_send_s));
    }

    if (pending != NULL) {
        pending->free_cb = kbdtoe_mempool_free;
        pending->wr_id = (uint64_t)ptr;
        pending->sockfd = fd;
        pending->send_sn = info.tx_out.curr_msn;
        pthread_spin_lock(&conn->send.pending_send_lock);
        TAILQ_INSERT_TAIL(&conn->send.unack_req, pending, node);
        pthread_spin_unlock(&conn->send.pending_send_lock);
    }

   return ret;
}

ssize_t kbdtoe_writev(int fd, const struct iovec *iov, int iovcnt)
{
    int ret = 0;
    int memcpy_ret;
    size_t total_size = 0;
    for (int i = 0; i < iovcnt; i++) {
        total_size += iov[i].iov_len;
    }
    unsigned char *ptr = NULL;
    ptr = (unsigned char*)kbdtoe_mempool_alloc(total_size);
    if (ptr == NULL) {
        KBDTOE_ERR("kbdtoe writev mempool alloc fail");
        errno = EAGAIN;
        return -1; 
    }
    size_t offset = 0;
    for (int i = 0; i < iovcnt; ++i) {
        memcpy_ret = memcpy_s(ptr + offset, iov[i].iov_len, iov[i].iov_base, iov[i].iov_len);
        if (memcpy_ret != EOK) {
            KBDTOE_ERR("kbdtoe write memcpy_s failed");
            kbdtoe_mempool_free(0, (uint64_t)ptr);
            errno = EFAULT;
            return -1;
        }
        offset += iov[i].iov_len;
    }

    struct iovec dtoe_iov[1];
    dtoe_iov[0].iov_base = ptr;
    dtoe_iov[0].iov_len = total_size;

    libdtoe_conn_s *conn = (libdtoe_conn_s *)get_conn_by_fd(fd);
    if (conn == NULL) {
        KBDTOE_ERR("kbdtoe writev, conn is null for fd %d", fd);
        kbdtoe_mempool_free(0, (uint64_t)ptr);
        errno = EINVAL;
        return -1;
    }

    flexda_dtoe_mr_s *mr = get_dtoe_mr_s();
    flexda_dtoe_tx_info_s info = {
        .tx_in.op_code = 0,
        .tx_in.lkey = mr ? mr->lkey : 0
    };
    ret = flexda_dtoe_send(conn->dtoe_conn, dtoe_iov, 1, &info);
    if (ret < 0) {
        kbdtoe_mempool_free(0, (uint64_t)ptr);
        errno = -ret;
        return -1;
    }

    pthread_spin_lock(&conn->send.pending_send_lock);
    libdtoe_pending_send_s *pending = TAILQ_FIRST(&conn->send.free_req);
    if (pending != NULL) {
        TAILQ_REMOVE(&conn->send.free_req, pending, node);
    }
    pthread_spin_unlock(&conn->send.pending_send_lock);

    if (pending == NULL) {
        pending = (libdtoe_pending_send_s*)malloc(sizeof(libdtoe_pending_send_s));
    }

    if (pending != NULL) {
        pending->free_cb = kbdtoe_mempool_free;
        pending->wr_id = (uint64_t)ptr;
        pending->sockfd = fd;
        pending->send_sn = info.tx_out.curr_msn;
        conn->send.last_send_sn = info.tx_out.curr_msn;
        pthread_spin_lock(&conn->send.pending_send_lock);
        TAILQ_INSERT_TAIL(&conn->send.unack_req, pending, node);
        pthread_spin_unlock(&conn->send.pending_send_lock);
    }

    return ret;
}

