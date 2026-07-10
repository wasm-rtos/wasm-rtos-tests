#include "../support/test_support.h"

int main(void)
{
    TestContext test;
    TestBinary wasm;
    OsTaskHandle task = NULL;
    OsStatus status;

    if (!test_begin(&test, "get_time_once", "get_time_once.log")) return 1;

    os_tick(1234U);
    test_expect(&test, os_get_tick_ms() == 1234U, "host tick advanced to expected value");

    if (test_load_binary(&test, "get_time_once.wasm", &wasm))
    {
        status = os_task_create(&task, wasm.bytes, wasm.size, "app_main", "get_time_once", 64U * 1024U, OS_TASK_PRIORITY_NORMAL);
        test_expect(&test, status == OS_STATUS_OK && task != NULL, "create get-time task");
        if (task != NULL)
        {
            test_expect(&test, test_schedule_until_dead(&test, task, 10U), "get-time task reaches DEAD");
            test_expect(&test, os_task_get_exit_reason(task) == OS_TASK_EXIT_RETURNED, "get-time task returned normally");
            test_expect(&test, os_task_get_exit_code(task) == 0U, "guest observed host time 1234 ms");
            test_expect(&test, os_task_delete(task) == OS_STATUS_OK, "delete get-time task");
        }
        test_free_binary(&wasm);
    }

    test_expect_clean_os(&test);
    return test_finish(&test);
}
