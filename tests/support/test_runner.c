#include "test_runner.h"
#include "test_support.h"

#include "wasm3/source/m3_env.h"

#include <stdlib.h>

#ifndef TEST_NAME
#error TEST_NAME is required
#endif
#ifndef TEST_KIND
#error TEST_KIND is required
#endif
#ifndef TEST_ENTRY
#define TEST_ENTRY "app_main"
#endif
#ifndef TEST_EXPECTED_CODE
#define TEST_EXPECTED_CODE 0U
#endif
#ifndef TEST_EXPECTED_REASON
#define TEST_EXPECTED_REASON OS_TASK_EXIT_RETURNED
#endif

#define TEST_WASM TEST_NAME ".wasm"
#define TEST_LOG TEST_NAME ".log"

static int create_task(TestContext* test, TestBinary* binary, OsTaskHandle* task)
{
    OsStatus status = os_task_create(task, binary->bytes, binary->size, TEST_ENTRY, TEST_NAME, 64U * 1024U, OS_TASK_PRIORITY_NORMAL);
    return test_expect(test, status == OS_STATUS_OK && *task != NULL, "create task");
}

static void delete_task(TestContext* test, OsTaskHandle task)
{
    OsStatus status = os_task_delete(task);
    test_expect(test, status == OS_STATUS_OK || status == OS_STATUS_TASK_DEAD, "delete task");
}

static void run_finite(TestContext* test, TestBinary* binary)
{
    OsTaskHandle task = NULL;
    if (!create_task(test, binary, &task)) return;
    test_expect(test, test_schedule_until_dead(test, task, 1000U), "task reaches DEAD");
    test_expect(test, os_task_get_exit_reason(task) == TEST_EXPECTED_REASON, "exit reason");
    test_expect(test, os_task_get_exit_code(task) == TEST_EXPECTED_CODE, "exit code");
    delete_task(test, task);
}

static void run_yield(TestContext* test, TestBinary* binary)
{
    OsTaskHandle task = NULL;
    if (!create_task(test, binary, &task)) return;
    test_expect(test, os_schedule() == OS_STATUS_OK, "first schedule");
    test_expect(test, os_task_get_state(task) == OS_TASK_READY, "yield returns READY");
    test_expect(test, test_schedule_until_dead(test, task, 10U), "resume to DEAD");
    delete_task(test, task);
}

static void run_delay(TestContext* test, TestBinary* binary)
{
    OsTaskHandle task = NULL;
    if (!create_task(test, binary, &task)) return;
    test_expect(test, os_schedule() == OS_STATUS_OK, "enter delay");
    test_expect(test, os_task_get_state(task) == OS_TASK_WAITING, "WAITING state");
    os_tick(9U);
    test_expect(test, os_task_get_state(task) == OS_TASK_WAITING, "not ready before deadline");
    os_tick(1U);
    test_expect(test, os_task_get_state(task) == OS_TASK_READY, "ready at deadline");
    test_expect(test, test_schedule_until_dead(test, task, 10U), "resume to DEAD");
    delete_task(test, task);
}

static void run_bad_import(TestContext* test, TestBinary* binary)
{
    OsTaskHandle task = NULL;
    OsStatus status = os_task_create(&task, binary->bytes, binary->size, TEST_ENTRY, TEST_NAME, 64U * 1024U, OS_TASK_PRIORITY_NORMAL);
    test_expect(test, status == OS_STATUS_WASM_ERROR, "WASM error returned");
    test_expect(test, task == NULL, "null task returned");
    test_expect(test, os_get_last_error_status() == OS_STATUS_WASM_ERROR, "diagnostic status");
}

static void run_long_running(TestContext* test, TestBinary* binary)
{
    OsTaskHandle task = NULL;
    if (!create_task(test, binary, &task)) return;
    test_expect(test, os_schedule() == OS_STATUS_OK, "bounded slice");
    test_expect(test, os_task_get_state(task) == OS_TASK_READY, "task remains READY");
    test_expect(test, os_task_get_run_count(task) == 1U, "one run recorded");
    delete_task(test, task);
}

static void run_get_time(TestContext* test, TestBinary* binary)
{
    os_tick(1234U);
    run_finite(test, binary);
}

static void run_return_values(TestContext* test, TestBinary* binary)
{
    const char* entries[4] = { "app_main_i32", "app_main_i64", "app_main_f32", "app_main_f64" };
    OsValueType types[4] = { OS_VALUE_TYPE_I32, OS_VALUE_TYPE_I64, OS_VALUE_TYPE_F32, OS_VALUE_TYPE_F64 };
    uint32_t index;

    for (index = 0U; index < 4U; ++index)
    {
        OsTaskHandle task = NULL;
        OsValue value;
        OsStatus status = os_task_create(&task, binary->bytes, binary->size, entries[index], entries[index], 64U * 1024U, OS_TASK_PRIORITY_NORMAL);
        test_expect(test, status == OS_STATUS_OK && task != NULL, "create return-value task");
        if (status != OS_STATUS_OK || task == NULL) continue;
        test_expect(test, test_schedule_until_dead(test, task, 10U), "return-value task reaches DEAD");
        status = os_task_get_return_value(task, &value);
        test_expect(test, status == OS_STATUS_OK && value.type == types[index], "return-value type");
        if (index == 0U) test_expect(test, value.value.i32 == 42U, "i32 value");
        if (index == 1U) test_expect(test, value.value.i64 == 0x1122334455667788ULL, "i64 value");
        if (index == 2U) test_expect(test, value.value.f32 == 12.5f, "f32 value");
        if (index == 3U) test_expect(test, value.value.f64 == 123.25, "f64 value");
        delete_task(test, task);
    }
}

static void run_entry_args(TestContext* test, TestBinary* binary)
{
    OsTaskHandle task = NULL;
    OsValue args[4];
    OsStatus status;
    args[0].type = OS_VALUE_TYPE_I32; args[0].value.i32 = 10U;
    args[1].type = OS_VALUE_TYPE_F32; args[1].value.f32 = 5.0f;
    args[2].type = OS_VALUE_TYPE_I64; args[2].value.i64 = 7U;
    args[3].type = OS_VALUE_TYPE_F64; args[3].value.f64 = 15.0;
    status = os_task_create_with_args(&task, binary->bytes, binary->size, TEST_ENTRY, args, 4U, TEST_NAME, 64U * 1024U, OS_TASK_PRIORITY_NORMAL);
    test_expect(test, status == OS_STATUS_OK && task != NULL, "create task with arguments");
    if (status != OS_STATUS_OK || task == NULL) return;
    test_expect(test, test_schedule_until_dead(test, task, 10U), "task reaches DEAD");
    test_expect(test, os_task_get_exit_code(task) == 37U, "mixed scalar result");
    delete_task(test, task);
}

static void run_snapshot(TestContext* test, TestBinary* binary, OsTaskState expected_state, uint32_t wake_ms, uint32_t expected_code)
{
    OsTaskHandle source = NULL;
    OsTaskHandle restore = NULL;
    uint8_t* snapshot = NULL;
    uint32_t size = 0U;
    uint32_t written = 0U;
    OsStatus status;
    if (!create_task(test, binary, &source)) return;
    test_expect(test, os_schedule() == OS_STATUS_OK, "source first schedule");
    test_expect(test, os_task_get_state(source) == expected_state, "source snapshot state");
    status = os_task_get_snapshot_size(source, &size);
    test_expect(test, status == OS_STATUS_OK && size > 0U, "snapshot size");
    snapshot = (uint8_t*)malloc(size);
    test_expect(test, snapshot != NULL, "allocate snapshot");
    if (snapshot != NULL)
    {
        status = os_task_save_snapshot(source, snapshot, size, &written);
        test_expect(test, status == OS_STATUS_OK && written > 0U, "save snapshot");
        if (create_task(test, binary, &restore))
        {
            status = os_task_load_snapshot(restore, snapshot, written);
            test_expect(test, status == OS_STATUS_OK, "load snapshot");
            if (wake_ms > 0U) os_tick(wake_ms);
            test_expect(test, test_schedule_until_dead(test, restore, 20U), "restored task reaches DEAD");
            test_expect(test, os_task_get_exit_code(restore) == expected_code, "restored exit code");
            delete_task(test, restore);
        }
        free(snapshot);
    }
    delete_task(test, source);
}

static m3ApiRawFunction(custom_import)
{
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, input);
    (void)runtime; (void)_ctx; (void)_mem;
    m3ApiReturn(input + 42U);
}

static void run_custom_import(TestContext* test, TestBinary* binary)
{
    test_expect(test, os_host_import_register("env", "host_test_value", "i(i)", custom_import) == OS_STATUS_OK, "register import");
    run_finite(test, binary);
}

static m3ApiRawFunction(queue_create_import)
{
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, item_size);
    m3ApiGetArg(uint32_t, item_count);
    OsQueueHandle queue = NULL;
    (void)runtime; (void)_ctx; (void)_mem;
    if (os_queue_create(&queue, item_size, item_count) != OS_STATUS_OK) m3ApiReturn(0U);
    m3ApiReturn(os_queue_get_id(queue));
}

static m3ApiRawFunction(queue_delete_import)
{
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, queue_id);
    OsQueueHandle queue = os_queue_find_by_id(queue_id);
    (void)runtime; (void)_ctx; (void)_mem;
    if (queue == NULL) m3ApiReturn((uint32_t)OS_STATUS_QUEUE_NOT_FOUND);
    os_queue_delete(queue);
    m3ApiReturn((uint32_t)OS_STATUS_OK);
}

static void run_queue_api(TestContext* test, TestBinary* binary)
{
    test_expect(test, os_host_import_register("env", "os_queue_create", "i(ii)", queue_create_import) == OS_STATUS_OK, "register queue create");
    test_expect(test, os_host_import_register("env", "os_queue_delete", "i(i)", queue_delete_import) == OS_STATUS_OK, "register queue delete");
    run_finite(test, binary);
}

int test_runner_main(void)
{
    TestContext test;
    TestBinary binary;
    if (!test_begin(&test, TEST_NAME, TEST_LOG)) return 1;
    if (test_load_binary(&test, TEST_WASM, &binary))
    {
#if TEST_KIND == TEST_FINITE
        run_finite(&test, &binary);
#elif TEST_KIND == TEST_YIELD
        run_yield(&test, &binary);
#elif TEST_KIND == TEST_DELAY
        run_delay(&test, &binary);
#elif TEST_KIND == TEST_BAD_IMPORT
        run_bad_import(&test, &binary);
#elif TEST_KIND == TEST_LONG_RUNNING
        run_long_running(&test, &binary);
#elif TEST_KIND == TEST_GET_TIME
        run_get_time(&test, &binary);
#elif TEST_KIND == TEST_RETURN_VALUES
        run_return_values(&test, &binary);
#elif TEST_KIND == TEST_ENTRY_ARGS
        run_entry_args(&test, &binary);
#elif TEST_KIND == TEST_SNAPSHOT_YIELD
        run_snapshot(&test, &binary, OS_TASK_READY, 0U, 12U);
#elif TEST_KIND == TEST_SNAPSHOT_DELAY
        run_snapshot(&test, &binary, OS_TASK_WAITING, 10U, 21U);
#elif TEST_KIND == TEST_CUSTOM_IMPORT
        run_custom_import(&test, &binary);
#elif TEST_KIND == TEST_QUEUE_API
        run_queue_api(&test, &binary);
#else
#error Unsupported TEST_KIND
#endif
        test_free_binary(&binary);
    }
    test_expect_clean_os(&test);
    return test_finish(&test);
}
