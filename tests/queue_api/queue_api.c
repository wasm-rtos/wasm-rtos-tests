#include <stdint.h>

#define OS_STATUS_OK 0
#define OS_STATUS_INVALID_ARGUMENT 2
__attribute__((import_module("env"), import_name("os_queue_create"))) extern int32_t
os_queue_create(uint32_t item_size, uint32_t item_count);

__attribute__((import_module("env"), import_name("os_queue_send"))) extern int32_t os_queue_send(int32_t queue,
                                                                                                 const void* item);

__attribute__((import_module("env"), import_name("os_queue_receive"))) extern int32_t os_queue_receive(int32_t queue,
                                                                                                       void* item);

__attribute__((import_module("env"), import_name("os_queue_delete"))) extern int32_t os_queue_delete(int32_t queue);

static int items_match(const uint32_t* actual, const uint32_t* expected, uint32_t count)
{
    uint32_t index = 0U;

    for (index = 0U; index < count; ++index)
    {
        if (actual[index] != expected[index])
        {
            return 0;
        }
    }

    return 1;
}

uint32_t app_main(void)
{
    const uint32_t first[2] = {0x12345678U, 0xABCDEF01U};
    const uint32_t second[2] = {0x11111111U, 0x22222222U};
    const uint32_t third[2] = {0x33333333U, 0x44444444U};
    uint32_t received[2] = {0U, 0U};
    int32_t queue = 0;
    int32_t invalid_queue = 0;
    int32_t status = OS_STATUS_OK;

    queue = os_queue_create((uint32_t)sizeof(first), 2U);
    if (queue <= 0)
    {
        return 1U;
    }

    invalid_queue = os_queue_create((uint32_t)sizeof(first), 0U);
    if (invalid_queue > 0)
    {
        (void)os_queue_delete(invalid_queue);
        (void)os_queue_delete(queue);
        return 2U;
    }

    invalid_queue = os_queue_create(0U, 2U);
    if (invalid_queue > 0)
    {
        (void)os_queue_delete(invalid_queue);
        (void)os_queue_delete(queue);
        return 3U;
    }

    status = os_queue_send(queue, first);
    if (status != OS_STATUS_OK)
    {
        (void)os_queue_delete(queue);
        return 4U;
    }

    status = os_queue_receive(queue, received);
    if (status != OS_STATUS_OK || !items_match(received, first, 2U))
    {
        (void)os_queue_delete(queue);
        return 5U;
    }

    status = os_queue_send(queue, second);
    if (status != OS_STATUS_OK)
    {
        (void)os_queue_delete(queue);
        return 6U;
    }

    status = os_queue_send(queue, third);
    if (status != OS_STATUS_OK)
    {
        (void)os_queue_delete(queue);
        return 7U;
    }

    status = os_queue_send(queue, first);
    if (status == OS_STATUS_OK)
    {
        (void)os_queue_delete(queue);
        return 8U;
    }

    received[0] = 0U;
    received[1] = 0U;
    status = os_queue_receive(queue, received);
    if (status != OS_STATUS_OK || !items_match(received, second, 2U))
    {
        (void)os_queue_delete(queue);
        return 9U;
    }

    received[0] = 0U;
    received[1] = 0U;
    status = os_queue_receive(queue, received);
    if (status != OS_STATUS_OK || !items_match(received, third, 2U))
    {
        (void)os_queue_delete(queue);
        return 10U;
    }

    status = os_queue_receive(queue, received);
    if (status == OS_STATUS_OK)
    {
        (void)os_queue_delete(queue);
        return 11U;
    }

    status = os_queue_delete(queue);
    if (status != OS_STATUS_OK)
    {
        return 12U;
    }

    status = os_queue_send(queue, first);
    if (status == OS_STATUS_OK)
    {
        return 13U;
    }

    (void)OS_STATUS_INVALID_ARGUMENT;
    return 0U;
}
