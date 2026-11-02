#include <sched.h>
#include <stdio.h>

int main(void)
{
    volatile unsigned sum = 0;
    unsigned index;

    for (index = 0; index < 50000U; ++index)
    {
        sum += index;
    }
    /* Each yield ends a slice that spent only a little of its fuel. */
    for (index = 0; index < 100U; ++index)
    {
        sched_yield();
    }
    return (int)(sum & 1U);
}
