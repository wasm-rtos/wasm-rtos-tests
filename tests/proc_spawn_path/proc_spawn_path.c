#include <stdlib.h>
#include <string.h>

#include <wasm-rtos.h>

/* The same program is the parent and the child; argv selects the role. */
int main(int argc, char** argv)
{
    /* Spawned without arguments: argv[0] is the path it was started from. */
    if (argc == 1)
        return strcmp(argv[0], "/bin/proc_spawn_path") == 0 ? 42 : 1;

    if (argc == 3 && strcmp(argv[1], "child") == 0)
        return atoi(argv[2]);

    if (argc == 2 && strcmp(argv[1], "parent") == 0)
    {
        /* Returns a bit mask of failed checks; 0 means every check passed. */
        wr_str child_args[3];
        int failures = 0;
        int32_t child;

        child_args[0] = wr_cstr("child");
        child_args[1] = wr_cstr("child");
        child_args[2] = wr_cstr("5");
        child = wr_spawn_path_cstr("/bin/proc_spawn_path", child_args, 3U, 0U, 0U);
        if (child <= 0)
            failures |= 1;
        else if (wr_wait(child, WR_WAIT_FOREVER) != 5 || wr_close(child) != 0)
            failures |= 2;

        if (wr_spawn_path_cstr("/bin/missing", 0, 0U, 0U, 0U) != -WR_ENOTFOUND)
            failures |= 4;
        if (wr_spawn_path_cstr("/bin", 0, 0U, 0U, 0U) != -WR_EISDIR)
            failures |= 8;
        /* Paths and arguments are (pointer, length): no NUL needed. */
        child = wr_spawn_path("/bin/proc_spawn_pathXYZ", 20U, child_args, 3U, 0U, 0U);
        if (child <= 0 || wr_wait(child, WR_WAIT_FOREVER) != 5 || wr_close(child) != 0)
            failures |= 16;
        return failures;
    }

    return 99;
}
