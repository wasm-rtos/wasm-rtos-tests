#include <stdint.h>

#define OS_STATUS_OK 0U
#define OS_WAIT_FOREVER UINT32_MAX

__attribute__((import_module("env"), import_name("os_queue_send_wait"))) extern uint32_t
os_queue_send_wait(uint32_t queue_id, const uint32_t* item, uint32_t timeout_ms);

__attribute__((import_module("env"), import_name("os_queue_receive_wait"))) extern uint32_t
os_queue_receive_wait(uint32_t queue_id, uint32_t* item, uint32_t timeout_ms);

uint32_t app_main_send(uint32_t queue_id, uint32_t value)
{
    return os_queue_send_wait(queue_id, &value, OS_WAIT_FOREVER);
}

uint32_t app_main_receive(uint32_t queue_id)
{
    uint32_t value = 0U;
    uint32_t status = os_queue_receive_wait(
        queue_id,
        &value,
        OS_WAIT_FOREVER
    );

    return status == OS_STATUS_OK ? value : 0x80000000U | status;
}

uint32_t app_main_send_timeout(uint32_t queue_id, uint32_t value)
{
    return os_queue_send_wait(queue_id, &value, 5U);
}

uint32_t app_main_receive_timeout(uint32_t queue_id)
{
    uint32_t value = 0xfeedbeefU;
    return os_queue_receive_wait(queue_id, &value, 5U);
}
