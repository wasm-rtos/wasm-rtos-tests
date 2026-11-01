#include <stdlib.h>
#include <string.h>

#define PROC_IMPORT(name) __attribute__((import_module("wasm-rtos:proc"), import_name(name)))
PROC_IMPORT("spawn_path") int proc_spawn_path(const char* path, const char* const* argv, unsigned argc, unsigned priority);
PROC_IMPORT("wait") unsigned proc_wait(int pid, unsigned* exit_code, unsigned timeout_ms);

#define OS_STATUS_OK 0
#define OS_STATUS_FILE_NOT_FOUND 25
#define OS_STATUS_IS_A_DIRECTORY 27
#define OS_WAIT_FOREVER 0xFFFFFFFFU

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
        static const char* const child_argv[] = { "child", "child", "5" };
        int failures = 0;
        unsigned code = 0U;
        int pid = proc_spawn_path("/bin/proc_spawn_path", child_argv, 3U, 0U);

        if (pid <= 0)
            failures |= 1;
        else if (proc_wait(pid, &code, OS_WAIT_FOREVER) != OS_STATUS_OK || code != 5U)
            failures |= 2;

        if (proc_spawn_path("/bin/missing", 0, 0U, 0U) != -OS_STATUS_FILE_NOT_FOUND)
            failures |= 4;
        if (proc_spawn_path("/bin", 0, 0U, 0U) != -OS_STATUS_IS_A_DIRECTORY)
            failures |= 8;
        return failures;
    }

    return 99;
}
