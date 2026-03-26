#include <assert.h>
#include <stdint.h>
#include <unistd.h>

#include "kbdtoe_log.h"

static void test_level_set_get(void)
{
    kbdtoe_loglevel_set(KBDTOE_LOG_WARN);
    assert(kbdtoe_loglevel_get() == KBDTOE_LOG_WARN);

    kbdtoe_loglevel_set(KBDTOE_LOG_DEBUG);
    assert(kbdtoe_loglevel_get() == KBDTOE_LOG_DEBUG);

    /* invalid level should not change current value */
    kbdtoe_loglevel_set((KBDTOE_LogLevel)KBDTOE_LOG_MAX);
    assert(kbdtoe_loglevel_get() == KBDTOE_LOG_DEBUG);
}

static void test_level_set_by_str(void)
{
    kbdtoe_loglevel_set_by_str("INFO");
    assert(kbdtoe_loglevel_get() == KBDTOE_LOG_INFO);

    kbdtoe_loglevel_set_by_str("DEBUG");
    assert(kbdtoe_loglevel_get() == KBDTOE_LOG_DEBUG);

    /* unknown level falls back to default */
    kbdtoe_loglevel_set_by_str("UNKNOWN_LEVEL");
    assert(kbdtoe_loglevel_get() == KBDTOE_LOG_DEFAULT);
}

static void test_time_monotonic(void)
{
    uint64_t t1 = kbdtoe_get_current_time_millis();
    usleep(1000); /* 1ms */
    uint64_t t2 = kbdtoe_get_current_time_millis();
    assert(t2 >= t1);
}

static void test_log_apis_no_crash(void)
{
    kbdtoe_log_init();
    kbdtoe_loglevel_set(KBDTOE_LOG_DEBUG);
    kbdtoe_log(__func__, __LINE__, KBDTOE_LOG_INFO, "unit test info %d", 1);
    kbdtoe_log(__func__, __LINE__, KBDTOE_LOG_INFO, "unit test normal");
    kbdtoe_log(__func__, __LINE__, KBDTOE_LOG_INFO, "unit test hook %s", "ok");
    kbdtoe_log_uninit();
}

int main(void)
{
    test_level_set_get();
    test_level_set_by_str();
    test_time_monotonic();
    test_log_apis_no_crash();
    return 0;
}
