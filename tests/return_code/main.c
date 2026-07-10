#include "../support/test_support.h"

int main(void)
{
    TestContext test;
    TestBinary wasm;
    OsTaskHandle task = NULL;
    OsStatus status;

    if (!test_begin(&test, "return_code", "return_code.log")) return 1;

    if (test_load_binary(&test, "return_code.wasm", &wasm))
    {
        status = os_task_create(&task, wasm.bytes, wasm.size, "app_main", "return_code", 64U * 1024U, OS_TASK_PRIORITY_NORMAL);
        test_expect(&test, status == OS_STATUS_OK && task != NULL, "create task");
        if (task != NULL)
        {
            test_expect(&test, test_schedule_until_dead(&test, task, 10U), "task reaches DEAD");
            test_expect(&test, os_task_get_exit_reason(task) == OS_TASK_EXIT_RETURNED, "exit reason is RETURNED");
            test_expect(&test, os_task_get_exit_code(task) == 42U, "entry return code is preserved");
            test_expect(&test, os_task_delete(task) == OS_STATUS_OK, "delete completed task");
        }
        test_free_binary(&wasm);
    }

    test_expect_clean_os(&test);
    return test_finish(&test);
}
