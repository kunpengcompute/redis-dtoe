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
#ifndef KB_DTOE_H
#define KB_DTOE_H
#ifdef __cplusplus
extern "C" {
#endif
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/uio.h>

/******************************************************************
  Prototype    : kbdtoe_init
  Description  : dtoe 初始化函数
  Input        : const char* dtoe_ip  dtoe网卡
               : unsigned int max_conn_num 最大连接数
  Return Value : 0 success， others-failed
 **************************************************************/
int kbdtoe_init(const char* dtoe_ip, unsigned int max_conn_num);

/******************************************************************
  Prototype    : kbdtoe_uninit 
  Description  : dtoe 销毁函数
  Return Value : None
 **************************************************************/
void kbdtoe_uninit();

/******************************************************************
  Prototype    : kbdtoe_conn_start_offload 
  Description  : 单个TCP连接开始卸载的函数，必须在accept之后调用
  Input        : int sockfd TCP accept之后的sock fd
  Return Value : 0 success， others-failed
 **************************************************************/
int kbdtoe_conn_start_offload(int sockfd);

typedef struct flexda_dtoe_iovec {
    void* iov_base;
    size_t iov_len;
} flexda_dtoe_iovec_s;

enum kbdtoe_recv_event_type {
    KBDTOE_RX_EVENT_NORMAL,
    KBDTOE_RX_EVENT_LEAK,
};

struct kbdtoe_recv_events {
    int sockfd;
    uint32_t iov_cnt;
    enum kbdtoe_recv_event_type type;
};

/******************************************************************
  Prototype    : kbdtoe_thread_poll 
  Description  : 指定线程的轮询事件函数
  Intput       : int thread_index  线程的index
  Output       ：1) struct kbdtoe_recv_events recv_events[] 线程可读的事件信息
                 2) int *nr_recv_event  线程可读的事事件个数
  Return Value : true-还有事件需要继续poll， false-没有事件了
 **************************************************************/
bool kbdtoe_thread_poll(int thread_index, struct kbdtoe_recv_events recv_events[], int *nr_recv_event);

/******************************************************************
  Prototype    : kbdtoe_read 
  Description  : 语义类似read函数
  Return Value : 读到的字节数
 **************************************************************/
ssize_t kbdtoe_read(int fd, void *buf, size_t nbyte);

/******************************************************************
  Prototype    : kbdtoe_write 
  Description  : 语义类似write函数
  Return Value : 写入的字节数
 **************************************************************/
ssize_t kbdtoe_write(int fd, const void *buf, size_t nbyte);

/******************************************************************
  Prototype    : kbdtoe_writev 
  Description  : 语义类似writev函数
  Return Value : 写入的字节数
 **************************************************************/
ssize_t kbdtoe_writev(int fd, const struct iovec *iov, int iovcnt);

/******************************************************************
  Prototype    : kbdtoe_close 
  Description  : 释放单个dtoe连接的资源
  Return Value : 0 success， others-failed
 **************************************************************/
int kbdtoe_close(int fd);

/******************************************************************
  Prototype    : kbdtoe_is_conn_offload_success 
  Description  : 判断单个dtoe连接是否卸载完成
  Input        : int sockfd TCP accept之后的sock fd
  Return Value : 0 success， others-failed
 **************************************************************/
bool kbdtoe_is_conn_offload_success(int sockfd);

/******************************************************************
  Prototype    : kbdtoe_is_conn_offload 
  Description  : 判断单个socket fd 是否进行dtoe 卸载
  Input        : int sockfd TCP accept之后的sock fd
  Return Value : 0 success， others-failed
 **************************************************************/
bool kbdtoe_is_conn_offload(int sockfd);

/******************************************************************
  Prototype    : kbdtoe_conn_status_for_close
  Description  : 设置卸载TCP进行预上载
  Input        : int sockfd TCP accept之后的sock fd
  Return Value : None
 **************************************************************/
void kbdtoe_conn_status_for_close(int sockfd);

/******************************************************************
  Prototype    : kbdtoe_is_channel_epoll_fd
  Description  : 判断fd是否为dtoe通信的channel epoll fd
  Input        : int thread_idx 线程index
               : int fd 待判断的fd
  Return Value : true-是channel epoll fd， false-非channel epoll fd
 **************************************************************/
bool kbdtoe_is_channel_epoll_fd(int thread_idx, int fd);

/******************************************************************
  Prototype    : kbdtoe_register_channel_fd_to_epoll
  Description  : 将dtoe通信的channel fd注册到epoll中
  Input        : int thread_idx 线程index
               : int epoll_fd epoll fd
  Return Value : 0 success， others-failed
 **************************************************************/
int kbdtoe_register_channel_fd_to_epoll(int thread_idx, int epoll_fd);

typedef void (*dtoe_close_done_callback_t) (int sockfd);
void register_dtoe_close_done_callback(dtoe_close_done_callback_t cb);
#ifdef __cplusplus
}
#endif
#endif

