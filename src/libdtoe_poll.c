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
#include "kbdtoe_base.h"
#include "kbdtoe.h"
#include "dtoe_mempool_mr.h"

void kbdtoe_thread_poll(int thread_idx, struct knet_recv_events recv_events[], int *nr_recv_event)
{
    libdtoe_thread_pool_s* thread_pool = get_thread_pool(thread_idx);
    int nr_event = knet_poll_send_channel(thread_pool->send_channel[0], DTOE_CONN_PER_CHNL);
    if(nr_event != 0) {
        KBDTOE_ERR("kbdtoe kbdtoe thread poll send channel failed");
    }

    *nr_recv_event = knet_poll_recv_channel(thread_pool->recv_channel[0], recv_events, DTOE_RECV_MAX_DESC_NUM);
    for (int i = 0; i < *nr_recv_event; ++i) {
        libdtoe_conn_s *conn = (libdtoe_conn_s *)knet_get_ulp_user_data(recv_events[i].sockfd);
        __atomic_add_fetch(&conn->recv_desc_num, recv_events[i].iov_cnt, __ATOMIC_SEQ_CST);
    }
}