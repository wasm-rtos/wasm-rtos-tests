#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Buffers a program passes the OS split across two frames of a paged
 * memory: the interpreter's heap here has no block large enough for the
 * program's memory, so the memory is paged in 4 KiB frames, and each call
 * must work on the bytes by address instead of through one pointer.
 */
#define FRAME 4096U

typedef struct
{
    uint8_t* d;
    uint32_t n;
} Bin;

/* Everything the output port received. */
typedef struct
{
    char text[2048];
    uint32_t length;
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
    FILE* x = fopen("guest_buffer_frames.wasm", "rb");
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

/* The heap with no block over 100 KiB, like a fragmented one: a 128 KiB memory goes paged. */
static void* fragmented(void* pointer, size_t size)
{
    if (size == 0U)
    {
        free(pointer);
        return NULL;
    }
    return size > 100U * 1024U ? NULL : realloc(pointer, size);
}

static void capture_write(void* context, uint32_t task_id, uint32_t fd, const void* data, uint32_t size)
{
    Capture* capture = (Capture*)context;
    (void)task_id;
    (void)fd;
    if (capture->length + size < sizeof(capture->text))
    {
        memcpy(capture->text + capture->length, data, size);
        capture->length += size;
        capture->text[capture->length] = '\0';
    }
}

int main(void)
{
    Bin b = {0};
    Capture capture = {{0}, 0U};
    OsOutputPort port = {&capture, capture_write};
    OsQueueHandle queue = NULL;
    OsTaskHandle t = NULL;
    OsTaskMemoryUsage usage = {0};
    const char* at = NULL;
    unsigned area = 0U;
    uint8_t bytes[12] = {0};
    uint32_t i;
    l = fopen("guest_buffer_frames.log", "a");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    wm_set_allocator(fragmented);
    ok(load(&b), "load guest_buffer_frames.wasm");
    if (!b.d)
        return 1;
    ok(os_output_port_set(&port) == OS_STATUS_OK, "install output port");
    ok(os_queue_create(&queue, 16U, 1U) == OS_STATUS_OK && queue, "create a queue of 16-byte items");
    os_queue_set_name(queue, "frames");

    ok(os_task_create(&t, b.d, b.n, "_start", "frames", 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "create the program");
    if (!t)
        return 1;
    os_task_get_memory_usage(t, &usage);
    ok(usage.linear_memory_bytes > 100U * 1024U && usage.resident_memory_bytes == usage.linear_memory_bytes,
       "its memory is larger than any block, so paged, and all of it is in RAM");

    /* It runs its own checks, then waits for an item. */
    for (i = 0; i < 100U && os_task_get_state(t) != OS_TASK_WAITING && os_task_get_state(t) != OS_TASK_DEAD; i++)
        os_schedule();
    ok(os_task_get_state(t) == OS_TASK_WAITING, "the program waits for an item");
    at = strstr(capture.text, "area ");
    ok(at != NULL && sscanf(at, "area %u", &area) == 1 && area % FRAME == 0U, "the program says where its buffers are");

    /* The host's own access by address, across frames. */
    ok(os_task_read_memory(t, area + 7U * FRAME - 6U, bytes, 12U) == OS_STATUS_OK && memcmp(bytes, "ACROSS-FRAME", 12U) == 0,
       "os_task_read_memory reads across frames");
    ok(os_task_write_memory(t, area + 8U * FRAME - 3U, (const uint8_t*)"HOSTPUT", 7U) == OS_STATUS_OK,
       "os_task_write_memory writes across frames");
    ok(os_task_read_memory(t, 0xFFFFFFF0U, bytes, 12U) == OS_STATUS_OUT_OF_BOUNDS, "an address past the memory is refused");

    /* The item goes straight to the waiting program, across frames. */
    ok(os_queue_send(queue, "from the host!!") == OS_STATUS_OK, "send the program an item");
    for (i = 0; i < 100U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        os_schedule();
    ok(os_task_get_state(t) == OS_TASK_DEAD && os_task_get_exit_code(t) == 0U, "the program's checks all pass");
    ok(strstr(capture.text, "written across frames\n") != NULL, "fd_write delivered the split bytes");
    ok(strstr(capture.text, "FAIL") == NULL, "the program reports no failure");
    printf("program output:\n%s", capture.text);
    if (l)
        fprintf(l, "OUTPUT %s", capture.text);

    os_task_delete(t);
    os_queue_delete(queue);
    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    free(b.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
