#include "../support/test_support.h"

int main(void)
{
    TestContext test;
    TestBinary wasm;
    OsTaskHandle task = NULL;
    OsStatus status;

    if (!test_begin(&test, "simple_loop", "simple_loop.log")) return 1;

    if (test_load_binary(&test, "simple_loop.wasm", &wasm))
    {
        status = os_task_create(&task, wasm.bytes, wasm.size, "app_main", "simple_loop", 64U * 1024U, OS_TASK_PRIORITY_NORMAL);
        test_expect(&test, status == OS_STATUS_OK && task != NULL, "create loop task");
        if (task != NULL)
        {
            test_expect(&test, test_schedule_until_dead(&test, task, 1000U), "finite loop reaches DEAD");
            test_expect(&test, os_task_get_exit_reason(task) == OS_TASK_EXIT_RETURNED, "finite loop returned normally");
            test_expect(&test, os_task_get_exit_code(task) == 0U, "finite loop returned zero");
            test_expect(&test, os_task_get_run_count(task) >= 1U, "scheduler recorded loop execution");
            test_expect(&test, os_task_delete(task) == OS_STATUS_OK, "delete loop task");
        }
        test_free_binary(&wasm);
    }

    test_expect_clean_os(&test);
    return test_finish(&test);
}
