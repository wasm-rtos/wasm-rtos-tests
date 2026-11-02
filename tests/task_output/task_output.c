#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int result;

    printf("out one\n");
    fflush(stdout);
    fprintf(stderr, "err one\n");
    sched_yield();
    printf("out two\n");
    fflush(stdout);

    /* Only stdout and stderr are writable. */
    result = (int)write(7, "x", 1);
    printf("fd 7: %d %s\n", result, errno == EBADF ? "EBADF" : "other");
    fflush(stdout);
    return 0;
}
