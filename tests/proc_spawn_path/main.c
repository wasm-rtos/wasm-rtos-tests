#include "os.h"
#include "fs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint8_t* d;
    uint32_t n;
} Bin;

static FILE* l;
static int f;

static void ok(int c, const char* s)
{
    printf("%s %s\n", c ? "PASS" : "FAIL", s);
    if (l)
    {
        fprintf(l, "%s %s\n", c ? "PASS" : "FAIL", s);
        fflush(l);
    }
    if (!c)
        f++;
}

static int load(Bin* b)
{
    FILE* x = fopen("proc_spawn_path.wasm", "rb");
    long n;
    if (!x)
        return 0;
    fseek(x, 0, SEEK_END);
    n = ftell(x);
    rewind(x);
    if (n <= 0)
        return fclose(x), 0;
    b->d = malloc((size_t)n);
    b->n = (uint32_t)n;
    if (!b->d || fread(b->d, 1, (size_t)n, x) != (size_t)n)
    {
        fclose(x);
        free(b->d);
        b->d = NULL;
        return 0;
    }
    fclose(x);
    return 1;
}

/* Minimal ustar writer: appends one entry and returns the next offset. */
static uint32_t tar_add(uint8_t* image, uint32_t offset, const char* name, char type, const void* data, uint32_t size)
{
    uint8_t* h = image + offset;
    unsigned sum = 0U;
    uint32_t i;

    memset(h, 0, 512);
    strncpy((char*)h, name, 100);
    memcpy(h + 100, "0000644", 7);
    memcpy(h + 108, "0000000", 7);
    memcpy(h + 116, "0000000", 7);
    snprintf((char*)h + 124, 12, "%011o", size);
    snprintf((char*)h + 136, 12, "%011o", 0U);
    h[156] = (uint8_t)type;
    memcpy(h + 257, "ustar", 6);
    memcpy(h + 263, "00", 2);
    memset(h + 148, ' ', 8);
    for (i = 0; i < 512; i++)
        sum += h[i];
    snprintf((char*)h + 148, 8, "%06o", sum);
    h[155] = ' ';
    if (size)
        memcpy(image + offset + 512, data, size);
    return offset + 512 + (size + 511U) / 512U * 512U;
}

/* Runs every task until t is dead and returns its exit code. */
static uint32_t run_until_dead(OsTaskHandle t)
{
    uint32_t i;
    for (i = 0; t && i < 1000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        os_schedule();
    if (!t || os_task_get_state(t) != OS_TASK_DEAD)
        return 999U;
    return os_task_get_exit_code(t);
}

int main(void)
{
    Bin b = {0};
    OsFsTar tar;
    OsFsTar copy_tar;
    /* The tar driver without map: files must be read into RAM. */
    OsFsDriver copy_driver = os_fs_tar_driver;
    uint8_t* image = NULL;
    uint32_t image_size = 0U;
    OsTaskHandle t = NULL;
    const char* const child_argv[] = { "/bin/proc_spawn_path", "child", "7" };
    const char* const copy_argv[] = { "copied", "child", "9" };
    const char* const parent_argv[] = { "parent", "parent" };

    copy_driver.map = NULL;
    l = fopen("proc_spawn_path.log", "a");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load proc_spawn_path.wasm");
    if (b.d)
    {
        image = calloc(1U, b.n + 2048U);
        image_size = tar_add(image, 0U, "bin/proc_spawn_path", '0', b.d, b.n) + 1024U;
    }
    ok(image && os_fs_tar_init(&tar, image, image_size) == OS_STATUS_OK, "program image is valid");
    ok(image && os_fs_tar_init(&copy_tar, image, image_size) == OS_STATUS_OK, "second view of the image is valid");
    ok(os_fs_mount("/", &os_fs_tar_driver, &tar) == OS_STATUS_OK, "mount mapping driver at /");
    ok(os_fs_mount("/ram", &copy_driver, &copy_tar) == OS_STATUS_OK, "mount copying driver at /ram");

    /* Mapped: runs from the image and keeps the mount busy until destroyed. */
    ok(os_task_spawn_path(&t, "/bin/proc_spawn_path", child_argv, 3U, 0U, 0U) == OS_STATUS_OK && t,
       "spawn mapped program by path with argv");
    ok(os_fs_unmount("/") == OS_STATUS_BUSY, "mount of a running mapped program is busy");
    ok(run_until_dead(t) == 7U, "mapped program got argv and exited with 7");
    ok(os_fs_unmount("/") == OS_STATUS_BUSY, "dead but undeleted program still holds its mount");
    ok(os_task_delete(t) == OS_STATUS_OK, "delete mapped program");
    ok(os_fs_unmount("/") == OS_STATUS_OK, "mount is free after the program is destroyed");
    ok(os_fs_mount("/", &os_fs_tar_driver, &tar) == OS_STATUS_OK, "mount mapping driver again");

    /* Without argv the program sees its path as argv[0]. */
    t = NULL;
    ok(os_task_spawn_path(&t, "/bin/proc_spawn_path", NULL, 0U, 0U, 0U) == OS_STATUS_OK && t,
       "spawn by path without argv");
    ok(run_until_dead(t) == 42U, "argv[0] is the program path");
    os_task_delete(t);

    /* Copied: the program no longer needs its file system. */
    t = NULL;
    ok(os_task_spawn_path(&t, "/ram/bin/proc_spawn_path", copy_argv, 3U, 0U, 0U) == OS_STATUS_OK && t,
       "spawn program from a driver without map");
    ok(os_fs_unmount("/ram") == OS_STATUS_OK, "copied program does not hold its mount");
    ok(run_until_dead(t) == 9U, "copied program runs after its mount is gone");
    os_task_delete(t);

    /* Errors leave no process behind. */
    t = NULL;
    ok(os_task_spawn_path(&t, "/bin/missing", NULL, 0U, 0U, 0U) == OS_STATUS_FILE_NOT_FOUND && !t,
       "missing file is FILE_NOT_FOUND");
    ok(os_task_spawn_path(&t, "/bin", NULL, 0U, 0U, 0U) == OS_STATUS_IS_A_DIRECTORY && !t,
       "directory is IS_A_DIRECTORY");
    ok(os_task_spawn_path(&t, "/ram/bin/proc_spawn_path", NULL, 0U, 0U, 0U) == OS_STATUS_FILE_NOT_FOUND,
       "unmounted path is FILE_NOT_FOUND");
    ok(os_task_spawn_path(&t, NULL, NULL, 0U, 0U, 0U) == OS_STATUS_INVALID_ARGUMENT, "NULL path is rejected");
    ok(os_task_spawn_path(&t, "", NULL, 0U, 0U, 0U) == OS_STATUS_INVALID_ARGUMENT, "empty path is rejected");

    /* A guest spawns by path, waits for the child and checks guest errors. */
    t = NULL;
    ok(os_task_spawn_path(&t, "/bin/proc_spawn_path", parent_argv, 2U, 0U, 0U) == OS_STATUS_OK && t,
       "spawn guest parent");
    ok(run_until_dead(t) == 0U, "guest spawn_path, wait and error codes work");
    os_task_delete(t);
    ok(os_get_task_count() == 0U, "no process left behind");
    ok(os_fs_unmount("/") == OS_STATUS_OK, "every image was released");

    os_shutdown();
    free(image);
    free(b.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
