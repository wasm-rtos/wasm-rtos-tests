#include "os.h"
#include "vm/source/wm.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Idle tasks make no code of their own (vm decision 21): an idle task runs
 * to the same result without adding predecoded records; raised above idle
 * in the middle, it makes them and goes on; a normal task makes them from
 * the start. Sanitizer builds predecode nothing, so the checks on records
 * hold there with none at all.
 */

typedef struct
{
    uint8_t* data;
    uint32_t size;
} Binary;

static FILE* log_file;
static int failures;

static void expect(int condition, const char* text)
{
    printf("%s %s\n", condition ? "PASS" : "FAIL", text);
    if (log_file)
    {
        fprintf(log_file, "%s %s\n", condition ? "PASS" : "FAIL", text);
        fflush(log_file);
    }
    if (!condition)
        ++failures;
}

static int load(Binary* binary)
{
    FILE* file = fopen("idle_in_place.wasm", "rb");
    long size;
    if (!file)
        return 0;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    rewind(file);
    binary->data = (uint8_t*)malloc((size_t)size);
    binary->size = (uint32_t)size;
    if (!binary->data || fread(binary->data, 1, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        free(binary->data);
        return 0;
    }
    fclose(file);
    return 1;
}

/* The task to its end, raised to normal after raise_at slices (0: never); the most records the cache held. */
static size_t run(const Binary* binary, uint32_t priority, uint32_t raise_at, uint32_t* exit_code, uint32_t* slices)
{
    OsTaskHandle task = NULL;
    size_t most = 0;
    uint32_t i;
    *exit_code = 0xFFFFFFFFu;
    *slices = 0;
    wm_code_cache_trim(0U);
    if (os_task_create(&task, binary->data, binary->size, "app_main", "idle", 64U * 1024U, priority) != OS_STATUS_OK)
        return (size_t)-1;
    for (i = 0; i < 1000000U && os_task_get_state(task) != OS_TASK_DEAD; ++i)
    {
        if (raise_at && i == raise_at)
            os_task_set_priority(task, OS_TASK_PRIORITY_NORMAL);
        if (os_schedule() != OS_STATUS_OK)
            break;
        if (wm_code_cache_bytes() > most)
            most = wm_code_cache_bytes();
    }
    *slices = i;
    if (os_task_get_state(task) == OS_TASK_DEAD && os_task_get_exit_reason(task) == OS_TASK_EXIT_RETURNED)
        *exit_code = os_task_get_exit_code(task);
    os_task_delete(task);
    return most;
}

int main(void)
{
    Binary binary = {0};
    uint32_t code, slices, normal_slices = 0;
    size_t normal, idle, raised;

    log_file = fopen("idle_in_place.log", "w");
    expect(os_init() == OS_STATUS_OK, "initialize OS");
    expect(load(&binary), "load idle_in_place.wasm");
    if (binary.data)
    {
        normal = run(&binary, OS_TASK_PRIORITY_NORMAL, 0U, &code, &normal_slices);
        expect(code == 0U, "normal task's result right");
        expect(normal_slices > 3U, "the task takes several slices");

        idle = run(&binary, OS_TASK_PRIORITY_IDLE, 0U, &code, &slices);
        expect(code == 0U, "idle task's result right");
        expect(idle == 0U, "idle task made no records");
        expect(slices == normal_slices, "idle task took the same slices (same fuel)");

        raised = run(&binary, OS_TASK_PRIORITY_IDLE, 2U, &code, &slices);
        expect(code == 0U, "raised task's result right");
        expect(raised == normal, "raised above idle, it made the records a normal task makes");
        expect(slices == normal_slices, "raised task took the same slices (same fuel)");
        printf("records: normal %zu, idle %zu, raised %zu; slices %u\n", normal, idle, raised, normal_slices);
        free(binary.data);
    }
    expect(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (log_file)
        fclose(log_file);
    return failures ? 1 : 0;
}
