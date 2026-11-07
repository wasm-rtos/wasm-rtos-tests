#include <wasm-rtos.h>

int32_t app_main_try(void)
{
    return wr_counter_open_cstr("enc0", WR_COUNTER_X4, 0);
}

/* Opening, properties, setting the count, and the errors. */
int32_t app_main_basic(void)
{
    int32_t count = 1;
    int32_t encoder = wr_counter_open_cstr("enc0", WR_COUNTER_X4, 1300);

    if (encoder <= 0)
        return 1;
    if (wr_counter_get(encoder, WR_COUNTER_KIND) != WR_COUNTER_QUADRATURE ||
        wr_counter_get(encoder, WR_COUNTER_MAX_HZ) != 40000000 || wr_counter_get(encoder, WR_COUNTER_FILTER_NS) != 1250 ||
        wr_counter_get(encoder, 9u) != -WR_EUNSUPPORTED)
        return 2;
    if (wr_counter_read(encoder, &count) != 0 || count != 0)
        return 3;
    if (wr_counter_open_cstr("enc0", WR_COUNTER_X4, 0) != -WR_EINUSE ||
        wr_counter_open_cstr("flow", WR_COUNTER_X4, 0) != -WR_EINVAL ||
        wr_counter_open_cstr("enc1", WR_COUNTER_X1, 0) != -WR_EINVAL ||
        wr_counter_open_cstr("nope", WR_COUNTER_RISING, 0) != -WR_ENOTFOUND ||
        wr_counter_open_cstr("flow", 0u, 0) != -WR_EINVAL || wr_counter_open_cstr("flow", 7u, 0) != -WR_EINVAL)
        return 4;
    /* Homing; counts are signed. */
    if (wr_counter_set(encoder, -500) != 0 || wr_counter_read(encoder, &count) != 0 || count != -500)
        return 5;
    if (wr_counter_read(encoder, (int32_t*)0xFFFFFFFEu) != -WR_EFAULT || wr_counter_read(-3, &count) != -WR_EBADH)
        return 6;
    if (wr_counter_watch(encoder, 10, 5) != -WR_EINVAL)
        return 7;
    /* Already outside the window: ready at once. */
    if (wr_counter_watch(encoder, 0, 100) != 0 || wr_wait_any(&encoder, 1u, 0u) != 0)
        return 8;
    if (wr_counter_watch(encoder, INT32_MIN, INT32_MAX) != 0 || wr_wait_any(&encoder, 1u, 0u) != -WR_ETIMEOUT)
        return 9;
    return 0;
}

/* Wait for the knob to move, then return where it is. */
int32_t app_main_wait(void)
{
    int32_t count = 0;
    int32_t encoder = wr_counter_open_cstr("enc0", WR_COUNTER_X4, 0);

    if (encoder <= 0 || wr_counter_read(encoder, &count) != 0)
        return -1;
    if (wr_counter_watch(encoder, count, count) != 0)
        return -2;
    if (wr_wait_any(&encoder, 1u, WR_WAIT_FOREVER) != 0)
        return -3;
    if (wr_counter_read(encoder, &count) != 0)
        return -4;
    return count;
}

int32_t app_main_wait_timeout(void)
{
    int32_t encoder = wr_counter_open_cstr("enc0", WR_COUNTER_X4, 0);

    if (encoder <= 0 || wr_counter_watch(encoder, 0, 0) != 0)
        return -1;
    return wr_wait_any(&encoder, 1u, 50u);
}

/* A flow meter: sleep until more than 99 pulses came, then return the count. */
int32_t app_main_flow(void)
{
    int32_t count = 0;
    int32_t flow = wr_counter_open_cstr("flow", WR_COUNTER_RISING, 0);

    if (flow <= 0 || wr_counter_watch(flow, INT32_MIN, 99) != 0)
        return -1;
    if (wr_wait_any(&flow, 1u, WR_WAIT_FOREVER) != 0 || wr_counter_read(flow, &count) != 0)
        return -2;
    return count;
}
