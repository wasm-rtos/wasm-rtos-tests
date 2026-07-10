#include "../support/test_support.h"

int main(void)
{
    TestContext test;
    TestBinary wasm;
    OsTaskHandle task = NULL;
    OsStatus status;

    if (!test_begin(&test, "yield_once", "yield_once.log")) return 1;

    if (test_load_binary(&test, "yield_once.wasm", &wasm))
    {
        status = os_task_create(&task, wasm.bytes, wasm.size, "app_main", "yield_once", 64U * 1024U, OS_TASK_PRIORITY_NORMAL);
        test_expect(&test, status == OS_STATUS_OK && task != NULL, "create task");

        if (task != NULL)
        {
            test_expect(&test, os_schedule() == OS_STATUS_OK, "run until yield");
            test_expect(&test, os_task_get_state(task) == OS_TASK_READY, "yield returns task to READY");
            test_expect(&test, os_task_get_run_count(task) == 1U, "yield consumed one scheduler slice");
            test_expect(&test, test_schedule_until_dead(&test, task, 10U), "resume task after yield");
            test_expect(&test, os_task_get_exit_reason(task) == OS_TASK_EXIT_RETURNED, "task returned normally");
            test_expect(&test, os_task_get_exit_code(task) == 0U, "task returned zero");
            test_expect(&test, os_task_delete(task) == OS_STATUS_OK, "delete completed task");
        }

        test_free_binary(&wasm);
    }

    test_expect_clean_os(&test);
    return test_finish(&test);
}
