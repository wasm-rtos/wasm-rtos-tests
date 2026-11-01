#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Linked with 1-byte pages and a 4 KiB C stack (see run-tests.sh), so memory
 * is counted and grown in bytes. Returns 0 when every check passes.
 */
int main(void)
{
    enum { BLOCKS = 300 };
    static unsigned char* blocks[BLOCKS];
    uint32_t start = (uint32_t)__builtin_wasm_memory_size(0);
    volatile uint8_t* fresh = (volatile uint8_t*)(uintptr_t)start;
    int i;
    int j;

    if (start > 16384U)
        return 1; /* a few KiB, not whole 64 KiB pages */
    if ((uint32_t)__builtin_wasm_memory_grow(0, 100) != start)
        return 2;
    if ((uint32_t)__builtin_wasm_memory_size(0) != start + 100U)
        return 3; /* grew by 100 bytes */
    for (i = 0; i < 100; ++i)
        if (fresh[i] != 0U)
            return 4; /* new memory is zeroed */
    fresh[99] = 7U;
    if (fresh[99] != 7U)
        return 5;

    /* malloc grows memory in small steps; every block must survive. */
    for (i = 0; i < BLOCKS; ++i)
    {
        blocks[i] = malloc((size_t)(32 + i * 3));
        if (!blocks[i])
            return 6;
        memset(blocks[i], i & 0xFF, (size_t)(32 + i * 3));
    }
    for (i = 0; i < BLOCKS; ++i)
        for (j = 0; j < 32 + i * 3; ++j)
            if (blocks[i][j] != (unsigned char)(i & 0xFF))
                return 7;
    return 0;
}
