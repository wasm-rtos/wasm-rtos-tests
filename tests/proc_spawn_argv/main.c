#include "os.h"
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
    FILE* x = fopen("proc_spawn_argv.wasm", "rb");
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

static int32_t return_i32(OsTaskHandle t)
{
    OsValue v;
    if (os_task_get_return_value(t, &v) != OS_STATUS_OK || v.type != OS_VALUE_TYPE_I32)
        return -1000;
    return (int32_t)v.value.i32;
}

/* os_task_get_id_list() skips dead tasks, so walk the ids this test used. */
static void delete_all_tasks(void)
{
    uint32_t id;
    for (id = 1U; id <= 64U; id++)
        if (os_task_find_by_id(id) != NULL)
            os_task_delete(os_task_find_by_id(id));
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;
    uint32_t i;
    l = fopen("proc_spawn_argv.log", "a");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load proc_spawn_argv.wasm");
    if (b.d)
    {
        const char* argv_storage[3];
        char* first = malloc(32);
        char* second = malloc(32);
        char* third = malloc(32);
        uint32_t id = 0U;
        strcpy(first, "/bin/proc_spawn_argv");
        strcpy(second, "--period");
        strcpy(third, "250");
        argv_storage[0] = first;
        argv_storage[1] = second;
        argv_storage[2] = third;
        ok(os_task_spawn(&t, b.d, b.n, argv_storage, 3U, 0U) == OS_STATUS_OK && t, "host spawns a WASI program with argv");
        /* The kernel must have copied the module and the arguments. */
        memset(b.d, 0, b.n);
        memset(first, 'x', 31);
        memset(second, 'x', 31);
        memset(third, 'x', 31);
        free(first);
        free(second);
        free(third);
        if (t)
        {
            id = os_task_get_id(t);
            ok(strcmp(os_task_get_name(t), "proc_spawn_argv") == 0, "process is named after argv[0]");
            ok(os_task_get_parent_id(t) == 0U, "host-spawned process has no parent");
            ok(os_task_get_priority(t) == OS_TASK_PRIORITY_NORMAL, "priority 0 picks the normal priority");
        }
        for (i = 0; t && i < 50U && os_task_get_state(t) != OS_TASK_DEAD; i++)
            os_schedule();
        if (t)
        {
            ok(os_task_get_state(t) == OS_TASK_DEAD, "process finishes");
            printf("exit code: %u\n", os_task_get_exit_code(t));
            ok(os_task_get_exit_code(t) == 0U, "program saw argv exactly as passed");
            os_schedule();
            ok(os_task_find_by_id(id) == t, "host-spawned process is kept until the host deletes it");
            ok(os_task_delete(t) == OS_STATUS_OK, "delete process");
        }
        {
            OsTaskHandle bad = NULL;
            const char* argv_with_null[2] = { "a", NULL };
            ok(os_task_spawn(&bad, NULL, 4U, NULL, 0U, 0U) == OS_STATUS_INVALID_ARGUMENT, "spawn rejects missing bytes");
            ok(os_task_spawn(&bad, (const uint8_t*)"x", 1U, argv_with_null, 2U, 0U) == OS_STATUS_INVALID_ARGUMENT,
               "spawn rejects a NULL argument");
        }
        free(b.d);
    }
    delete_all_tasks();
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
