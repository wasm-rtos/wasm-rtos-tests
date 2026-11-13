#include <wasm-rtos.h>

/* 192 KB: far more than the 32 KB of RAM the test gives programs, so its frames live in the stores. */
static uint32_t g_words[49152];

/* Each tick it writes all of its memory again; the sum is right only if every frame came back whole. */
int app_main(void)
{
    int32_t timer = wr_timer_create(1u, WR_TIMER_PERIODIC);
    uint32_t sum = 0;

    wr_timer_start(timer);
    for (uint32_t tick = 0; tick < 12u; tick++)
    {
        wr_wait_any(&timer, 1u, WR_WAIT_FOREVER);
        wr_timer_read(timer);
        for (uint32_t i = 0; i < 49152u; i += 7u)
            g_words[i] += i ^ tick;
    }
    for (uint32_t i = 0; i < 49152u; i += 7u)
        sum += g_words[i];
    return (int)(sum & 0x7fffffff);
}
