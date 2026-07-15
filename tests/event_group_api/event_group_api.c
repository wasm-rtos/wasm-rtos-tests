#include <stdint.h>

#define OS_WAIT_FOREVER UINT32_MAX

__attribute__((import_module("env"), import_name("os_event_group_wait_bits"))) extern uint32_t
os_event_group_wait_bits(
    uint32_t event_group_id,
    uint32_t bits_to_wait_for,
    uint32_t clear_on_exit,
    uint32_t wait_for_all,
    uint32_t timeout_ms
);

uint32_t app_main_wait_all(uint32_t event_group_id)
{
    return os_event_group_wait_bits(
        event_group_id,
        0x3U,
        1U,
        1U,
        OS_WAIT_FOREVER
    );
}

uint32_t app_main_timeout(uint32_t event_group_id)
{
    return os_event_group_wait_bits(
        event_group_id,
        0x4U,
        0U,
        0U,
        5U
    );
}
