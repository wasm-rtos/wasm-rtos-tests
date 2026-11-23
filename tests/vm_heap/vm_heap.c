/* Several functions, called many times, so each gets its records. */
static unsigned fib(unsigned n) { return n < 2u ? n : fib(n - 1u) + fib(n - 2u); }

static unsigned mix(unsigned x)
{
    x ^= x << 13;
    x ^= x >> 17;
    return x ^ (x << 5);
}

static unsigned sum(unsigned n)
{
    unsigned s = 0u;
    for (unsigned i = 0u; i < n; ++i)
        s += mix(i + 1u);
    return s;
}

int app_main(void)
{
    unsigned r = fib(18u) + sum(2000u);
    return r == 0u ? 1 : 0;
}
