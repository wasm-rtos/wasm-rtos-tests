#include <wasm-rtos.h>

/* (module (func (export "app_main") (result i32) i32.const 42)) */
static const unsigned char quick_child[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0c, 0x01, 0x08, 'a', 'p', 'p', '_', 'm', 'a', 'i', 'n', 0x00, 0x00,
    0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x2a, 0x0b
};

/*
 * Fills the handle table with private event groups, then returns with all of
 * them open: the kernel closes them. Returns 0 or the failing step.
 */
int32_t app_main_limit(void)
{
    int32_t handles[32];
    int32_t extra;
    uint32_t index;

    for (index = 0U; index < 32U; ++index)
    {
        handles[index] = wr_event_create(0, 0U);
        if (handles[index] != (int32_t)index + 1)
            return 1;
    }
    if (wr_event_create(0, 0U) != -WR_ELIMIT)
        return 2;
    if (wr_close(handles[5]) != 0)
        return 3;
    extra = wr_event_create(0, 0U);
    if (extra != handles[5])
        return 4;
    return 0;
}

/* Handle 1 means nothing to a program that has not opened anything. */
int32_t app_main_foreign(void)
{
    uint32_t item = 0U;
    if (wr_queue_send(1, &item, 0U) != -WR_EBADH)
        return 1;
    if (wr_event_get(1) != -WR_EBADH)
        return 2;
    return 0;
}

/* Receives three items from the shared queue "pipe" and returns their sum. */
int32_t app_main_consumer(void)
{
    int32_t queue = wr_queue_open("pipe", sizeof(uint32_t), 4U);
    uint32_t sum = 0U;
    uint32_t index;

    if (queue < 0)
        return queue;
    for (index = 0U; index < 3U; ++index)
    {
        uint32_t item = 0U;
        int32_t status = wr_queue_receive(queue, &item, WR_WAIT_FOREVER);
        if (status != 0)
            return status;
        sum += item;
    }
    return (int32_t)sum;
}

/* Sends 1, 2 and 3 to "pipe"; the queue must already exist with these limits. */
int32_t app_main_producer(void)
{
    int32_t queue;
    uint32_t value;

    if (wr_queue_open("pipe", 8U, 4U) != -WR_EMISMATCH)
        return -100;
    queue = wr_queue_open("pipe", sizeof(uint32_t), 4U);
    if (queue < 0)
        return queue;
    for (value = 1U; value <= 3U; ++value)
    {
        int32_t status = wr_queue_send(queue, &value, 0U);
        if (status != 0)
            return status;
    }
    return 0;
}

/* Arguments the kernel refuses. Returns 0 or the failing step. */
int32_t app_main_bad_arguments(void)
{
    static const char long_name[] = "a-name-that-is-longer-than-32-bytes";
    static const char bad_utf8[] = { 'q', (char)0xff };
    int32_t queue = wr_queue_create(0, 0U, 4U, 1U);
    int32_t mutex = wr_mutex_create(0, 0U);

    if (queue <= 0 || mutex <= 0)
        return 1;
    if (wr_queue_create(long_name, wr_strlen(long_name), 4U, 1U) != -WR_ETOOLONG)
        return 2;
    if (wr_queue_create(bad_utf8, sizeof(bad_utf8), 4U, 1U) != -WR_EINVAL)
        return 3;
    if (wr_event_create((const char*)0xFFFFFFF0U, 32U) != -WR_EFAULT)
        return 4;
    /* A handle of another kind, and handles that were never opened. */
    if (wr_mutex_lock(queue, 0U) != -WR_EBADH || wr_semaphore_give(mutex) != -WR_EBADH)
        return 5;
    if (wr_close(0) != -WR_EBADH || wr_close(-1) != -WR_EBADH || wr_close(99) != -WR_EBADH)
        return 6;
    /* wait.any needs at least one waitable handle. */
    if (wr_wait_any(&queue, 0U, 0U) != -WR_EINVAL)
        return 7;
    if (wr_wait_any(&mutex, 1U, 0U) != -WR_EBADH)
        return 8;
    if (wr_wait_any((const int32_t*)0xFFFFFFF0U, 8U, 0U) != -WR_EFAULT)
        return 9;
    if (wr_timer_create(0U, 0U) != -WR_EINVAL)
        return 10;
    return 0;
}

/* wait.any over a queue, a timer and a child. Returns 0 or the failing step. */
int32_t app_main_wait_any(void)
{
    int32_t handles[3];
    uint32_t item = 5U;

    handles[0] = wr_queue_create(0, 0U, sizeof(item), 1U);
    handles[1] = wr_timer_create(10U, 0U);
    if (handles[0] <= 0 || handles[1] <= 0)
        return 1;
    if (wr_wait_any(handles, 2U, 0U) != -WR_ETIMEOUT)
        return 2;
    if (wr_timer_start(handles[1]) != 0)
        return 3;
    /* Blocks until the timer expires. */
    if (wr_wait_any(handles, 2U, WR_WAIT_FOREVER) != 1 || wr_timer_read(handles[1]) != 1)
        return 4;
    if (wr_queue_send(handles[0], &item, 0U) != 0)
        return 5;
    /* Waiting consumes nothing: the item is still there afterwards. */
    if (wr_wait_any(handles, 2U, 0U) != 0 || wr_wait_any(handles, 2U, 0U) != 0)
        return 6;
    item = 0U;
    if (wr_queue_receive(handles[0], &item, 0U) != 0 || item != 5U)
        return 7;
    handles[2] = wr_spawn(quick_child, sizeof(quick_child), 0, 0U, 0U, 0U);
    if (handles[2] <= 0)
        return 8;
    if (wr_wait_any(handles, 3U, 100U) != 2 || wr_wait(handles[2], 0U) != 42)
        return 9;
    if (wr_wait_any(handles, 2U, 20U) != -WR_ETIMEOUT)
        return 10;
    return 0;
}
