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
    struct knet_rx_desc recv_desc;
    libdtoe_conn_s *conn = (libdtoe_conn_s *)knet_get_ulp_user_data(fd);
    while (__atomic_load_n(&conn->recv_desc_num, __ATOMIC_RELAXED) && (read_length < nbyte) && (iov_cnt < DTOE_RECV_MAX_DESC_NUM)) {
        if (conn->recv_desc.data_remain == 0) {
            ret = knet_recv(conn->fd, &recv_desc, 1, 0);
            if (ret < 0) {
                knet_recv_mem_loopback(iovs, iov_cnt);
                KBDTOE_ERR("kbdtoe conn:%p, knet recv failed, error =%d!\n", conn, ret);
                if (ret == -ECONNRESET) {
                    knet_prepare_close(conn->fd);
                    conn->conn_status = DTOE_CONN_PRE_CLOSING;
                }
                return -1;
            } else if (recv_desc.iov.iov_base == NULL) {
                KBDTOE_ERR("knet_rev debug!!!\n");
                return -1;
            }
        } else {
            recv_desc = conn->recv_desc.desc;
        }

        if ((read_length + recv_desc.iov.iov_len) <= nbyte) {
            memcpy(buf + read_length, recv_desc.iov.iov_base, recv_desc.iov.iov_len);
            read_length += recv_desc.iov.iov_len;
            (void)__atomic_fetch_sub(&conn->recv_desc_num, 1, __ATOMIC_SEQ_CST);

            if (conn->recv_desc.data_remain != 0) {
                iovs[iov_cnt].iov_base = conn->recv_desc.iov_origin.iov_base;
                iovs[iov_cnt].iov_len = conn->recv_desc.iov_origin.iov_len;
            } else {
                iovs[iov_cnt].iov_base = recv_desc.iov.iov_base;
                iovs[iov_cnt].iov_len = recv_desc.iov.iov_len;
            }
            iov_cnt++;
            conn->recv_desc.data_remain = 0;
        } else {
            memcpy((buf + read_length), recv_desc.iov.iov_base, (nbyte - read_length));
            if (conn->recv_desc.data_remain == 0) {
                conn->recv_desc.data_remain = 1;
                conn->recv_desc.iov_origin.iov_base = recv_desc.iov.iov_base;
                conn->recv_desc.iov_origin.iov_len = recv_desc.iov.iov_len;
            }
            recv_desc.iov.iov_base += (nbyte - read_length);
            recv_desc.iov.iov_len -= (nbyte - read_length);
            conn->recv_desc.desc = recv_desc;
            read_length = nbyte;
        }
    }
    if (iov_cnt) {
        knet_recv_mem_loopback(iovs, iov_cnt);
    }
    return read_length;
 }