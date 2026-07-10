#include "../support/test_support.h"
#include "wasm3/source/m3_env.h"

static m3ApiRawFunction(host_test_value)
{
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, input);
    (void)runtime;
    (void)_ctx;
    (void)_mem;
    m3ApiReturn(input + 42U);
}

int main(void)
{
    TestContext test;
    TestBinary wasm;
    OsTaskHandle task = NULL;
    OsStatus status;

    if (!test_begin(&test, "custom_import", "custom_import.log")) return 1;

    status = os_host_import_register("env", "host_test_value", "i(i)", host_test_value);
    test_expect(&test, status == OS_STATUS_OK, "register custom host import");

    if (test_load_binary(&test, "custom_import.wasm", &wasm))
    {
        status = os_task_create(&task, wasm.bytes, wasm.size, "app_main", "custom_import", 64U * 1024U, OS_TASK_PRIORITY_NORMAL);
        test_expect(&test, status == OS_STATUS_OK && task != NULL, "link task against custom import");
        if (task != NULL)
        {
            test_expect(&test, test_schedule_until_dead(&test, task, 10U), "custom-import task reaches DEAD");
            test_expect(&test, os_task_get_exit_reason(task) == OS_TASK_EXIT_RETURNED, "custom-import task returned normally");
            test_expect(&test, os_task_get_exit_code(task) == 0U, "custom import returned expected value");
            test_expect(&test, os_task_delete(task) == OS_STATUS_OK, "delete custom-import task");
        }
        test_free_binary(&wasm);
    }

    os_host_import_clear_all();
    test_expect_clean_os(&test);
    return test_finish(&test);
}
