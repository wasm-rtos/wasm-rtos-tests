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

static int load(Binary* binary)
{
    FILE* file = fopen("return_code.wasm", "rb");
    long size;
    if (!file)
        return 0;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    rewind(file);
    if (size <= 0 || (unsigned long)size > 0xFFFFFFFFUL)
    {
        fclose(file);
        return 0;
    }
    binary->data = malloc((size_t)size);
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

int main(void)
{
    Binary binary = {0};
    OsTaskHandle task = NULL;
    uint32_t i;
    log_file = fopen("return_code.log", "a");
    hal_init();
    expect(os_init() == OS_STATUS_OK, "initialize OS");
    expect(load(&binary), "load return_code.wasm");
    if (binary.data)
    {
        expect(os_task_create(&task, binary.data, binary.size, "app_main", "return_code", 64U * 1024U,
                              OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK &&
                   task != NULL,
               "create task");
        for (i = 0; task && i < 10U && os_task_get_state(task) != OS_TASK_DEAD; ++i)
            expect(os_schedule() == OS_STATUS_OK, "schedule task");
        if (task)
        {
            expect(os_task_get_state(task) == OS_TASK_DEAD, "task reaches DEAD");
            expect(os_task_get_exit_reason(task) == OS_TASK_EXIT_RETURNED, "exit reason is RETURNED");
            expect(os_task_get_exit_code(task) == 42U, "entry return code is preserved");
            expect(os_task_delete(task) == OS_STATUS_OK, "delete task");
        }
        free(binary.data);
    }
    expect(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (log_file)
        fclose(log_file);
    return failures ? 1 : 0;
}
