#include "../support/test_support.h"

#include <string.h>

int main(void)
{
    TestContext test;
    TestBinary wasm;
    OsTaskHandle task = NULL;
    OsStatus status;
    const char* phase;
    const char* result;

    if (!test_begin(&test, "bad_import", "bad_import.log")) return 1;

    if (test_load_binary(&test, "bad_import.wasm", &wasm))
    {
        status = os_task_create(&task, wasm.bytes, wasm.size, "app_main", "bad_import", 64U * 1024U, OS_TASK_PRIORITY_NORMAL);
        phase = os_get_last_error_phase();
        result = os_get_last_error_result();

        test_expect(&test, status == OS_STATUS_WASM_ERROR, "task creation reports WASM error");
        test_expect(&test, task == NULL, "failed creation returns null task");
        test_expect(&test, os_get_last_error_status() == OS_STATUS_WASM_ERROR, "diagnostic status is WASM error");
        test_expect(&test, phase != NULL && strcmp(phase, "none") != 0, "diagnostic phase is recorded");
        test_expect(&test, result != NULL && result[0] != '\0', "diagnostic result is recorded");
        test_expect(&test, os_get_task_count() == 0U, "failed task was not inserted");

        test_free_binary(&wasm);
    }

    test_expect_clean_os(&test);
    return test_finish(&test);
}
