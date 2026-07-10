#include "../support/test_support.h"

int main(void)
{
    TestContext test;
    TestBinary wasm;
    OsTaskHandle task = NULL;
    OsValue args[4];
    OsStatus status;

    if (!test_begin(&test, "entry_args_mixed_scalar", "entry_args_mixed_scalar.log")) return 1;

    if (test_load_binary(&test, "entry_args_mixed_scalar.wasm", &wasm))
    {
        args[0].type = OS_VALUE_TYPE_I32; args[0].value.i32 = 10U;
        args[1].type = OS_VALUE_TYPE_F32; args[1].value.f32 = 5.0f;
        args[2].type = OS_VALUE_TYPE_I64; args[2].value.i64 = 7U;
        args[3].type = OS_VALUE_TYPE_F64; args[3].value.f64 = 15.0;

        status = os_task_create_with_args(
            &task,
            wasm.bytes,
            wasm.size,
            "app_main",
            args,
            4U,
            "entry_args_mixed_scalar",
            64U * 1024U,
            OS_TASK_PRIORITY_NORMAL
        );
        test_expect(&test, status == OS_STATUS_OK && task != NULL, "create task with mixed scalar arguments");

        if (task != NULL)
        {
            test_expect(&test, test_schedule_until_dead(&test, task, 10U), "task with arguments reaches DEAD");
            test_expect(&test, os_task_get_exit_reason(task) == OS_TASK_EXIT_RETURNED, "task returned normally");
            test_expect(&test, os_task_get_exit_code(task) == 37U, "arguments arrived in correct order and type");
            test_expect(&test, os_task_delete(task) == OS_STATUS_OK, "delete argument task");
        }

        test_free_binary(&wasm);
    }

    test_expect_clean_os(&test);
    return test_finish(&test);
}
