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
#include<stdio.h>
#include "kbdtoe_base.h"
#include "dtoe_mempool_mr.h"

 ssize_t kbdtoe_read(int fd, void *buf, size_t nbyte)
 {
    int ret = 0; 
    int iov_cnt = 0;
    struct knet_iovec iovs[DTOE_RECV_MAX_DESC_NUM];
    ssize_t read_length = 0;
    struct knet_iovec iov;
    libdtoe_conn_s *conn = (libdtoe_conn_s *)knet_get_ulp_user_data(fd);
    /** 解决卸载过程中数据丢包问题 */
    ssize_t leaked_size = knet_get_leaked_packet_size(fd);
    if (unlikely(leaked_size > 0)) {
        conn->leaked_buff = malloc(leaked_size);
        if (conn->leaked_buff == NULL) {
            KBDTOE_ERR("kbdtoe read malloc leaked buff failed");
            return 0;
        }
        ssize_t recved_bytes = knet_recv_leaked_packet(fd, conn->leaked_buff, leaked_size);
        if (recved_bytes < 0) {
            KBDTOE_ERR("kbdtoe read leaked packet failed");
            free(conn->leaked_buff);
            return 0;
        }
        conn->leaked_size = leaked_size;
        conn->read_leaked_offset = 0;
    }

    if (unlikely(conn->leaked_size > 0)) {
        if (conn->leaked_size > nbyte) {
            memcpy(buf + read_length, conn->leaked_buff + conn->read_leaked_offset, nbyte);
            conn->read_leaked_offset += nbyte;
            conn->leaked_size -= nbyte;
            return nbyte;
        } else {
            memcpy(buf + read_length, conn->leaked_buff + conn->read_leaked_offset, conn->leaked_size);
            nbyte -= conn->leaked_size;
            read_length += conn->leaked_size;
            conn->leaked_size = 0;
            free(conn->leaked_buff);
            conn->leaked_buff = NULL;
        }
    }

    while (__atomic_load_n(&conn->recv_desc_num, __ATOMIC_RELAXED) && (read_length < nbyte) && (iov_cnt < DTOE_RECV_MAX_DESC_NUM)) {
        if (conn->recv_desc.data_remain == 0) {
            ret = knet_recv(conn->fd, &iov, 1);
            if (ret < 0) {
                knet_recv_mem_loopback(iovs, iov_cnt);
                KBDTOE_ERR("kbdtoe conn:%p, knet recv failed, error =%d!", conn, ret);
                if (ret == -ECONNRESET) {
                    knet_prepare_close(conn->fd);
                    conn->conn_status = DTOE_CONN_PRE_CLOSING;
                }
                return 0;
            } else if (iov.iov_base == NULL) {
                KBDTOE_ERR("knet_rev debug!!!");
                knet_recv_mem_loopback(iovs, iov_cnt);
                knet_prepare_close(conn->fd);
                return 0;
            }
        } else {
            iov = conn->recv_desc.iov;
        }

        if ((read_length + iov.iov_len) <= nbyte) {
            memcpy(buf + read_length, iov.iov_base, iov.iov_len);
            read_length += iov.iov_len;
            (void)__atomic_fetch_sub(&conn->recv_desc_num, 1, __ATOMIC_SEQ_CST);

            if (conn->recv_desc.data_remain != 0) {
                iovs[iov_cnt].iov_base = conn->recv_desc.iov_origin.iov_base;
                iovs[iov_cnt].iov_len = conn->recv_desc.iov_origin.iov_len;
            } else {
                iovs[iov_cnt].iov_base = iov.iov_base;
                iovs[iov_cnt].iov_len = iov.iov_len;
            }
            iov_cnt++;
            conn->recv_desc.data_remain = 0;
        } else {
            memcpy((buf + read_length), iov.iov_base, (nbyte - read_length));
            if (conn->recv_desc.data_remain == 0) {
                conn->recv_desc.data_remain = 1;
                conn->recv_desc.iov_origin.iov_base = iov.iov_base;
                conn->recv_desc.iov_origin.iov_len = iov.iov_len;
            }
            iov.iov_base += (nbyte - read_length);
            iov.iov_len -= (nbyte - read_length);
            conn->recv_desc.iov = iov;
            read_length = nbyte;
        }
    }
    if (iov_cnt) {
        knet_recv_mem_loopback(iovs, iov_cnt);
    }
    return read_length;
 }