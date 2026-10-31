#include <stdio.h>
#include <string.h>

/* Exit code 0 only when argv arrives exactly as the host passed it. */
int main(int argc, char** argv)
{
    printf("argc=%d\n", argc);
    for (int i = 0; i < argc; ++i)
        printf("argv[%d]=%s\n", i, argv[i]);

    if (argc != 3)
        return 10 + argc;
    if (strcmp(argv[0], "/bin/proc_spawn_argv") != 0)
        return 20;
    if (strcmp(argv[1], "--period") != 0)
        return 21;
    if (strcmp(argv[2], "250") != 0)
        return 22;
    return 0;
}
