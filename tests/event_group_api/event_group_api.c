#include <wasm-rtos.h>

/* Each entry waits on the host's event group "e" and returns the ABI result. */
static int32_t open_event(void)
{
    return wr_event_open("e");
}

int32_t app_main_wait_all(void)
{
    int32_t event = open_event();
    return event < 0 ? event : wr_event_wait(event, 0x3U, WR_EVENT_ALL | WR_EVENT_CLEAR, WR_WAIT_FOREVER);
}

int32_t app_main_timeout(void)
{
    int32_t event = open_event();
    return event < 0 ? event : wr_event_wait(event, 0x4U, 0U, 5U);
}

/* Sets, reads and clears bits itself; returns 0 or the failing step. */
int32_t app_main_bits(void)
{
    int32_t event = open_event();
    if (event < 0)
        return 1;
    if (wr_event_set(event, 0x10U) != 0x10)
        return 2;
    if (wr_event_get(event) != 0x10)
        return 3;
    if (wr_event_wait(event, 0x30U, 0U, 0U) != 0x10)
        return 4;
    if (wr_event_wait(event, 0x30U, WR_EVENT_ALL, 0U) != -WR_ETIMEOUT)
        return 5;
    if (wr_event_clear(event, 0x10U) != 0x10)
        return 6;
    if (wr_event_set(event, 0x01000000U) != -WR_EINVAL)
        return 7;
    if (wr_event_wait(event, 0U, 0U, 0U) != -WR_EINVAL)
        return 8;
    return 0;
}
