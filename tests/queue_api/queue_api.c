#include <wasm-rtos.h>

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
    int32_t queue = wr_queue_create(0, 0U, (uint32_t)sizeof(first), 2U);

    if (queue <= 0)
        return 1U;
    if (wr_queue_create(0, 0U, (uint32_t)sizeof(first), 0U) != -WR_EINVAL)
        return 2U;
    if (wr_queue_create(0, 0U, 0U, 2U) != -WR_EINVAL)
        return 3U;
    if (wr_queue_send(queue, first, 0U) != 0)
        return 4U;
    if (wr_queue_receive(queue, received, 0U) != 0 || !items_match(received, first, 2U))
        return 5U;
    if (wr_queue_send(queue, second, 0U) != 0)
        return 6U;
    if (wr_queue_send(queue, third, 0U) != 0)
        return 7U;
    if (wr_queue_send(queue, first, 0U) != -WR_EFULL)
        return 8U;

    received[0] = 0U;
    received[1] = 0U;
    if (wr_queue_receive(queue, received, 0U) != 0 || !items_match(received, second, 2U))
        return 9U;
    received[0] = 0U;
    received[1] = 0U;
    if (wr_queue_receive(queue, received, 0U) != 0 || !items_match(received, third, 2U))
        return 10U;
    if (wr_queue_receive(queue, received, 0U) != -WR_EEMPTY)
        return 11U;
    if (wr_queue_receive(queue, (void*)0xFFFFFFF0U, 0U) != -WR_EFAULT)
        return 12U;
    if (wr_close(queue) != 0)
        return 13U;
    if (wr_queue_send(queue, first, 0U) != -WR_EBADH)
        return 14U;
    if (wr_close(queue) != -WR_EBADH)
        return 15U;

    return 0U;
}
