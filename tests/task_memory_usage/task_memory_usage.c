#include <stdint.h>

__attribute__((import_module("env"), import_name("os_delay_ms"))) void os_delay_ms(uint32_t delay_ms);

/* Smallest possible program; also the entry point os_task_spawn detects. */
uint32_t app_main(void)
{
    return 1U;
}

/* Grows linear memory by one page, then stays alive so the host can look. */
uint32_t app_main_grow(void)
{
    uint32_t previous_pages = (uint32_t)__builtin_wasm_memory_grow(0, 1);
    os_delay_ms(1000U);
    return previous_pages;
}
