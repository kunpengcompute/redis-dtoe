/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
* redis dtoe  is licensed under the Mulan PSL v2.
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
#include "securec.h"

extern struct libdtoe_conn_readable_event_head g_readable_event_head;

ssize_t kbdtoe_read(int fd, void *buf, size_t nbyte)
{
    int ret = 0;
    int memcpy_ret;
    int iov_cnt = 0;
    struct iovec iovs[DTOE_RECV_MAX_DESC_NUM];
    ssize_t read_length = 0;
    struct iovec iov;
    libdtoe_conn_s *conn = (libdtoe_conn_s *)get_conn_by_fd(fd);
    libdtoe_recv_desc_s *recv_desc = NULL;
    recv_desc = &conn->recv_desc;
    if (conn->offload_status == DTOE_OFFLOAD_START) {
        errno = EAGAIN;
        return -1;
    }
    /** 解决卸载过程中数据丢包问题 */
    ssize_t leaked_size = flexda_dtoe_get_leaked_packet_size(conn->dtoe_conn);
    if (unlikely(leaked_size > 0)) {
        conn->leaked_buff = malloc(leaked_size);
        if (conn->leaked_buff == NULL) {
            KBDTOE_ERR("kbdtoe read malloc leaked buff failed");
            errno = ENOMEM;
            return -1;
        }
        ssize_t recved_bytes = flexda_dtoe_recv_leaked_packet(conn->dtoe_conn, conn->leaked_buff, leaked_size);
        if (recved_bytes < 0) {
            KBDTOE_ERR("kbdtoe read leaked packet failed");
            free(conn->leaked_buff);
            errno = -recved_bytes;
            return -1;
        }
        conn->leaked_size = leaked_size;
        conn->read_leaked_offset = 0;
    }

    if (unlikely(conn->leaked_size > 0)) {
        size_t leaked_copy_len = (conn->leaked_size > nbyte) ? nbyte : (size_t)conn->leaked_size;
        memcpy_ret = memcpy_s(buf, leaked_copy_len, (char *)conn->leaked_buff + conn->read_leaked_offset, leaked_copy_len);
        if (memcpy_ret != EOK) {
           KBDTOE_ERR("kbdtoe read leaked memcpy_s failed");
           errno = EFAULT;
           return -1;
        }
        conn->read_leaked_offset += leaked_copy_len;
        conn->leaked_size -= leaked_copy_len;
        read_length += leaked_copy_len;
        if (conn->leaked_size == 0) {
            free(conn->leaked_buff);
            conn->leaked_buff = NULL;
            if (conn->has_readable_event) {
                TAILQ_REMOVE(&g_readable_event_head, conn, readable_event_node);
                conn->has_readable_event = 0;
            }
        }
        if (read_length == nbyte) {
            if (conn->leaked_size > 0) {
                conn->has_readable_event = 1;
            }
            if (conn->has_readable_event) {
                TAILQ_INSERT_TAIL(&g_readable_event_head, conn, readable_event_node);
            }
            return read_length;
        }
    }
    if (conn->has_readable_event) {
        TAILQ_REMOVE(&g_readable_event_head, conn, readable_event_node);
        conn->has_readable_event = 0;
    }

    while (__atomic_load_n(&conn->recv_desc_num, __ATOMIC_RELAXED) && (read_length < nbyte) && (iov_cnt < DTOE_RECV_MAX_DESC_NUM)) {
        if (recv_desc->data_remain == 0) {
            ret = flexda_dtoe_recv(conn->dtoe_conn, &iov, 1);
            if (ret < 0) {
                flexda_dtoe_recv_mem_loopback(iovs, iov_cnt);
                KBDTOE_ERR("kbdtoe conn:%p, flexda_dtoe_recv failed, error =%d!", conn, ret);
                errno = -ret;
                return -1;
            } else if (iov.iov_base == NULL) {
                KBDTOE_ERR("flexda_dtoe_recv debug!!!");
                flexda_dtoe_recv_mem_loopback(iovs, iov_cnt);
                errno = ENOBUFS; // fix me
                return -1;
            }
        } else {
            iov.iov_base = recv_desc->iov.iov_base;
            iov.iov_len = recv_desc->iov.iov_len;
        }

        if ((read_length + iov.iov_len) <= nbyte) {
            memcpy_ret = memcpy_s(buf + read_length, iov.iov_len, iov.iov_base, iov.iov_len);
            if (memcpy_ret != EOK) {
               KBDTOE_ERR("kbdtoe read memcpy_s scene3 failed");
               flexda_dtoe_recv_mem_loopback(iovs, iov_cnt);
               errno = EFAULT;
               return -1;
            }
            read_length += iov.iov_len;
            (void)__atomic_fetch_sub(&conn->recv_desc_num, 1, __ATOMIC_RELAXED);

            if (recv_desc->data_remain != 0) {
                iovs[iov_cnt].iov_base = conn->recv_desc.iov_origin.iov_base;
                iovs[iov_cnt].iov_len = conn->recv_desc.iov_origin.iov_len;
            } else {
                iovs[iov_cnt].iov_base = iov.iov_base;
                iovs[iov_cnt].iov_len = iov.iov_len;
            }
            iov_cnt++;
            recv_desc->data_remain = 0;
        } else {
            size_t remain_len = nbyte - read_length;
            memcpy_ret = memcpy_s((buf + read_length), remain_len, iov.iov_base, remain_len);
            if (memcpy_ret != EOK) {
               KBDTOE_ERR("kbdtoe read memcpy_s scene4 failed");
               flexda_dtoe_recv_mem_loopback(iovs, iov_cnt);
               errno = EFAULT;
               return -1;
            }
            if (recv_desc->data_remain == 0) {
                recv_desc->data_remain = 1;
                recv_desc->iov_origin.iov_base = iov.iov_base;
                recv_desc->iov_origin.iov_len = iov.iov_len;
            }
            iov.iov_base = (char*)iov.iov_base + remain_len;
            iov.iov_len -= remain_len;
            recv_desc->iov.iov_base = iov.iov_base;
            recv_desc->iov.iov_len = iov.iov_len;
            read_length = nbyte;
        }
    }
    if (iov_cnt) {
        flexda_dtoe_recv_mem_loopback(iovs, iov_cnt);
    }
    if (__atomic_load_n(&conn->recv_desc_num, __ATOMIC_RELAXED)) {
        TAILQ_INSERT_TAIL(&g_readable_event_head, conn, readable_event_node);
        conn->has_readable_event = 1;
    }
    return read_length;
}
