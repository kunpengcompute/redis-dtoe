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
#include <stdio.h>
#include "kbdtoe_base.h"
#include "dtoe_mempool_mr.h"
#include "securec.h"

ssize_t kbdtoe_write(int fd, const void *buf, size_t nbyte)
{
    int ret = 0;
    int memcpy_ret;
    unsigned char *ptr = NULL;
    ptr = (unsigned char*)dtoe_mempool_alloc(nbyte);
    if (ptr == NULL) {
        KBDTOE_ERR("kbdtoe write mempool alloc fail");
        errno = EAGAIN;
        return -1; 
    }
    memcpy_ret = memcpy_s(ptr, nbyte, buf, nbyte); // 应用程序已保证nbyte的合法
    if (memcpy_ret != EOK) {
        KBDTOE_ERR("kbdtoe write memcpy_s failed");
        errno = EAGAIN;
        return -1;
    }
    struct knet_tx_req tx_req = {0};
    struct knet_iovec iov[1];
    tx_req.iov = iov;
    tx_req.iov[0].iov_base = ptr;
    tx_req.iov[0].iov_len = nbyte;
    tx_req.lkey = get_dtoe_mr_s()->lkey;
    tx_req.wr_id = (uint64_t)ptr;
    tx_req.free_cb = dtoe_mempool_free;
    tx_req.iov_cnt = 1;
    ret = knet_send(fd, &tx_req);
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
    ptr = (unsigned char*)dtoe_mempool_alloc(total_size);
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
            errno = EAGAIN;
            return -1;
        }
        offset += iov[i].iov_len;
    }
    struct knet_tx_req tx_req = {0};
    struct knet_iovec dtoe_iov[1];
    tx_req.iov = dtoe_iov;
    tx_req.iov[0].iov_base = ptr;
    tx_req.iov[0].iov_len = total_size;
    tx_req.lkey = get_dtoe_mr_s()->lkey;
    tx_req.free_cb = dtoe_mempool_free;
    tx_req.wr_id = (uint64_t)ptr;
    tx_req.iov_cnt = 1;
    ret = knet_send(fd, &tx_req);
    return ret;
}