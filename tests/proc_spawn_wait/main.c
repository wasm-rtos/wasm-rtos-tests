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
    FILE* x = fopen("proc_spawn_wait.wasm", "rb");
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
    l = fopen("proc_spawn_wait.log", "a");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load proc_spawn_wait.wasm");
    if (b.d)
    {
        int saw_parent_waiting = 0;
        uint32_t most_tasks = 0U;
        ok(os_task_create(&t, b.d, b.n, NULL, "parent", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
           "create parent task");
        for (i = 0; t && i < 500U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        {
            os_tick(10U);
            os_schedule();
            if (os_get_task_count() > most_tasks)
                most_tasks = os_get_task_count();
            if (os_task_get_state(t) == OS_TASK_WAITING)
                saw_parent_waiting = 1;
        }
        if (t)
        {
            printf("parent result mask: %d\n", (int)return_i32(t));
            ok(os_task_get_state(t) == OS_TASK_DEAD, "parent finishes");
            ok(return_i32(t) == 0, "every check inside the parent passed");
            ok(most_tasks == 3U, "both children ran next to the parent");
            ok(saw_parent_waiting, "parent blocked in wait while the slow child slept");
            os_schedule();
            /* The parent is task 1; its children were created as tasks 2 and 3. */
            ok(os_task_find_by_id(2U) == NULL && os_task_find_by_id(3U) == NULL, "collected children were reclaimed");
            ok(os_task_find_by_id(os_task_get_id(t)) == t, "host-created parent stays until deleted");
            ok(os_task_delete(t) == OS_STATUS_OK, "delete parent");
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
