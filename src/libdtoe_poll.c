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

extern struct libdtoe_conn_readable_event_head g_readable_event_head;

bool kbdtoe_thread_poll(int thread_idx, struct kbdtoe_recv_events recv_events[], int *nr_recv_event)
{
    libdtoe_thread_pool_s* thread_pool = get_thread_pool(thread_idx);
    int nr_send_event = flexda_dtoe_poll_send_channel(thread_pool->send_channel[0], DTOE_CONN_PER_CHNL);
    if (nr_send_event < 0) {
        KBDTOE_ERR("kbdtoe kbdtoe thread poll send channel failed, ret:%d\n", nr_send_event);
    } else if (nr_send_event >= 0) {
        KBDTOE_DEBUG("kbdtoe kbdtoe thread poll send channel, nr_send_event:%d\n", nr_send_event);
    }

    libdtoe_recv_channel_wrapper_s *recv_channel = thread_pool->recv_channel[0];
    recv_channel->next_event_idx = 0;
    recv_channel->maxevents = (*nr_recv_event) != 0 ? (*nr_recv_event) : DTOE_RECV_MAX_DESC_NUM;
    *nr_recv_event = 0;
    recv_channel->events = recv_events;
    int poll_max_cnt = recv_channel->maxevents;
    if (poll_max_cnt > DTOE_RECV_MAX_DESC_NUM) {
        poll_max_cnt = DTOE_RECV_MAX_DESC_NUM;
    }

    int remain_ceq_events = flexda_dtoe_poll_receive_channel(&recv_channel->channel, poll_max_cnt);

    for (uint32_t i = 0; i < recv_channel->next_event_idx; ++i) {
        libdtoe_conn_s *conn = (libdtoe_conn_s *)get_conn_by_fd(recv_channel->events[i].sockfd);
        if (conn != NULL) {
            conn->recv_event_index = -1;
            __atomic_add_fetch(&conn->recv_desc_num, recv_channel->events[i].iov_cnt, __ATOMIC_RELAXED);
            conn->poll_mask = 1;
        }
    }

    *nr_recv_event = recv_channel->next_event_idx;

    libdtoe_conn_s *node = NULL;
    TAILQ_FOREACH(node, &g_readable_event_head, readable_event_node) {
        if (node->poll_mask) {
            node->poll_mask = 0;
            continue;
        }
        if (*nr_recv_event >= recv_channel->maxevents) {
            break;
        }
        recv_events[*nr_recv_event].sockfd = node->fd;
        recv_events[*nr_recv_event].iov_cnt = 0;
        node->poll_mask = 0;
        (*nr_recv_event)++;
    }

    if (nr_send_event > 0 || *nr_recv_event > 0 || remain_ceq_events > 0) {
        return true;
    }
    return false;
}
