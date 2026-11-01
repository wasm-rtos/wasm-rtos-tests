#include "os.h"
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
    if (log_file != NULL)
    {
        fprintf(log_file, "%s %s\n", condition ? "PASS" : "FAIL", text);
        fflush(log_file);
    }
    if (!condition)
        ++failures;
}

static int load(Binary* binary)
{
    FILE* file = fopen("queue_api.wasm", "rb");
    long size;
    if (file == NULL)
        return 0;
    fseek(file, 0L, SEEK_END);
    size = ftell(file);
    rewind(file);
    if (size <= 0L || (unsigned long)size > 0xFFFFFFFFUL)
    {
        fclose(file);
        return 0;
    }
    binary->data = (uint8_t*)malloc((size_t)size);
    binary->size = (uint32_t)size;
    if (binary->data == NULL || fread(binary->data, 1U, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        free(binary->data);
        binary->data = NULL;
        return 0;
    }
    fclose(file);
    return 1;
}

int main(void)
{
    Binary binary = {0};
    OsTaskHandle task = NULL;
    uint32_t iteration;

    log_file = fopen("queue_api.log", "w");
    expect(os_init() == OS_STATUS_OK, "initialize OS");
    expect(load(&binary), "load queue_api.wasm");

    if (binary.data != NULL)
    {
        expect(os_task_create(&task, binary.data, binary.size, "app_main", "queue_api", 64U * 1024U,
                              OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK &&
                   task != NULL,
               "create queue API task");
        for (iteration = 0U; task != NULL && iteration < 100U && os_task_get_state(task) != OS_TASK_DEAD; ++iteration)
            expect(os_schedule() == OS_STATUS_OK, "schedule queue API task");
        if (task != NULL)
        {
            expect(os_task_get_state(task) == OS_TASK_DEAD, "queue API task reaches DEAD");
            expect(os_task_get_exit_reason(task) == OS_TASK_EXIT_RETURNED, "queue API task returned normally");
            expect(os_task_get_exit_code(task) == 0U, "queue API task returned zero");
            if (os_task_get_exit_code(task) != 0U)
                printf("     failed check %u\n", (unsigned)os_task_get_exit_code(task));
            expect(os_task_delete(task) == OS_STATUS_OK, "delete queue API task");
        }
        free(binary.data);
    }

    {
        OsQueueHandle queue = NULL;
        uint32_t item = 1U;

        expect(os_queue_create(&queue, sizeof(uint32_t), 3U) == OS_STATUS_OK, "create host queue with 3 slots");
        expect(os_queue_get_space(queue) == 3U && os_queue_get_count(queue) == 0U, "empty queue has 3 free slots");
        expect(os_queue_send(queue, &item) == OS_STATUS_OK && os_queue_get_space(queue) == 2U, "a sent item takes a slot");
        expect(os_queue_receive(queue, &item) == OS_STATUS_OK && os_queue_get_space(queue) == 3U, "a received item frees it");
        expect(os_queue_get_space(NULL) == 0U, "NULL queue has no space");
        os_queue_delete(queue);
    }

    expect(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    if (log_file != NULL)
        fclose(log_file);
    return failures == 0 ? 0 : 1;
}
