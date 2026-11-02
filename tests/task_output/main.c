#include "os.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint8_t* d;
    uint32_t n;
} Bin;

/* Everything the output port received; a line starts with who wrote it. */
typedef struct
{
    char text[1024];
    uint32_t length;
    uint32_t writes;
    uint8_t mid_line;
} Capture;

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
    FILE* x = fopen("task_output.wasm", "rb");
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
        return 0;
    }
    fclose(x);
    return 1;
}

/*
 * Record lines as "<task id>:<fd>:<text>" so the test sees who wrote what, in
 * order. Output is a byte stream: libc may hand one line over in pieces.
 */
static void capture_append(Capture* capture, const char* format, ...)
{
    va_list args;
    int written;
    va_start(args, format);
    written = vsnprintf(capture->text + capture->length, sizeof(capture->text) - capture->length, format, args);
    va_end(args);
    if (written > 0 && capture->length + (uint32_t)written < sizeof(capture->text))
        capture->length += (uint32_t)written;
}

static void capture_write(void* context, uint32_t task_id, uint32_t fd, const void* data, uint32_t size)
{
    Capture* capture = (Capture*)context;
    if (!capture->mid_line)
        capture_append(capture, "%u:%u:", (unsigned)task_id, (unsigned)fd);
    capture_append(capture, "%.*s", (int)size, (const char*)data);
    capture->mid_line = size > 0U && ((const char*)data)[size - 1U] != '\n';
    capture->writes++;
}

static void run_until_dead(OsTaskHandle a, OsTaskHandle b)
{
    uint32_t i;
    for (i = 0; i < 100U && (os_task_get_state(a) != OS_TASK_DEAD || os_task_get_state(b) != OS_TASK_DEAD); i++)
        os_schedule();
}

int main(void)
{
    Bin b = {0};
    Capture capture = {0};
    OsOutputPort port = {&capture, capture_write};
    OsOutputPort no_write = {&capture, NULL};
    OsTaskHandle t1 = NULL;
    OsTaskHandle t2 = NULL;
    char expected[512];
    l = fopen("task_output.log", "a");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load task_output.wasm");
    ok(os_output_port_set(&no_write) == OS_STATUS_INVALID_ARGUMENT, "a port without write is rejected");
    ok(os_output_port_set(&port) == OS_STATUS_OK, "install output port");
    if (b.d)
    {
        ok(os_task_create(&t1, b.d, b.n, "_start", "first", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t1,
           "create first task");
        ok(os_task_create(&t2, b.d, b.n, "_start", "second", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t2,
           "create second task");
        if (t1 && t2)
        {
            uint32_t a = os_task_get_id(t1);
            uint32_t c = os_task_get_id(t2);
            run_until_dead(t1, t2);
            ok(os_task_get_exit_code(t1) == 0U && os_task_get_exit_code(t2) == 0U, "both programs return zero");
            /* Each yield hands the CPU to the other task, so their lines interleave. */
            snprintf(expected, sizeof(expected),
                     "%u:1:out one\n%u:2:err one\n%u:1:out one\n%u:2:err one\n"
                     "%u:1:out two\n%u:1:fd 7: -1 EBADF\n%u:1:out two\n%u:1:fd 7: -1 EBADF\n",
                     a, a, c, c, a, a, c, c);
            ok(strcmp(capture.text, expected) == 0, "output carries the writer's task id and fd, in order");
            if (strcmp(capture.text, expected) != 0)
                printf("got:\n%s\nexpected:\n%s\n", capture.text, expected);
            ok(os_task_delete(t1) == OS_STATUS_OK && os_task_delete(t2) == OS_STATUS_OK, "delete tasks");
        }

        /* Without a port, output goes to the C library again. */
        ok(os_output_port_set(NULL) == OS_STATUS_OK, "remove output port");
        capture.writes = 0U;
        t1 = NULL;
        ok(os_task_create(&t1, b.d, b.n, "_start", "plain", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t1,
           "create task without a port");
        if (t1)
        {
            run_until_dead(t1, t1);
            ok(os_task_get_exit_code(t1) == 0U && capture.writes == 0U, "default output bypasses the removed port");
            os_task_delete(t1);
        }

        /* os_init clears the port. */
        ok(os_output_port_set(&port) == OS_STATUS_OK && os_init() == OS_STATUS_OK, "reinitialize with a port set");
        t1 = NULL;
        if (os_task_create(&t1, b.d, b.n, "_start", "after_init", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK)
        {
            run_until_dead(t1, t1);
            ok(capture.writes == 0U, "os_init clears the output port");
            os_task_delete(t1);
        }
        free(b.d);
    }
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
