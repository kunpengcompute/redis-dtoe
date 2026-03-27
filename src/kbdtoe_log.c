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
#include "kbdtoe_log.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <pthread.h>
#include <time.h>
#include "securec.h"
#include "kbdtoe_base.h"
/* Some toolchains require an explicit prototype for getpid() */
extern pid_t getpid(void);

#define PROCESS_PATH_SIZE 1024
#define MAX_LOG_LEN 1024
#define INVALID_PROC_NAME "invalid proc name"
#define NUM_2 2
#define MILLISECONDS_PER_SECOND 1000
#define NANOS_PER_MILLISECOND 1000000
#define PRINT_ERR(fmt, ...) printf("[ERR] %s|%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

typedef struct {
    KBDTOE_LogLevel level;
    const char *level_name;
} dtoe_loglevel_name_s;

static KBDTOE_LogLevel g_loglevel = KBDTOE_LOG_DEFAULT;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static char g_proc_name[DTOE_THREAD_NAME_LEN];
static const dtoe_loglevel_name_s g_loglevel_name[] = {
    {KBDTOE_LOG_ERR, "ERROR" },
    {KBDTOE_LOG_WARN, "WARNING"},
    {KBDTOE_LOG_INFO, "INFO"},
    {KBDTOE_LOG_DEBUG, "DEBUG"},
};

static const char *get_self_thread_name(char *name, size_t len)
{
    if (name == NULL || len < DTOE_THREAD_NAME_LEN) {
        return "invalid parameter";
    }
    if (prctl(PR_GET_NAME, name) < 0) {
        return "invalid thread name";
    }
    return name;
}

uint64_t kbdtoe_get_current_time_millis(void)
{
    struct timespec ts = {0};
    int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (ret != 0) {
        KBDTOE_ERR("DTOE clock gettime error, ret %d, errno %d, %s", ret, errno, strerror(errno));
        return DTOE_FAIL;
    }
    // 将时间转换为毫秒，timespec在系统时间正常的情况下不会为负值。
    uint64_t milliseconds = (uint64_t)ts.tv_sec * MILLISECONDS_PER_SECOND
        + (uint64_t)ts.tv_nsec / NANOS_PER_MILLISECOND;
    return milliseconds;
}

void kbdtoe_log_mutex_lock(void)
{
    pthread_mutex_lock(&g_log_mutex);
}

void kbdtoe_log_mutex_unlock(void)
{
    pthread_mutex_unlock(&g_log_mutex);
}

void kbdtoe_loglevel_set(KBDTOE_LogLevel log_level)
{
    if (log_level >= KBDTOE_LOG_MAX) {
        KBDTOE_ERR("logLevel %u not support", log_level);
        return;
    }
    g_loglevel = log_level;
}

KBDTOE_LogLevel kbdtoe_loglevel_get(void)
{
    return g_loglevel;
}

static const char *get_self_process_name(char *name, size_t len)
{
    char path[PROCESS_PATH_SIZE] = {0};
    if (readlink("/proc/self/exe", path, sizeof(path) - 1) <= 0) {
        return INVALID_PROC_NAME;
    }

    char *proc_name = strrchr(path, '/');
    if (proc_name == NULL || path + strlen(path) <= proc_name + NUM_2) {
        return INVALID_PROC_NAME;
    }
    if (strlen(proc_name + 1) > len) {
        return INVALID_PROC_NAME;
    }
    int ret = strcpy_s(name, len, proc_name + 1);
    if (ret != 0) {
        return INVALID_PROC_NAME;
    }

    return name;
}

static void kbdtoe_log_lock_unlock(void *arg)
{
    (void)arg;
    pthread_mutex_unlock(&g_log_mutex);
}

static int thread_safe_syslog(int level, char log_msg[MAX_LOG_LEN])
{
    int orig_cancel_type;
    int ret = pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &orig_cancel_type);
    if (ret != 0) {
        PRINT_ERR("Pthread set canceltype failed, ret %d", ret);
        return ret;
    }

    int orig_cancel_state;
    pthread_cleanup_push(kbdtoe_log_lock_unlock, NULL);
    ret = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &orig_cancel_state);
    if (ret != 0) {
        PRINT_ERR("Pthread set cancelstate failed, ret %d", ret);
        return ret;
    }

    pthread_mutex_lock(&g_log_mutex);
    syslog(level, "%s", log_msg);
    pthread_mutex_unlock(&g_log_mutex);

    ret = pthread_setcancelstate(orig_cancel_state, NULL);
    if (ret != 0) {
        PRINT_ERR("Pthread restore canceltype failed, ret %d", ret);
    }
    pthread_cleanup_pop(1);  // Ensure push/pop are paired in the same lexical scope.

    ret = pthread_setcanceltype(orig_cancel_type, NULL);
    if (ret != 0) {
        PRINT_ERR("Pthread restore cancelstate failed, ret %d", ret);
    }

    return 0;
}

static int log_message(const char *function, int line, int level, const char *format, va_list va)
{
    char new_format[MAX_LOG_LEN] = {0};
    char thread_name[DTOE_THREAD_NAME_LEN] = {0};
    if (g_proc_name[0] == '\0') {
        (void)get_self_process_name(g_proc_name, DTOE_THREAD_NAME_LEN);
    }
    int ret = sprintf_s(new_format, MAX_LOG_LEN, "[%s:%d | %s:%ld]  "
#ifdef DTOE_DEBUG_BUILD
        "%s[%d]|"
#endif
        "%s\n",
        g_proc_name, getpid(),
        get_self_thread_name(thread_name, DTOE_THREAD_NAME_LEN), syscall(__NR_gettid),
#ifdef DTOE_DEBUG_BUILD
        function, line,
#endif
        format);
    if (ret <= 0 || ret >= sizeof(new_format)) {
        PRINT_ERR("Sprintf failed ret %d, errno %d", ret, errno);
        return ret;
    }

    char log_msg[MAX_LOG_LEN] = {0};
    ret = vsprintf_s(log_msg, MAX_LOG_LEN, new_format, va);
    if (ret == -1) {
        PRINT_ERR("LogMsg size exceeds MAX_LOG_LEN size %d, errno %d", MAX_LOG_LEN, errno);
        return ret;
    }

    ret = thread_safe_syslog(level, log_msg);
    if (ret != 0) {
        PRINT_ERR("Failed to write log in a thread-safe manner, ret %d errno %d", ret, errno);
        return ret;
    }

#ifdef DTOE_DEBUG_BUILD
    (void)printf("%s", log_msg);
#endif

    return ret;
}

void kbdtoe_log(const char *function, int line, int level, const char *format, ...)
{
    if (likely(g_loglevel < level)) {
        return;
    }

    va_list va;
    int ori_errno = errno;
    va_start(va, format);
    (void)log_message(function, line, level, format, va);
    va_end(va);
    errno = ori_errno;
}

void kbdtoe_loglevel_set_by_str(const char *level_str)
{
    kbdtoe_loglevel_set(KBDTOE_LOG_DEFAULT);

    size_t len = sizeof(g_loglevel_name) / sizeof(g_loglevel_name[0]);
    for (size_t i = 0; i < len; ++i) {
        if (strcmp(level_str, g_loglevel_name[i].level_name) == 0) {
            kbdtoe_loglevel_set(g_loglevel_name[i].level);
            return;
        }
    }
    KBDTOE_ERR("Log level not support, use default log level %u", KBDTOE_LOG_DEFAULT);
}

void kbdtoe_log_init()
{
    openlog(KBDTOE_LOG_MODULE_NAME, LOG_PID | LOG_CONS | LOG_NDELAY, LOG_USER);
    (void)setlogmask(LOG_MASK(LOG_ERR) | LOG_MASK(LOG_WARNING) | LOG_MASK(LOG_INFO) | LOG_MASK(LOG_DEBUG));
    get_self_process_name(g_proc_name, DTOE_THREAD_NAME_LEN);
}

void kbdtoe_log_uninit(void)
{
    closelog();
}
