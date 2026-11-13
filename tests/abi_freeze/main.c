#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * ABI 1.x frozen at 1.8: a program importing every call loads; newer minor
 * versions load and calls the kernel lacks return -WR_EUNSUPPORTED; WASI
 * calls nothing answers return ENOSYS; a child asked above its parent runs
 * at the parent's priority, while the host still chooses any.
 */

#ifndef TEST_NAME
#define TEST_NAME "abi_freeze"
#endif

#define WASI_EBADF 8
#define WASI_ENOSYS 52

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
    FILE* x = fopen(TEST_NAME ".wasm", "rb");
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

/* The one live task other than skip, or NULL. */
static OsTaskHandle other_task(OsTaskHandle skip)
{
    uint32_t ids[8];
    uint32_t count = os_task_get_id_list(ids, 8U);
    for (uint32_t i = 0; i < count; i++)
    {
        OsTaskHandle t = os_task_find_by_id(ids[i]);
        if (t != NULL && t != skip)
            return t;
    }
    return NULL;
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;
    l = fopen(TEST_NAME ".log", "a");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    printf("build: processes %d, files %d\n", OS_HAS_PROCESSES, OS_HAS_FS);
    ok(load(&b), "load " TEST_NAME ".wasm");
    /* A pin gives programs the gpio driver, so a newer gpio call goes through it. */
    ok(os_gpio_add_pin("led", 2U, OS_GPIO_CAN_OUTPUT, 0U) == OS_STATUS_OK, "the gpio driver is there");
    if (b.d)
    {
        uint32_t i;
        int32_t r;
        OsTaskHandle child;
        ok(os_task_create(&t, b.d, b.n, NULL, TEST_NAME, 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
           "program importing every 1.8 call, @1.0.0, @1.9.0 and unanswered WASI calls loads");
        for (i = 0U; t && i < 500U && os_task_get_state(t) != OS_TASK_DEAD; ++i)
            os_schedule();
        ok(t && os_task_get_state(t) == OS_TASK_DEAD && os_task_get_exit_reason(t) == OS_TASK_EXIT_RETURNED,
           "program finishes");
        r = t ? return_i32(t) : -1000;
        printf("returned 0x%x\n", (unsigned)r);
        ok(r >= 0 && (r & 0x100) == 0, "a call named @1.0.0 and @1.9.0 is the same call");
        ok(r >= 0 && (r & 0x200) == 0, "a kernel call 1.8 lacks, named @1.9.0, returns -WR_EUNSUPPORTED");
        ok(r >= 0 && (r & 0x400) == 0, "a driver call 1.8 lacks, named @1.9.0, returns -WR_EUNSUPPORTED");
        ok(r >= 0 && (r & 0x800) == 0, "an interface 1.8 lacks returns -WR_EUNSUPPORTED");
        ok(r >= 0 && (r & 0x1000) == 0, "a WASI call nothing answers returns ENOSYS");
        ok(r >= 0 && (r & 0x2000) == 0, "spawn above the parent's priority succeeds (or is unsupported)");
        ok(r >= 0 && (r & 0xff) == (OS_HAS_FS ? WASI_EBADF : WASI_ENOSYS),
           OS_HAS_FS ? "a WASI file call is answered with files" : "a WASI file call returns ENOSYS without files");

        child = other_task(t);
#if OS_HAS_PROCESSES
        ok(child != NULL && os_task_get_base_priority(child) == OS_TASK_PRIORITY_NORMAL,
           "a child asked at priority 20 runs at its parent's 5");
#else
        ok(child == NULL, "no child without processes");
#endif
        if (child)
            os_task_delete(child);
        if (t)
            os_task_delete(t);

#if OS_HAS_PROCESSES
        {
            /* The host is not a program: it starts processes at any priority. */
            static const uint8_t endless[] = {
                0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
                0x01, 0x04, 0x01, 0x60, 0x00, 0x00,
                0x03, 0x02, 0x01, 0x00,
                0x07, 0x0c, 0x01, 0x08, 'a', 'p', 'p', '_', 'm', 'a', 'i', 'n', 0x00, 0x00,
                0x0a, 0x09, 0x01, 0x07, 0x00, 0x03, 0x40, 0x0c, 0x00, 0x0b, 0x0b
            };
            OsTaskHandle h = NULL;
            ok(os_task_spawn(&h, endless, sizeof(endless), NULL, 0U, OS_TASK_PRIORITY_REALTIME, 0U) == OS_STATUS_OK &&
                   h && os_task_get_base_priority(h) == OS_TASK_PRIORITY_REALTIME,
               "the host spawns at priority 20");
            if (h)
                os_task_delete(h);
        }
#endif
        free(b.d);
    }
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
