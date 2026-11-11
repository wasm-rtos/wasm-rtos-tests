#include <wasm-rtos.h>

/* Programs with more memory than the host lets stay in RAM: the page store holds the rest. */
#define PAGE 65536U

static uint32_t mix(uint32_t i)
{
    return (i * 2654435761U) ^ (i >> 7);
}

/* Grows by pages, writes a word every 256 bytes of the new memory, then reads them back as a sum. */
uint32_t app_main_big(uint32_t pages)
{
    uint32_t base = (uint32_t)__builtin_wasm_memory_grow(0, pages);
    volatile uint32_t* words;
    uint32_t count = pages * (PAGE / 4U);
    uint32_t sum = 0U;
    uint32_t i;

    if (base == 0xFFFFFFFFU)
        return 0U;
    words = (volatile uint32_t*)(uintptr_t)(base * PAGE);
    for (i = 0U; i < count; i += 64U)
        words[i] = mix(i);
    /* Backwards, so the frames read first are the ones stored longest ago. */
    for (i = count; i >= 64U; i -= 64U)
        sum += words[i - 64U] ^ (i - 64U);
    return sum;
}

/* Sends a queue item from the far end of new memory, a frame that went to the store meanwhile. */
uint32_t app_main_send(uint32_t pages)
{
    uint32_t base = (uint32_t)__builtin_wasm_memory_grow(0, pages);
    volatile uint8_t* item;
    volatile uint8_t* first;
    int32_t queue = wr_queue_open("q", 16U, 1U);
    uint32_t i;

    if (base == 0xFFFFFFFFU || queue < 0)
        return 1U;
    item = (volatile uint8_t*)(uintptr_t)((base + pages) * PAGE - 16U);
    for (i = 0U; i < 16U; ++i)
        item[i] = (uint8_t)("from a far frame"[i]);
    /* Touch the start of the memory so the item's frame goes to the store. */
    first = (volatile uint8_t*)(uintptr_t)(base * PAGE);
    for (i = 0U; i < pages * PAGE - 16U; i += 4096U)
        first[i] = 1U;
    return wr_queue_send(queue, (const void*)item, 0U) == 0 ? 0U : 2U;
}

/* Grows a page at a time until memory.grow says no: the pages it got. */
uint32_t app_main_grow_all(void)
{
    uint32_t pages = 0U;
    while (__builtin_wasm_memory_grow(0, 1) != (__SIZE_TYPE__)-1)
        ++pages;
    return pages;
}
