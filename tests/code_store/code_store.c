#include <wasm-rtos.h>

/* Some work to predecode, then a long sleep: the scheduler has nothing ready meanwhile. */
static unsigned int mix(unsigned int x)
{
    for (int i = 0; i < 1000; ++i)
        x = x * 1103515245U + 12345U + (x >> 7);
    return x;
}

void app_main(void)
{
    volatile unsigned int r = mix(1U);
    (void)r;
    wr_sleep_ms(100000U);
}
