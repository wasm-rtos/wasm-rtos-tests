#include <wasm-rtos.h>

/* Memory the program fills: its own, whatever other processes of it do. */
static uint32_t g_words[4096];

/* Ticks a timer, writing its memory between ticks, and returns a sum only a whole run gives. */
int app_main(void)
{
    int32_t timer = wr_timer_create(1u, WR_TIMER_PERIODIC);
    uint32_t sum = 0;

    wr_timer_start(timer);
    for (uint32_t tick = 0; tick < 20u; tick++)
    {
        wr_wait_any(&timer, 1u, WR_WAIT_FOREVER);
        wr_timer_read(timer);
        for (uint32_t i = 0; i < 4096u; i++)
            g_words[i] += i ^ tick;
    }
    for (uint32_t i = 0; i < 4096u; i++)
        sum += g_words[i];
    return (int)(sum & 0x7fffffff);
}
