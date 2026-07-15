#include <stdint.h>

__attribute__((import_module("env"), import_name("os_semaphore_take"))) extern uint32_t
os_semaphore_take(uint32_t semaphore_id, uint32_t timeout_ms);

uint32_t app_main_wait(uint32_t semaphore_id)
{
    return os_semaphore_take(semaphore_id, 50U);
}

uint32_t app_main_timeout(uint32_t semaphore_id)
{
    return os_semaphore_take(semaphore_id, 5U);
}

uint32_t app_main_immediate(uint32_t semaphore_id)
{
    return os_semaphore_take(semaphore_id, 0U);
}
