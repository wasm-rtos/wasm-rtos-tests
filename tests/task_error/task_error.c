#include <stdint.h>

__attribute__((noinline)) static void parse_header(volatile int depth)
{
    if (depth >= 0)
        __builtin_trap();
}

__attribute__((noinline)) static void load_file(int depth)
{
    parse_header(depth + 1);
}

/* Fails two calls deep. */
int32_t app_main(void)
{
    load_file(0);
    return 0;
}

int32_t app_main_ok(void)
{
    return 7;
}
