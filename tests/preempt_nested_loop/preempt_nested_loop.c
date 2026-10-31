/* Nested loops where the inner loop jumps straight to the next outer
 * iteration (a `continue` of the outer loop from inside the inner one). */
static volatile unsigned sink;
unsigned app_main(void)
{
    unsigned checksum = 0;
    for (unsigned i = 0; i < 300000; ++i)
    {
        for (unsigned j = 0;; ++j)
        {
            checksum = checksum * 31u + i + j;
            if (j == (i % 5))
                goto next_outer;
            if (j > 10)
                break;
        }
        checksum ^= 0x5a5a5a5au;
    next_outer:
        sink = checksum;
    }
    return checksum;
}
