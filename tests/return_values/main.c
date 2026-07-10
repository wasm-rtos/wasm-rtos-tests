#include "../support/test_support.h"

int main(void)
{
    static const char* entries[] = { "app_main_i32", "app_main_i64", "app_main_f32", "app_main_f64" };
    static const OsValueType types[] = { OS_VALUE_TYPE_I32, OS_VALUE_TYPE_I64, OS_VALUE_TYPE_F32, OS_VALUE_TYPE_F64 };
    TestContext test;
    TestBinary wasm;
    uint32_t index;

    if (!test_begin(&test, "return_values", "return_values.log")) return 1;

    if (test_load_binary(&test, "return_values.wasm", &wasm))
    {
        for (index = 0U; index < 4U; ++index)
        {
            OsTaskHandle task = NULL;
            OsValue value;
            OsStatus status = os_task_create(&task, wasm.bytes, wasm.size, entries[index], entries[index], 64U * 1024U, OS_TASK_PRIORITY_NORMAL);
            test_expect(&test, status == OS_STATUS_OK && task != NULL, "create typed return task");
            if (task == NULL) continue;

            test_expect(&test, test_schedule_until_dead(&test, task, 10U), "typed return task reaches DEAD");
            status = os_task_get_return_value(task, &value);
            test_expect(&test, status == OS_STATUS_OK, "read return value");
            test_expect(&test, value.type == types[index], "return value type matches export");

            if (index == 0U) test_expect(&test, value.value.i32 == 42U, "i32 return value");
            if (index == 1U) test_expect(&test, value.value.i64 == 0x1122334455667788ULL, "i64 return value");
            if (index == 2U) test_expect(&test, value.value.f32 == 12.5f, "f32 return value");
            if (index == 3U) test_expect(&test, value.value.f64 == 123.25, "f64 return value");

            test_expect(&test, os_task_delete(task) == OS_STATUS_OK, "delete typed return task");
        }
        test_free_binary(&wasm);
    }

    test_expect_clean_os(&test);
    return test_finish(&test);
}
