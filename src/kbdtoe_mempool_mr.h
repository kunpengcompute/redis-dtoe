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
#ifndef KBDTOE_MEMPOOL_MR_H
#define KBDTOE_MEMPOOL_MR_H
#ifdef __cplusplus
extern "C" {
#endif
#include <inttypes.h>
#include <stdint.h>
#include <stddef.h>
#include "kbdtoe_base.h"

/******************************************************************
  Prototype    : kbdtoe_mempool_init 
  Description  : 分配1G的内存并完成虚拟地址物理地址的映射
  Return Value : 0 success others-failed
 **************************************************************/
int kbdtoe_mempool_init();

/******************************************************************
  Prototype    : kbdtoe_mempool_destroy 
  Description  : 释放内存池
  Input        : None
  Output       : None
  Return Value : None
 **************************************************************/
void kbdtoe_mempool_destroy();

/******************************************************************
  Prototype    : kbdtoe_mempool_alloc 
  Description  : 从内存池获取所需要大小内存的起始地址
  Input        : size_t size: 需要内存的大小
  Output       : None
  Return Value : NULL failed, others return start address
 **************************************************************/
void* kbdtoe_mempool_alloc(size_t size);

/******************************************************************
  Prototype    : kbdtoe_mempool_free 
  Description  : 将内存归还到内存池
  Input        : uint64_t wid:  内存池的地址
  Input        : int sockfd:  socket的fd
  Output       : None
  Return Value : None
 **************************************************************/
void kbdtoe_mempool_free(int sockfd, uint64_t wid);


/******************************************************************
  Prototype    : get_dtoe_mr_s
  Description  : 注册成功后放回的MR信息，发生数据时需要用到,可选
  Input        : None
  Output       : None
  Return Value : struct flexda_dtoe_mr:注册成功后放回的MR信息
 **************************************************************/
flexda_dtoe_mr_s *get_dtoe_mr_s();

/******************************************************************
  Prototype    : kbdtoe_mempool_stats
  Description  : 打印内存使用情况
  Input        : None
  Output       : None
  Return Value : None
 **************************************************************/
void kbdtoe_mempool_stats();
#ifdef __cplusplus
}
#endif
#endif

