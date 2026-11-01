#include <wasm-rtos.h>

/* Every entry opens the host's queue named "q" and returns the ABI result. */
static int32_t open_queue(void)
{
    return wr_queue_open("q", sizeof(uint32_t), 1U);
}

int32_t app_main_send(uint32_t value)
{
    int32_t queue = open_queue();
    return queue < 0 ? queue : wr_queue_send(queue, &value, WR_WAIT_FOREVER);
}

/* The received value, or a negative error. */
int32_t app_main_receive(void)
{
    uint32_t value = 0U;
    int32_t queue = open_queue();
    int32_t status = queue < 0 ? queue : wr_queue_receive(queue, &value, WR_WAIT_FOREVER);
    return status == 0 ? (int32_t)value : status;
}

int32_t app_main_send_timeout(uint32_t value)
{
    int32_t queue = open_queue();
    return queue < 0 ? queue : wr_queue_send(queue, &value, 5U);
}

int32_t app_main_receive_timeout(void)
{
    uint32_t value = 0xfeedbeefU;
    int32_t queue = open_queue();
    return queue < 0 ? queue : wr_queue_receive(queue, &value, 5U);
}
