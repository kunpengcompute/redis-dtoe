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
#include "kbdtoe_mempool_mr.h"

extern struct libdtoe_conn_readable_event_head g_readable_event_head;

bool kbdtoe_thread_poll(int thread_idx, struct knet_recv_events recv_events[], int *nr_recv_event)
{
    libdtoe_thread_pool_s* thread_pool = get_thread_pool(thread_idx);
    int nr_send_event = knet_poll_send_channel(thread_pool->send_channel[0], DTOE_CONN_PER_CHNL);
    if (nr_send_event < 0) {
        KBDTOE_ERR("kbdtoe kbdtoe thread poll send channel failed, ret:%d\n", nr_send_event);
    } else if (nr_send_event >= 0) {
        KBDTOE_DEBUG("kbdtoe kbdtoe thread poll send channel, nr_send_event:%d\n", nr_send_event);
    }

    *nr_recv_event = knet_poll_recv_channel(thread_pool->recv_channel[0], recv_events, DTOE_RECV_MAX_DESC_NUM);
    for (int i = 0; i < *nr_recv_event; ++i) {
        libdtoe_conn_s *conn = (libdtoe_conn_s *)knet_get_ulp_user_data(recv_events[i].sockfd);
        __atomic_add_fetch(&conn->recv_desc_num, recv_events[i].iov_cnt, __ATOMIC_RELAXED);
        conn->poll_mask = 1;
    }

    libdtoe_conn_s *node = NULL;
    TAILQ_FOREACH(node, &g_readable_event_head, readable_event_node) {
        if (node->poll_mask) {
            node->poll_mask = 0;
            continue;
        }
        recv_events[*nr_recv_event].sockfd = node->fd;
        node->poll_mask = 0;
        (*nr_recv_event)++;
    }
    if (nr_send_event > 0 || *nr_recv_event > 0) {
        return true;
    }
    return false;
}

int kbdtoe_enable_epoll_mode(uint32_t epoll_enable)
{
    libdtoe_thread_pool_s* thread_pool = get_thread_pool(0);
    return knet_flexda_dtoe_channel_epoll_set(thread_pool->send_channel[0], thread_pool->recv_channel[0], epoll_enable);
}