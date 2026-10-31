static volatile unsigned sink;
static unsigned table[256];

/* Grows memory by one page and works on the new page. */
__attribute__((noinline)) static unsigned grow_and_work(unsigned round)
{
    unsigned total = 0;
    int previous = __builtin_wasm_memory_grow(0, 1);
    if (previous < 0)
        return 0;
    volatile unsigned* page = (volatile unsigned*)((unsigned long)previous * 65536u);
    for (unsigned i = 0; i < 300; ++i)
    {
        page[i] = round + i;
        total += page[i];
    }
    return total;
}

/*
 * The caller keeps writing linear memory after each call. When a task is
 * preempted inside grow_and_work, the caller is resumed afterwards and
 * must use the memory as grown (and possibly moved), not the old block.
 */
unsigned app_main(void)
{
    unsigned checksum = 0;
    for (unsigned round = 0; round < 12; ++round)
    {
        unsigned value = grow_and_work(round);
        for (unsigned i = 0; i < 256; ++i)
            table[i] = table[i] * 3u + value;
        checksum += table[round];
    }
    sink = checksum;
    return checksum;
}
