#include "os.h"
#include "hal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

static int load(Binary* b)
{
    FILE* f = fopen("snapshot_delay.wasm", "rb");
    long n;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    if (n <= 0 || (unsigned long)n > 0xFFFFFFFFUL)
    {
        fclose(f);
        return 0;
    }
    b->data = malloc((size_t)n);
    b->size = (uint32_t)n;
    if (!b->data || fread(b->data, 1, (size_t)n, f) != (size_t)n)
    {
        fclose(f);
        free(b->data);
        return 0;
    }
    fclose(f);
    return 1;
}

int main(void)
{
    Binary b = {0};
    OsTaskHandle source = NULL, restored = NULL;
    uint8_t* snapshot = NULL;
    uint8_t blocked_snapshot = 0U;
    uint32_t size = 0, written = 0, i;
    log_file = fopen("snapshot_delay.log", "w");
    hal_init();
    expect(os_init() == OS_STATUS_OK, "initialize OS");
    expect(load(&b), "load snapshot_delay.wasm");
    if (b.data)
    {
        expect(os_task_create(&source, b.data, b.size, "app_main", "snapshot_delay_source", 64U * 1024U,
                              OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK &&
                   source,
               "create source task");
        if (source)
        {
            expect(os_schedule() == OS_STATUS_OK, "run source until delay");
            expect(os_task_get_state(source) == OS_TASK_WAITING, "source is WAITING");
            expect(os_task_get_snapshot_size(source, &size) == OS_STATUS_BUSY && size == 0U,
                   "reject snapshot size while task is WAITING");
            expect(os_task_save_snapshot(source, &blocked_snapshot, 1U, &written) == OS_STATUS_BUSY,
                   "reject snapshot save while task is WAITING");
            os_tick(10U);
            expect(os_task_get_state(source) == OS_TASK_READY,
                   "delay completion makes source READY");
            expect(os_task_get_snapshot_size(source, &size) == OS_STATUS_OK && size > 0U,
                   "get snapshot size after wait completes");
            snapshot = malloc(size);
            expect(snapshot != NULL, "allocate snapshot");
            if (snapshot)
            {
                expect(os_task_save_snapshot(source, snapshot, size, &written) == OS_STATUS_OK && written > 0U,
                       "save snapshot after wait completes");
                expect(os_task_delete(source) == OS_STATUS_OK, "delete snapshot source task");
                source = NULL;
                expect(os_task_create(&restored, b.data, b.size, "app_main", "snapshot_delay_restored", 64U * 1024U,
                                      OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK &&
                           restored,
                       "create blocked snapshot target");
                if (restored)
                {
                    expect(os_schedule() == OS_STATUS_OK &&
                               os_task_get_state(restored) == OS_TASK_WAITING,
                           "run snapshot target until delay");
                    expect(os_task_load_snapshot(restored, snapshot, written) == OS_STATUS_BUSY &&
                               os_task_get_state(restored) == OS_TASK_WAITING,
                           "reject snapshot load without corrupting active wait");
                    os_tick(10U);
                    for (i = 0; i < 20U && os_task_get_state(restored) != OS_TASK_DEAD; ++i)
                        expect(os_schedule() == OS_STATUS_OK, "schedule unmodified target task");
                    expect(os_task_get_state(restored) == OS_TASK_DEAD,
                           "unmodified target reaches DEAD");
                    expect(os_task_get_exit_code(restored) == 21U,
                           "unmodified target returns 21");
                    expect(os_task_delete(restored) == OS_STATUS_OK,
                           "delete blocked snapshot target");
                }
                free(snapshot);
            }
            if (source)
                expect(os_task_delete(source) == OS_STATUS_OK, "delete source task");
        }
        free(b.data);
    }
    expect(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (log_file)
        fclose(log_file);
    return failures ? 1 : 0;
}
