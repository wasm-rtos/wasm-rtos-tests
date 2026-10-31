#include <errno.h>
#include <stdio.h>

/*
 * Nothing is mounted. The runner's own main.c sits in the host working
 * directory; a program must not be able to reach it.
 */
int main(void)
{
    const char* paths[] = { "main.c", "/main.c", "../main.c", "./fs_sandbox.c" };

    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i)
    {
        FILE* file = NULL;
        errno = 0;
        file = fopen(paths[i], "r");
        if (file != NULL)
        {
            printf("opened host file %s\n", paths[i]);
            return 1 + (int)i;
        }
        if (errno != ENOENT)
            return 10 + (int)i;
    }
    printf("host files stay out of reach\n");
    return 0;
}
