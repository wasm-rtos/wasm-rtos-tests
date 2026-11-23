/* A loop that takes many slices, with calls, as a background job. */
static unsigned mix(unsigned x)
{
    x ^= x << 13;
    x ^= x >> 17;
    return x ^ (x << 5);
}

int app_main(void)
{
    unsigned s = 0u;
    for (unsigned i = 0u; i < 20000u; ++i)
        s += mix(i + 1u);
    return s == 0u ? 1 : 0;
}
