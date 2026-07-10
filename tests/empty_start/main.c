#include "../support/test_support.h"

int main(void)
{
    TestContext test;
    TestBinary wasm;
    OsTaskHandle task = NULL;
    OsStatus status;

    if (!test_begin(&test, "empty_start", "empty_start.log"))
    {
        return 1;
    }

    if (test_load_binary(&test, "empty_start.wasm", &wasm))
    {
        status = os_task_create(
            &task,
            wasm.bytes,
            wasm.size,
            "app_main",
            "empty_start",
            64U * 1024U,
            OS_TASK_PRIORITY_NORMAL
        );
        test_expect(&test, status == OS_STATUS_OK && task != NULL, "create empty task");

        if (status == OS_STATUS_OK && task != NULL)
        {
            test_expect(&test, os_task_get_state(task) == OS_TASK_READY, "task starts READY");
            test_expect(&test, os_schedule() == OS_STATUS_OK, "schedule empty task");
            test_expect(&test, os_task_get_state(task) == OS_TASK_DEAD, "task becomes DEAD");
            test_expect(&test, os_task_get_exit_reason(task) == OS_TASK_EXIT_RETURNED, "exit reason is RETURNED");
            test_expect(&test, os_task_get_exit_code(task) == 0U, "exit code is zero");
            test_expect(&test, os_task_get_run_count(task) == 1U, "task ran once");
            test_expect(&test, os_task_delete(task) == OS_STATUS_OK, "delete empty task");
        }

        test_free_binary(&wasm);
    }

    test_expect_clean_os(&test);
    return test_finish(&test);
}
