#include <stdint.h>

/* Never returns; the host counts its scheduler slices. */
void app_main_spin(void)
{
    volatile uint32_t counter = 0U;
    for (;;)
        ++counter;
}

/* Finishes in its first slice. */
uint32_t app_main_done(void)
{
    return 1U;
}
