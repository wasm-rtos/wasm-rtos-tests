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
    FILE* x = fopen("proc_kill_orphan.wasm", "rb");
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
    l = fopen("proc_kill_orphan.log", "a");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load proc_kill_orphan.wasm");
    if (b.d)
    {
        uint32_t ids[8];
        uint32_t count = 0U;
        uint32_t parent_id = 0U;
        OsTaskHandle orphan = NULL;
        ok(os_task_create(&t, b.d, b.n, NULL, "parent", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
           "create parent task");
        parent_id = t ? os_task_get_id(t) : 0U;
        for (i = 0; t && i < 500U && os_task_get_state(t) != OS_TASK_DEAD; i++)
            os_schedule();
        if (t)
        {
            printf("parent result mask: %d\n", (int)return_i32(t));
            ok(os_task_get_state(t) == OS_TASK_DEAD, "parent finishes");
            ok(return_i32(t) == 0, "kill, wait and spawn checks inside the parent passed");
        }
        for (i = 0; i < 5U; i++)
            os_schedule();
        /* Task 2 is the killed child, task 3 the orphan. */
        ok(os_task_find_by_id(2U) == NULL, "killed and collected child was reclaimed");
        ok(t && os_task_find_by_id(parent_id) == t, "dead host-created parent stays until deleted");
        count = os_task_get_id_list(ids, 8U);
        ok(count == 1U, "only the orphan is still alive");
        for (i = 0; i < count; i++)
            if (ids[i] != parent_id)
                orphan = os_task_find_by_id(ids[i]);
        ok(orphan != NULL && os_task_get_parent_id(orphan) == 0U, "child of a dead parent is orphaned");
        ok(orphan != NULL && os_task_get_state(orphan) != OS_TASK_DEAD, "orphan keeps running");
        free(b.d);
    }
    delete_all_tasks();
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
