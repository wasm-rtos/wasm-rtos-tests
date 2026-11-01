#include <wasm-rtos.h>

/* Each entry takes the host's semaphore "s" and returns the ABI result. */
static int32_t take(uint32_t timeout_ms)
{
    int32_t semaphore = wr_semaphore_open("s", 1U, 0U);
    return semaphore < 0 ? semaphore : wr_semaphore_take(semaphore, timeout_ms);
}

int32_t app_main_wait(void)
{
    return take(50U);
}

int32_t app_main_timeout(void)
{
    return take(5U);
}

int32_t app_main_immediate(void)
{
    return take(0U);
}

/* A second open with other limits is refused. */
int32_t app_main_mismatch(void)
{
    return wr_semaphore_open("s", 2U, 0U);
}
