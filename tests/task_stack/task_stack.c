#include <wasm-rtos.h>

/*
 * The program's own settings: an unknown key 9 (skipped), a 4 KiB
 * interpreter stack, its ABI version and its name.
 */
__asm__(".section \".custom_section.wasm-rtos.app\",\"\",@\n"
        ".int8 9\n.uleb128 4\n.int32 123\n"
        ".text\n");
WR_APP_STACK_SIZE(4096);
WR_APP_ABI_VERSION();
WR_APP_NAME("Stack test");

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
