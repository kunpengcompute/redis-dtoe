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
*/
#ifndef KBDTOE_LOG_H
#define KBDTOE_LOG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <syslog.h>

#define DTOE_THREAD_NAME_LEN 128
#define KBDTOE_LOG_MODULE_NAME "libkbdtoe" // 定义输出日志的模块名字，使得在输出日志中统一
#define KBDTOE_LIMIT_LOG_DEFAULT_INTERNEL_IN_MS (100) /* 0.1s */

typedef enum {
    KBDTOE_LOG_EMERG = LOG_EMERG,
    KBDTOE_LOG_ERR = LOG_ERR,
    KBDTOE_LOG_WARN = LOG_WARNING,
    KBDTOE_LOG_DEFAULT = KBDTOE_LOG_WARN,
    KBDTOE_LOG_INFO = LOG_INFO,
    KBDTOE_LOG_DEBUG = LOG_DEBUG,
    KBDTOE_LOG_MAX,
} KBDTOE_LogLevel;

void kbdtoe_log(const char *function, int line, int level, const char *format, ...);
void kbdtoe_log_init(void);
void kbdtoe_log_uninit(void);
uint64_t kbdtoe_get_current_time_millis(void);
void kbdtoe_loglevel_set(KBDTOE_LogLevel log_level);
KBDTOE_LogLevel kbdtoe_loglevel_get(void);
void kbdtoe_loglevel_set_by_str(const char *level_str);

#define KBDTOE_LOG(level, fmt, args...) kbdtoe_log(__func__, __LINE__, level, fmt, ##args)
#define KBDTOE_ERR(fmt, args...) kbdtoe_log(__func__, __LINE__, KBDTOE_LOG_ERR, "[ERR] " fmt, ##args)
#define KBDTOE_WARN(fmt, args...) kbdtoe_log(__func__, __LINE__, KBDTOE_LOG_WARN, "[WARN] " fmt, ##args)
#define KBDTOE_INFO(fmt, args...) kbdtoe_log(__func__, __LINE__, KBDTOE_LOG_INFO, "[INFO] " fmt, ##args)
#define KBDTOE_DEBUG(fmt, args...) kbdtoe_log(__func__, __LINE__, KBDTOE_LOG_DEBUG, "[DEBUG] " fmt, ##args)

#define KBDTOE_LOG_LINE_LIMIT(log_level, fmt, args...)                                                                 \
    do {                                                                                                             \
        if (kbdtoe_loglevel_get() < (log_level)) {                                                                      \
            break;                                                                                                   \
        }                                                                                                            \
        static __thread uint32_t limit_times##__func__##__LINE__ = 0;                                               \
        static __thread uint64_t last_ms##__func__##__LINE__ = 0;                                                   \
        uint64_t now_ms = kbdtoe_get_current_time_millis();                                                               \
        if ((now_ms - last_ms##__func__##__LINE__) < KBDTOE_LIMIT_LOG_DEFAULT_INTERNEL_IN_MS) {                      \
            ++limit_times##__func__##__LINE__;                                                                       \
            break;                                                                                                   \
        }                                                                                                            \
        last_ms##__func__##__LINE__ = now_ms;                                                                        \
        switch (log_level) {                                                                                         \
            case KBDTOE_LOG_ERR:                                                                                       \
                kbdtoe_log(__func__, __LINE__, (log_level),                                                            \
                    "[ERR | limitTimes %u]  " fmt, limit_times##__func__##__LINE__, ##args);                      \
                break;                                                                                               \
            case KBDTOE_LOG_WARN:                                                                                      \
                kbdtoe_log(__func__, __LINE__, (log_level),                                                            \
                    "[WARN | limitTimes %u]  " fmt, limit_times##__func__##__LINE__, ##args);                     \
                break;                                                                                               \
            case KBDTOE_LOG_INFO:                                                                                      \
                kbdtoe_log(__func__, __LINE__, (log_level),                                                            \
                    "[INFO | limitTimes %u]  " fmt, limit_times##__func__##__LINE__, ##args);                     \
                break;                                                                                               \
            default:                                                                                                 \
                kbdtoe_log(__func__, __LINE__, (log_level),                                                            \
                    "[DEBUG | limitTimes %u]  " fmt, limit_times##__func__##__LINE__, ##args);                    \
                break;                                                                                               \
        }                                                                                                            \
        limit_times##__func__##__LINE__ = 0;                                                                         \
    } while (0)

#endif // __KBDTOE_LOG_H__