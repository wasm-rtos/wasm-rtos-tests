#include <stdio.h>

#ifndef WASI_TASK_NAME
#define WASI_TASK_NAME "wasi_task"
#endif

int main(void)
{
    volatile unsigned int spin = 0U;

    for (;;)
    {
        puts(WASI_TASK_NAME);

        for (spin = 0U; spin < 1U; ++spin)
        {
        }
    }
}
