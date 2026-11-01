#include <stdint.h>

/*
 * Linked with 1-byte pages and a 4 KiB C stack (see run-tests.sh), so memory
 * is counted and grown in bytes. No libc: what is tested is the runtime, not
 * the guest's allocator. Returns 0 when every check passes.
 */
uint32_t app_main(void)
{
    uint32_t start = (uint32_t)__builtin_wasm_memory_size(0);
    volatile uint8_t* fresh = (volatile uint8_t*)(uintptr_t)start;
    volatile uint8_t* far = 0;
    uint32_t i;

    if (start > 16384U)
        return 1; /* a few KiB, not whole 64 KiB pages */
    if ((uint32_t)__builtin_wasm_memory_grow(0, 100) != start)
        return 2;
    if ((uint32_t)__builtin_wasm_memory_size(0) != start + 100U)
        return 3; /* grew by 100 bytes */
    for (i = 0U; i < 100U; ++i)
        if (fresh[i] != 0U)
            return 4; /* new memory is zeroed */
    fresh[99] = 7U;

    /* Past 64 KiB of memory: the old limit counted 64 KiB-sized pages. */
    if ((uint32_t)__builtin_wasm_memory_grow(0, 70000) != start + 100U)
        return 5;
    far = (volatile uint8_t*)(uintptr_t)(start + 100U + 69999U);
    if (*far != 0U)
        return 6;
    *far = 9U;
    if (*far != 9U || fresh[99] != 7U)
        return 7; /* old bytes survive the reallocation */
    return 0;
}
