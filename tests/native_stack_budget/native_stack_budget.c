#include <stdint.h>

__attribute__((import_module("env"), import_name("host_probe"))) void host_probe(void);

/* Every level reports in, so the host sees the deepest native stack. */
__attribute__((noinline)) static uint32_t recurse(uint32_t depth)
{
    host_probe();
    return depth == 0U ? 0U : recurse(depth - 1U) + 1U;
}

uint32_t app_main(uint32_t depth)
{
    return recurse(depth);
}
