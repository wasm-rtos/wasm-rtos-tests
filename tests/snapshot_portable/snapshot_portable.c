#include <stdint.h>

/*
 * A program that keeps state everywhere a snapshot has to carry: deep
 * recursion without loops, nested loops, calls through a table, floating
 * point, 64-bit values, globals and a mostly empty block of memory.
 */

static uint8_t field[256 * 1024];
static uint64_t mixed = 0x9e3779b97f4a7c15ULL;
static double drift = 0.5;

__attribute__((noinline)) static uint32_t fib(uint32_t n)
{
    return n < 2U ? n : fib(n - 1U) + fib(n - 2U);
}

__attribute__((noinline)) static uint64_t step_mul(uint64_t value, uint32_t i)
{
    return value * 6364136223846793005ULL + i;
}

__attribute__((noinline)) static uint64_t step_xor(uint64_t value, uint32_t i)
{
    return (value ^ (value >> 29)) + (uint64_t)i * 0x2545f4914f6cdd1dULL;
}

typedef uint64_t (*Step)(uint64_t, uint32_t);
static Step volatile steps[2] = { step_mul, step_xor };

__attribute__((noinline)) static double wobble(double value, uint32_t i)
{
    return value * 0.999 + (double)(i % 7U) * 0.125;
}

uint32_t app_main(void)
{
    uint32_t i;
    uint32_t j;
    uint64_t sum = 0U;

    for (i = 0U; i < 64U; ++i)
    {
        for (j = 0U; j < 200U; ++j)
        {
            mixed = steps[(i + j) & 1U](mixed, j);
            drift = wobble(drift, j);
        }
        /* A few bytes far apart: the rest of the field stays zero. */
        field[(i * 4099U) % sizeof(field)] = (uint8_t)mixed;
        sum += fib(12U + (i % 6U));
    }

    for (i = 0U; i < sizeof(field); i += 4099U)
        sum = sum * 31U + field[i];

    return (uint32_t)(sum ^ (sum >> 32) ^ mixed ^ (mixed >> 32)) ^ (uint32_t)(int32_t)(drift * 1000.0);
}
