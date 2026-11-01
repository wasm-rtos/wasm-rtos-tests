#include <stdint.h>

/*
 * The program's own settings: an unknown key 9 (ignored) and key 1, a
 * 4 KiB interpreter stack.
 */
__asm__(".section \".custom_section.wasm-rtos.app\",\"\",@\n"
        ".int8 9\n.int32 123\n"
        ".int8 1\n.int32 4096\n"
        ".text\n");

uint32_t app_main(void)
{
    return 1U;
}

__attribute__((noinline)) static uint32_t recurse(uint32_t depth)
{
    return depth == 0U ? 0U : recurse(depth - 1U) + 1U;
}

/* Uses interpreter stack in proportion to depth. */
uint32_t app_main_recurse(uint32_t depth)
{
    return recurse(depth);
}
