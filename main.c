/* Native test runner for the external wasm-rtos-tests repository. */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#endif

#include "wasm-rtos/os.h"
#include "wasm-rtos/hal.h"

static const int TEST_FAILURE_INIT = 1;
static const int TEST_FAILURE_TICK_ZERO = 2;
static const int TEST_FAILURE_EMPTY_OS_INITIAL = 3;
static const int TEST_FAILURE_HAL_NATIVE_TIME = 4;
static const int TEST_FAILURE_TICK_ADVANCE = 7;
static const int TEST_FAILURE_EMPTY_SCHEDULE = 8;
static const int TEST_FAILURE_EMPTY_OS_AFTER_SCHEDULE = 9;
static const int TEST_FAILURE_MISSING_WASM = 100;
static const uint32_t TEST_WASM_STACK_SIZE = 64U * 1024U;
static const uint32_t TEST_WASM_PRIORITY = OS_TASK_PRIORITY_NORMAL;
static const uint32_t TEST_MAX_SCHEDULE_ITERATIONS = 1000U;
static const uint32_t TEST_MESSAGE_BUFFER_SIZE = 512U;
static const uint32_t TEST_TICK_WRAP_START = 0xFFFFFFF8U;

static const char* TEST_LOG_DIRECTORY = "logs";
static const char* TEST_LOG_PATH = "logs/smoke_test.log";

static FILE* g_log_file = 0;

typedef struct TestBinary
{
    uint8_t* bytes;
    uint32_t size;
} TestBinary;

static void format_message(char* buffer, uint32_t buffer_size, const char* format, ...)
{
    va_list args;

    if (buffer == 0 || buffer_size == 0U)
    {
        return;
    }

    buffer[0] = '\0';
    if (format == 0)
    {
        return;
    }

    va_start(args, format);
    (void)vsnprintf(buffer, (size_t)buffer_size, format, args);
    va_end(args);
    buffer[buffer_size - 1U] = '\0';
}

static int create_log_directory(void)
{
#if defined(_WIN32)
    if (_mkdir(TEST_LOG_DIRECTORY) == 0)
    {
        return 1;
    }
#else
    if (mkdir(TEST_LOG_DIRECTORY, 0777) == 0)
    {
        return 1;
    }
#endif

    /* If the directory already exists, opening the log below will still succeed. */
    return 1;
}

static int log_open(void)
{
    if (!create_log_directory())
    {
        printf("Smoke test failure: failed to create %s\n", TEST_LOG_DIRECTORY);
        return 0;
    }

    g_log_file = fopen(TEST_LOG_PATH, "w");
    if (g_log_file == 0)
    {
        printf("Smoke test failure: failed to open %s\n", TEST_LOG_PATH);
        return 0;
    }

    return 1;
}

static void log_close(void)
{
    if (g_log_file != 0)
    {
        fclose(g_log_file);
        g_log_file = 0;
    }
}

static void log_write(const char* level, const char* message)
{
    if (g_log_file != 0)
    {
        fprintf(g_log_file, "%s %s\n", level, message);
        fflush(g_log_file);
    }
}

static void log_phase(const char* message)
{
    log_write("PHASE", message);
}

static void log_info(const char* message)
{
    log_write("INFO", message);
}

static void log_pass(const char* message)
{
    log_write("PASS", message);
}

static void log_fail(const char* message)
{
    log_write("FAIL", message);
}

static const char* os_status_name(OsStatus status)
{
    switch (status)
    {
        case OS_STATUS_OK: return "OS_STATUS_OK";
        case OS_STATUS_ERROR: return "OS_STATUS_ERROR";
        case OS_STATUS_INVALID_ARGUMENT: return "OS_STATUS_INVALID_ARGUMENT";
        case OS_STATUS_OUT_OF_MEMORY: return "OS_STATUS_OUT_OF_MEMORY";
        case OS_STATUS_WASM_ERROR: return "OS_STATUS_WASM_ERROR";
        case OS_STATUS_TASK_DEAD: return "OS_STATUS_TASK_DEAD";
        case OS_STATUS_TASK_NOT_FOUND: return "OS_STATUS_TASK_NOT_FOUND";
        case OS_STATUS_NO_READY_TASKS: return "OS_STATUS_NO_READY_TASKS";
        case OS_STATUS_BUFFER_TOO_SMALL: return "OS_STATUS_BUFFER_TOO_SMALL";
        case OS_STATUS_UNSUPPORTED: return "OS_STATUS_UNSUPPORTED";
        default: return "OS_STATUS_UNKNOWN";
    }
}

static const char* os_task_state_name(OsTaskState state)
{
    switch (state)
    {
        case OS_TASK_READY: return "OS_TASK_READY";
        case OS_TASK_RUNNING: return "OS_TASK_RUNNING";
        case OS_TASK_WAITING: return "OS_TASK_WAITING";
        case OS_TASK_SUSPENDED: return "OS_TASK_SUSPENDED";
        case OS_TASK_SWAPPED: return "OS_TASK_SWAPPED";
        case OS_TASK_DEAD: return "OS_TASK_DEAD";
        default: return "OS_TASK_UNKNOWN";
    }
}

static const char* os_task_exit_reason_name(OsTaskExitReason reason)
{
    switch (reason)
    {
        case OS_TASK_EXIT_NONE: return "OS_TASK_EXIT_NONE";
        case OS_TASK_EXIT_RETURNED: return "OS_TASK_EXIT_RETURNED";
        case OS_TASK_EXIT_EXPLICIT: return "OS_TASK_EXIT_EXPLICIT";
        case OS_TASK_EXIT_DELETED: return "OS_TASK_EXIT_DELETED";
        case OS_TASK_EXIT_WASM_ERROR: return "OS_TASK_EXIT_WASM_ERROR";
        default: return "OS_TASK_EXIT_UNKNOWN";
    }
}


static void log_last_os_error(const char* context)
{
    char message[TEST_MESSAGE_BUFFER_SIZE];
    const char* phase = os_get_last_error_phase();
    const char* result = os_get_last_error_result();
    const char* task_name = os_get_last_error_task_name();

    format_message(
        message,
        TEST_MESSAGE_BUFFER_SIZE,
        "%s last OS error: status=%s phase=%s result=%s task=%s",
        context == 0 ? "unknown" : context,
        os_status_name(os_get_last_error_status()),
        phase == 0 ? "null" : phase,
        result == 0 ? "null" : result,
        task_name == 0 ? "null" : task_name
    );
    log_info(message);
}

static int fail_test(const char* message, int code);

static void sleep_for_hal_time_test(void)
{
#if defined(_WIN32)
    Sleep(5);
#else
    struct timespec requested_sleep;
    requested_sleep.tv_sec = 0;
    requested_sleep.tv_nsec = 5L * 1000L * 1000L;
    (void)nanosleep(&requested_sleep, 0);
#endif
}

static int run_hal_native_time_test(int code_base)
{
    const char* test_name = "HAL native time";
    uint32_t pre_init = 0U;
    uint32_t start = 0U;
    uint32_t after = 0U;
    uint32_t retries = 0U;
    const uint32_t max_retries = 5U;
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s test start", test_name);
    log_phase(message);

    os_shutdown();
    hal_shutdown();

    pre_init = hal_get_time_ms();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s pre-init value=%u", test_name, pre_init);
    log_info(message);
    if (pre_init != 0U)
    {
        return fail_test("HAL native time expected pre-init value to be zero", code_base);
    }

    hal_init();
    log_info("HAL native time hal_init called");

    start = hal_get_time_ms();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s start value=%u", test_name, start);
    log_info(message);

    sleep_for_hal_time_test();
    after = hal_get_time_ms();

    while (after == start && retries < max_retries)
    {
        ++retries;
        sleep_for_hal_time_test();
        after = hal_get_time_ms();
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s after-sleep value=%u", test_name, after);
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s retries=%u", test_name, retries);
    log_info(message);

    if (after < start)
    {
        return fail_test("HAL native time moved backward", code_base + 1);
    }

    if (after == start)
    {
        return fail_test("HAL native time did not advance after retries", code_base + 2);
    }

    hal_shutdown();
    hal_init();

    status = os_init();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_init after HAL test status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        return fail_test("HAL native time failed to reinitialize OS after test", code_base + 3);
    }

    log_pass("HAL native time final PASS");
    return 0;
}

static void log_task_counters(const char* prefix)
{
    char message[TEST_MESSAGE_BUFFER_SIZE];
    format_message(
        message,
        TEST_MESSAGE_BUFFER_SIZE,
        "%s task counters: tasks=%u ready=%u waiting=%u current=%s",
        prefix,
        os_get_task_count(),
        os_get_ready_task_count(),
        os_get_waiting_task_count(),
        os_task_get_current() == 0 ? "null" : "non-null"
    );
    log_info(message);
}

static void log_exit_metadata_task_snapshot(const char* label, OsTaskHandle task)
{
    char message[TEST_MESSAGE_BUFFER_SIZE];

    format_message(
        message,
        TEST_MESSAGE_BUFFER_SIZE,
        "%s task=%s state=%s run_count=%u exit_reason=%s exit_code=%u",
        label,
        os_task_get_name(task) == 0 ? "null" : os_task_get_name(task),
        os_task_state_name(os_task_get_state(task)),
        os_task_get_run_count(task),
        os_task_exit_reason_name(os_task_get_exit_reason(task)),
        os_task_get_exit_code(task)
    );
    log_info(message);
    log_task_counters(label);
    log_last_os_error(label);
}

static void shutdown_harness(void)
{
    os_shutdown();
    hal_shutdown();
    log_close();
}

static int fail_test(const char* message, int code)
{
    char log_message[TEST_MESSAGE_BUFFER_SIZE];

    printf("Smoke test failure: %s (code %d)\n", message, code);
    format_message(log_message, TEST_MESSAGE_BUFFER_SIZE, "%s code=%d", message, code);
    log_fail(log_message);
    log_task_counters("failure");

    /* Keep the log open so the remaining tests can continue writing diagnostics. */
    os_shutdown();
    (void)os_init();

    return code;
}

static void record_test_status(const char* test_name, int status, int* failure_count, int* first_failure_code)
{
    char message[TEST_MESSAGE_BUFFER_SIZE];

    if (status == 0)
    {
        return;
    }

    if (failure_count != 0)
    {
        *failure_count += 1;
    }

    if (first_failure_code != 0 && *first_failure_code == 0)
    {
        *first_failure_code = status;
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s failed with code=%d; continuing", test_name, status);
    log_fail(message);
}

static int read_binary_file(const char* path, TestBinary* out_binary)
{
    FILE* file = 0;
    long file_size = 0;
    size_t bytes_read = 0;

    if (out_binary == 0)
    {
        return 0;
    }

    out_binary->bytes = 0;
    out_binary->size = 0U;

    file = fopen(path, "rb");
    if (file == 0)
    {
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return 0;
    }

    file_size = ftell(file);
    if (file_size <= 0L)
    {
        fclose(file);
        return 0;
    }

    if ((unsigned long)file_size > 0xFFFFFFFFUL)
    {
        fclose(file);
        return 0;
    }

    if (fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return 0;
    }

    out_binary->bytes = (uint8_t*)malloc((size_t)file_size);
    if (out_binary->bytes == 0)
    {
        fclose(file);
        return 0;
    }

    bytes_read = fread(out_binary->bytes, 1U, (size_t)file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size)
    {
        free(out_binary->bytes);
        out_binary->bytes = 0;
        return 0;
    }

    out_binary->size = (uint32_t)file_size;
    return 1;
}

static void free_binary(TestBinary* binary)
{
    if (binary != 0)
    {
        free(binary->bytes);
        binary->bytes = 0;
        binary->size = 0U;
    }
}

static int verify_no_tasks(int code_base)
{
    char message[TEST_MESSAGE_BUFFER_SIZE];

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "empty OS checks start code_base=%d", code_base);
    log_phase(message);
    log_task_counters("empty OS checks");

    if (os_get_task_count() != 0U)
    {
        log_fail("empty OS checks fail: expected zero live tasks");
        return fail_test("expected zero live tasks", code_base);
    }

    if (os_get_ready_task_count() != 0U)
    {
        log_fail("empty OS checks fail: expected zero ready tasks");
        return fail_test("expected zero ready tasks", code_base + 1);
    }

    if (os_get_waiting_task_count() != 0U)
    {
        log_fail("empty OS checks fail: expected zero waiting tasks");
        return fail_test("expected zero waiting tasks", code_base + 2);
    }

    if (os_task_get_current() != 0)
    {
        log_fail("empty OS checks fail: expected no current task");
        return fail_test("expected no current task", code_base + 3);
    }

    log_pass("empty OS checks pass");
    return 0;
}


static int run_task_id_host_api_test(int code_base)
{
    TestBinary wasm_binary;
    OsTaskHandle task_a = 0;
    OsTaskHandle task_b = 0;
    OsStatus status = OS_STATUS_OK;
    uint32_t id_a = 0U;
    uint32_t id_b = 0U;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    log_phase("task id host API test start");

    int no_task_status = verify_no_tasks(code_base);
    if (no_task_status != 0)
    {
        return no_task_status;
    }

    if (os_task_get_id(0) != 0U)
    {
        return fail_test("os_task_get_id(NULL) did not return zero", code_base + 1);
    }

    if (!read_binary_file("build/empty_start.wasm", &wasm_binary))
    {
        log_fail("WASM file load failure path=build/empty_start.wasm");
        return fail_test("empty_start.wasm missing or unreadable for task id host API test", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&task_a, wasm_binary.bytes, wasm_binary.size, "app_main", "task_id_a", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task_a == 0)
    {
        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("task id host API create task_a");
        }
        free_binary(&wasm_binary);
        return fail_test("task id host API task_a creation failed", code_base + 2);
    }

    status = os_task_create(&task_b, wasm_binary.bytes, wasm_binary.size, "app_main", "task_id_b", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task_b == 0)
    {
        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("task id host API create task_b");
        }
        (void)os_task_delete(task_a);
        free_binary(&wasm_binary);
        return fail_test("task id host API task_b creation failed", code_base + 3);
    }

    id_a = os_task_get_id(task_a);
    id_b = os_task_get_id(task_b);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "task id host API initial ids a=%u b=%u", id_a, id_b);
    log_info(message);

    if (id_a == 0U || id_b == 0U || id_a == id_b ||
        os_task_get_id(task_a) != id_a || os_task_get_id(task_b) != id_b ||
        os_task_get_state(task_a) != OS_TASK_READY || os_task_get_state(task_b) != OS_TASK_READY ||
        os_get_task_count() != 2U || os_get_ready_task_count() != 2U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("task id host API initial checks");
        }
        (void)os_task_delete(task_a);
        (void)os_task_delete(task_b);
        free_binary(&wasm_binary);
        return fail_test("task id host API initial expectations failed", code_base + 4);
    }

    status = os_schedule();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "task id host API after schedule status=%s state_a=%s state_b=%s ids=%u/%u",
        os_status_name(status), os_task_state_name(os_task_get_state(task_a)), os_task_state_name(os_task_get_state(task_b)),
        os_task_get_id(task_a), os_task_get_id(task_b));
    log_info(message);

    if (status != OS_STATUS_OK ||
        os_task_get_id(task_a) != id_a || os_task_get_id(task_b) != id_b ||
        os_task_get_state(task_a) != OS_TASK_DEAD || os_task_get_state(task_b) != OS_TASK_READY ||
        os_get_task_count() != 1U || os_get_ready_task_count() != 1U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("task id host API after schedule");
        }
        (void)os_task_delete(task_a);
        (void)os_task_delete(task_b);
        free_binary(&wasm_binary);
        return fail_test("task id host API after schedule expectations failed", code_base + 5);
    }

    status = os_task_delete(task_a);
    if (status != OS_STATUS_OK)
    {
        (void)os_task_delete(task_b);
        free_binary(&wasm_binary);
        return fail_test("task id host API task_a cleanup failed", code_base + 6);
    }

    status = os_task_delete(task_b);
    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        return fail_test("task id host API task_b cleanup failed", code_base + 7);
    }

    no_task_status = verify_no_tasks(code_base + 8);
    if (no_task_status != 0)
    {
        free_binary(&wasm_binary);
        return no_task_status;
    }

    log_pass("task id host API test final PASS");
    free_binary(&wasm_binary);
    return 0;
}


static int run_task_id_list_host_api_test(int code_base)
{
    TestBinary wasm_binary;
    OsTaskHandle task_a = 0;
    OsTaskHandle task_b = 0;
    OsStatus status = OS_STATUS_OK;
    uint32_t id_a = 0U;
    uint32_t id_b = 0U;
    uint32_t ids[2] = { 0U, 0U };
    uint32_t count = 0U;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    log_phase("task id list host API test start");

    int no_task_status = verify_no_tasks(code_base);
    if (no_task_status != 0)
    {
        return no_task_status;
    }

    if (os_task_get_id_list(NULL, 2U) != 0U || os_task_get_id_list(ids, 0U) != 0U)
    {
        return fail_test("task id list host API empty argument handling failed", code_base + 1);
    }

    if (!read_binary_file("build/empty_start.wasm", &wasm_binary))
    {
        log_fail("WASM file load failure path=build/empty_start.wasm");
        return fail_test("empty_start.wasm missing or unreadable for task id list host API test", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&task_a, wasm_binary.bytes, wasm_binary.size, "app_main", "task_id_list_a", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task_a == 0)
    {
        if (status == OS_STATUS_WASM_ERROR) { log_last_os_error("task id list host API create task_a"); }
        free_binary(&wasm_binary);
        return fail_test("task id list host API task_a creation failed", code_base + 2);
    }

    status = os_task_create(&task_b, wasm_binary.bytes, wasm_binary.size, "app_main", "task_id_list_b", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task_b == 0)
    {
        if (status == OS_STATUS_WASM_ERROR) { log_last_os_error("task id list host API create task_b"); }
        (void)os_task_delete(task_a);
        free_binary(&wasm_binary);
        return fail_test("task id list host API task_b creation failed", code_base + 3);
    }

    id_a = os_task_get_id(task_a);
    id_b = os_task_get_id(task_b);
    count = os_task_get_id_list(ids, 2U);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "task id list host API full count=%u ids=%u/%u expected=%u/%u", count, ids[0], ids[1], id_a, id_b);
    log_info(message);

    if (id_a == 0U || id_b == 0U || id_a == id_b || count != 2U || ids[0] != id_a || ids[1] != id_b ||
        os_get_task_count() != 2U || os_get_ready_task_count() != 2U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR) { log_last_os_error("task id list host API full list checks"); }
        (void)os_task_delete(task_a);
        (void)os_task_delete(task_b);
        free_binary(&wasm_binary);
        return fail_test("task id list host API full list expectations failed", code_base + 4);
    }

    ids[0] = 0U;
    ids[1] = 0U;
    count = os_task_get_id_list(ids, 1U);
    if (count != 1U || ids[0] != id_a)
    {
        (void)os_task_delete(task_a);
        (void)os_task_delete(task_b);
        free_binary(&wasm_binary);
        return fail_test("task id list host API truncated list expectations failed", code_base + 5);
    }

    status = os_schedule();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "task id list host API after schedule status=%s state_a=%s state_b=%s",
        os_status_name(status), os_task_state_name(os_task_get_state(task_a)), os_task_state_name(os_task_get_state(task_b)));
    log_info(message);

    if (status != OS_STATUS_OK || os_task_get_state(task_a) != OS_TASK_DEAD || os_task_get_state(task_b) != OS_TASK_READY)
    {
        (void)os_task_delete(task_a);
        (void)os_task_delete(task_b);
        free_binary(&wasm_binary);
        return fail_test("task id list host API schedule expectations failed", code_base + 6);
    }

    ids[0] = 0U;
    ids[1] = 0U;
    count = os_task_get_id_list(ids, 2U);
    if (count != 1U || ids[0] != id_b)
    {
        (void)os_task_delete(task_a);
        (void)os_task_delete(task_b);
        free_binary(&wasm_binary);
        return fail_test("task id list host API dead task skip expectations failed", code_base + 7);
    }

    status = os_task_delete(task_a);
    if (status != OS_STATUS_OK)
    {
        (void)os_task_delete(task_b);
        free_binary(&wasm_binary);
        return fail_test("task id list host API task_a cleanup failed", code_base + 8);
    }

    status = os_task_delete(task_b);
    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        return fail_test("task id list host API task_b cleanup failed", code_base + 9);
    }

    ids[0] = id_a;
    ids[1] = id_b;
    if (os_task_get_id_list(ids, 2U) != 0U)
    {
        free_binary(&wasm_binary);
        return fail_test("task id list host API returned ids after cleanup", code_base + 10);
    }

    no_task_status = verify_no_tasks(code_base + 11);
    if (no_task_status != 0)
    {
        free_binary(&wasm_binary);
        return no_task_status;
    }

    log_pass("task id list host API test final PASS");
    free_binary(&wasm_binary);
    return 0;
}


static int run_task_find_by_id_host_api_test(int code_base)
{
    TestBinary wasm_binary;
    OsTaskHandle task_a = 0;
    OsTaskHandle task_b = 0;
    OsStatus status = OS_STATUS_OK;
    uint32_t id_a = 0U;
    uint32_t id_b = 0U;
    uint32_t unknown_id = 0U;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    log_phase("task find by id host API test start");

    int no_task_status = verify_no_tasks(code_base);
    if (no_task_status != 0)
    {
        return no_task_status;
    }

    if (os_task_find_by_id(0U) != 0 || os_task_find_by_id(0xFFFFFFFFU) != 0)
    {
        return fail_test("os_task_find_by_id returned a task for zero or unknown id before task creation", code_base + 1);
    }

    if (!read_binary_file("build/empty_start.wasm", &wasm_binary))
    {
        log_fail("WASM file load failure path=build/empty_start.wasm");
        return fail_test("empty_start.wasm missing or unreadable for task find by id host API test", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&task_a, wasm_binary.bytes, wasm_binary.size, "app_main", "task_find_by_id_a", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task_a == 0)
    {
        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("task find by id host API create task_a");
        }
        free_binary(&wasm_binary);
        return fail_test("task find by id host API task_a creation failed", code_base + 2);
    }

    status = os_task_create(&task_b, wasm_binary.bytes, wasm_binary.size, "app_main", "task_find_by_id_b", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task_b == 0)
    {
        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("task find by id host API create task_b");
        }
        (void)os_task_delete(task_a);
        free_binary(&wasm_binary);
        return fail_test("task find by id host API task_b creation failed", code_base + 3);
    }

    id_a = os_task_get_id(task_a);
    id_b = os_task_get_id(task_b);
    unknown_id = id_b + 1000U;
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "task find by id host API initial ids a=%u b=%u unknown=%u", id_a, id_b, unknown_id);
    log_info(message);

    if (id_a == 0U || id_b == 0U || id_a == id_b ||
        os_task_find_by_id(id_a) != task_a || os_task_find_by_id(id_b) != task_b ||
        os_task_find_by_id(0U) != 0 || os_task_find_by_id(unknown_id) != 0 ||
        os_task_get_state(task_a) != OS_TASK_READY || os_task_get_state(task_b) != OS_TASK_READY ||
        os_get_task_count() != 2U || os_get_ready_task_count() != 2U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("task find by id host API initial checks");
        }
        (void)os_task_delete(task_a);
        (void)os_task_delete(task_b);
        free_binary(&wasm_binary);
        return fail_test("task find by id host API initial expectations failed", code_base + 4);
    }

    status = os_schedule();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "task find by id host API after schedule status=%s state_a=%s state_b=%s",
        os_status_name(status), os_task_state_name(os_task_get_state(task_a)), os_task_state_name(os_task_get_state(task_b)));
    log_info(message);

    if (status != OS_STATUS_OK ||
        os_task_get_state(task_a) != OS_TASK_DEAD || os_task_get_state(task_b) != OS_TASK_READY ||
        os_task_find_by_id(id_a) != task_a || os_task_find_by_id(id_b) != task_b ||
        os_get_task_count() != 1U || os_get_ready_task_count() != 1U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("task find by id host API after schedule");
        }
        (void)os_task_delete(task_a);
        (void)os_task_delete(task_b);
        free_binary(&wasm_binary);
        return fail_test("task find by id host API after schedule expectations failed", code_base + 5);
    }

    status = os_task_delete(task_a);
    if (status != OS_STATUS_OK)
    {
        (void)os_task_delete(task_b);
        free_binary(&wasm_binary);
        return fail_test("task find by id host API task_a cleanup failed", code_base + 6);
    }

    if (os_task_find_by_id(id_a) != 0 || os_task_find_by_id(id_b) != task_b ||
        os_get_task_count() != 1U || os_get_ready_task_count() != 1U || os_get_waiting_task_count() != 0U)
    {
        (void)os_task_delete(task_b);
        free_binary(&wasm_binary);
        return fail_test("task find by id host API post task_a delete expectations failed", code_base + 7);
    }

    status = os_task_delete(task_b);
    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        return fail_test("task find by id host API task_b cleanup failed", code_base + 8);
    }

    if (os_task_find_by_id(id_b) != 0)
    {
        free_binary(&wasm_binary);
        return fail_test("task find by id host API found task_b after delete", code_base + 9);
    }

    no_task_status = verify_no_tasks(code_base + 10);
    if (no_task_status != 0)
    {
        free_binary(&wasm_binary);
        return no_task_status;
    }

    log_pass("task find by id host API test final PASS");
    free_binary(&wasm_binary);
    return 0;
}

static int run_finite_wasm_app_test(
    const char* test_name,
    const char* wasm_path,
    const char* entry_function_name,
    int code_base
)
{
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    int no_task_status = 0;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int task_dead = 0;
    uint32_t iterations_used = 0U;
    OsTaskState final_state;

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM test start name=%s", test_name);
    log_phase(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file path: %s", wasm_path);
    log_info(message);

    if (!read_binary_file(wasm_path, &wasm_binary))
    {
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file load failure path=%s", wasm_path);
        log_fail(message);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s missing or unreadable WASM file: %s", test_name, wasm_path);
        return fail_test(message, TEST_FAILURE_MISSING_WASM);
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file load success path=%s", wasm_path);
    log_pass(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file byte size=%u", wasm_binary.size);
    log_info(message);

    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, entry_function_name, test_name, TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "os_task_create status=%s", os_status_name(status));
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "task handle result=%s", task == 0 ? "null" : "non-null");
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s after create", test_name);
    log_task_counters(message);

    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_task_create failed", test_name);
        return fail_test(message, code_base);
    }

    if (task == 0)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s returned a null task handle", test_name);
        return fail_test(message, code_base + 1);
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s schedule loop start max_iterations=%u", test_name, TEST_MAX_SCHEDULE_ITERATIONS);
    log_phase(message);

    final_state = os_task_get_state(task);
    for (uint32_t iteration = 0U; iteration < TEST_MAX_SCHEDULE_ITERATIONS; ++iteration)
    {
        status = os_schedule();
        final_state = os_task_get_state(task);
        iterations_used = iteration + 1U;

        if (iteration == 0U || status != OS_STATUS_OK || final_state == OS_TASK_DEAD)
        {
            format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s scheduler iteration=%u status=%s task_state=%s", test_name, iterations_used, os_status_name(status), os_task_state_name(final_state));
            log_info(message);
        }

        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
            free_binary(&wasm_binary);
            format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s returned OS_STATUS_WASM_ERROR", test_name);
            return fail_test(message, code_base + 2);
        }

        if (final_state == OS_TASK_DEAD)
        {
            task_dead = 1;
            break;
        }

        if (status == OS_STATUS_NO_READY_TASKS)
        {
            free_binary(&wasm_binary);
            format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s had no ready tasks before task death", test_name);
            return fail_test(message, code_base + 3);
        }

        if (status != OS_STATUS_OK)
        {
            free_binary(&wasm_binary);
            format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s returned an unexpected scheduler status", test_name);
            return fail_test(message, code_base + 4);
        }
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s final task state=%s", test_name, os_task_state_name(final_state));
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s scheduler iterations used=%u", test_name, iterations_used);
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s OS_TASK_DEAD reached=%s", test_name, task_dead ? "yes" : "no");
    log_info(message);

    if (!task_dead)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s did not finish before the schedule limit", test_name);
        return fail_test(message, code_base + 5);
    }

    status = os_schedule();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s post-death scheduler status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_NO_READY_TASKS)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s expected no ready tasks after task death", test_name);
        return fail_test(message, code_base + 6);
    }

    status = os_task_delete(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_task_delete status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_task_delete failed", test_name);
        return fail_test(message, code_base + 7);
    }

    no_task_status = verify_no_tasks(code_base + 8);
    if (no_task_status == 0)
    {
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s final", test_name);
        log_task_counters(message);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM test pass name=%s", test_name);
        log_pass(message);
    }

    free_binary(&wasm_binary);
    return no_task_status;
}

static int run_yield_once_wasm_app_test(const char* wasm_path, const char* entry_function_name, int code_base)
{
    const char* test_name = "yield_once.wasm";
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int resumed = 0;
    int task_dead = 0;
    uint32_t iterations_used = 0U;
    OsTaskState state_after_yield;
    OsTaskState final_state;
    int no_task_status = 0;

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM yield import test start name=%s", test_name);
    log_phase(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file path: %s", wasm_path);
    log_info(message);

    if (!read_binary_file(wasm_path, &wasm_binary))
    {
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file load failure path=%s", wasm_path);
        log_fail(message);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s missing or unreadable WASM file: %s", test_name, wasm_path);
        return fail_test(message, TEST_FAILURE_MISSING_WASM);
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file load success path=%s", wasm_path);
    log_pass(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file byte size=%u", wasm_binary.size);
    log_info(message);

    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, entry_function_name, test_name, TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "os_task_create status=%s", os_status_name(status));
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "task handle result=%s", task == 0 ? "null" : "non-null");
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s after create", test_name);
    log_task_counters(message);

    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_task_create failed", test_name);
        return fail_test(message, code_base);
    }

    if (task == 0)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s returned a null task handle", test_name);
        return fail_test(message, code_base + 1);
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s first scheduler call should hit env.os_yield", test_name);
    log_phase(message);
    status = os_schedule();
    state_after_yield = os_task_get_state(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s first scheduler status=%s task_state=%s", test_name, os_status_name(status), os_task_state_name(state_after_yield));
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s after first scheduler call", test_name);
    log_task_counters(message);

    if (status == OS_STATUS_WASM_ERROR)
    {
        log_last_os_error(test_name);
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s returned OS_STATUS_WASM_ERROR at yield", test_name);
        return fail_test(message, code_base + 2);
    }

    if (state_after_yield == OS_TASK_DEAD)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s became dead before it could resume", test_name);
        return fail_test(message, code_base + 3);
    }

    if (status != OS_STATUS_OK && status != OS_STATUS_NO_READY_TASKS)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s returned an unexpected first scheduler status", test_name);
        return fail_test(message, code_base + 4);
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s resume schedule loop start max_iterations=%u", test_name, TEST_MAX_SCHEDULE_ITERATIONS);
    log_phase(message);

    final_state = state_after_yield;
    for (uint32_t iteration = 0U; iteration < TEST_MAX_SCHEDULE_ITERATIONS; ++iteration)
    {
        status = os_schedule();
        final_state = os_task_get_state(task);
        iterations_used = iteration + 1U;

        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s resume iteration=%u status=%s task_state=%s", test_name, iterations_used, os_status_name(status), os_task_state_name(final_state));
        log_info(message);

        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
            free_binary(&wasm_binary);
            format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s returned OS_STATUS_WASM_ERROR after yield", test_name);
            return fail_test(message, code_base + 5);
        }

        if (final_state == OS_TASK_DEAD)
        {
            resumed = 1;
            task_dead = 1;
            break;
        }

        if (status == OS_STATUS_OK)
        {
            resumed = 1;
        }

        if (status == OS_STATUS_NO_READY_TASKS)
        {
            free_binary(&wasm_binary);
            format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s had no ready tasks before task death", test_name);
            return fail_test(message, code_base + 6);
        }

        if (status != OS_STATUS_OK)
        {
            free_binary(&wasm_binary);
            format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s returned an unexpected scheduler status after yield", test_name);
            return fail_test(message, code_base + 7);
        }
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s resumed=%s", test_name, resumed ? "yes" : "no");
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s final task state=%s", test_name, os_task_state_name(final_state));
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s scheduler iterations used after yield=%u", test_name, iterations_used);
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s OS_TASK_DEAD reached=%s", test_name, task_dead ? "yes" : "no");
    log_info(message);

    if (!resumed)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s did not resume after yielding", test_name);
        return fail_test(message, code_base + 8);
    }

    if (!task_dead)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s did not finish before the schedule limit", test_name);
        return fail_test(message, code_base + 9);
    }

    status = os_task_delete(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_task_delete status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_task_delete failed", test_name);
        return fail_test(message, code_base + 10);
    }

    no_task_status = verify_no_tasks(code_base + 11);
    if (no_task_status == 0)
    {
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s final", test_name);
        log_task_counters(message);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM yield import test pass name=%s", test_name);
        log_pass(message);
    }

    free_binary(&wasm_binary);
    return no_task_status;
}



static int run_wasi_exit_test(int code_base)
{
    const char* test_name = "wasi_exit.wasm";
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    OsTaskState state_after_schedule = OS_TASK_DEAD;
    uint32_t run_count_after_schedule = 0U;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int no_task_status = 0;

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASI exit test start name=%s", test_name);
    log_phase(message);
    log_info("WASM file path: build/wasi_exit.wasm");

    if (!read_binary_file("build/wasi_exit.wasm", &wasm_binary))
    {
        log_fail("WASM file load failure path=build/wasi_exit.wasm");
        return fail_test("wasi_exit.wasm missing or unreadable WASM file: build/wasi_exit.wasm", TEST_FAILURE_MISSING_WASM);
    }

    log_pass("WASM file load success path=build/wasi_exit.wasm");
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file byte size=%u", wasm_binary.size);
    log_info(message);

    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, "_start", "wasi_exit_task", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "os_task_create status=%s", os_status_name(status));
    log_info(message);
    log_task_counters("wasi_exit.wasm after create");

    if (status != OS_STATUS_OK || task == 0)
    {
        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
        }
        free_binary(&wasm_binary);
        return fail_test("wasi_exit.wasm os_task_create failed", code_base);
    }

    if (os_task_get_state(task) != OS_TASK_READY || os_get_task_count() != 1U ||
        os_get_ready_task_count() != 1U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_task_get_run_count(task) != 0U ||
        os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
        }
        free_binary(&wasm_binary);
        return fail_test("wasi_exit.wasm initial task expectations failed", code_base + 1);
    }

    log_phase("wasi_exit.wasm scheduler call should hit WASI exit(7)");
    status = os_schedule();
    state_after_schedule = os_task_get_state(task);
    run_count_after_schedule = os_task_get_run_count(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "wasi_exit.wasm schedule status=%s state=%s run_count=%u", os_status_name(status), os_task_state_name(state_after_schedule), run_count_after_schedule);
    log_info(message);
    log_task_counters("wasi_exit.wasm after schedule");

    if (status != OS_STATUS_OK || state_after_schedule != OS_TASK_DEAD ||
        run_count_after_schedule != 1U ||
        os_task_get_exit_reason(task) != OS_TASK_EXIT_EXPLICIT ||
        os_task_get_exit_code(task) != 7U || os_get_task_count() != 0U ||
        os_get_ready_task_count() != 0U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
        }
        free_binary(&wasm_binary);
        return fail_test("wasi_exit.wasm did not terminate cleanly after WASI exit", code_base + 2);
    }

    status = os_schedule();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "wasi_exit.wasm post-exit scheduler status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_NO_READY_TASKS || os_task_get_run_count(task) != 1U)
    {
        free_binary(&wasm_binary);
        return fail_test("wasi_exit.wasm continued scheduling after WASI exit", code_base + 3);
    }

    status = os_task_delete(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "wasi_exit.wasm delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD)
    {
        free_binary(&wasm_binary);
        return fail_test("wasi_exit.wasm cleanup delete failed", code_base + 4);
    }

    no_task_status = verify_no_tasks(code_base + 5);
    if (no_task_status == 0)
    {
        log_task_counters("wasi_exit.wasm final counters");
        log_pass("WASI exit test final PASS");
    }

    free_binary(&wasm_binary);
    return no_task_status;
}

static int run_wasm_exit_metadata_test(int code_base)
{
    TestBinary return_binary;
    TestBinary explicit_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    int no_task_status = 0;

    return_binary.bytes = 0;
    return_binary.size = 0U;
    explicit_binary.bytes = 0;
    explicit_binary.size = 0U;

    log_phase("exit metadata test start");

    if (!read_binary_file("build/empty_start.wasm", &return_binary))
    {
        log_fail("WASM file load failure path=build/empty_start.wasm");
        return fail_test("empty_start.wasm missing or unreadable for exit metadata test", TEST_FAILURE_MISSING_WASM);
    }

    if (!read_binary_file("build/wasi_exit.wasm", &explicit_binary))
    {
        free_binary(&return_binary);
        log_fail("WASM file load failure path=build/wasi_exit.wasm");
        return fail_test("wasi_exit.wasm missing or unreadable for exit metadata test", TEST_FAILURE_MISSING_WASM);
    }

    log_phase("normal return metadata case");
    no_task_status = verify_no_tasks(code_base);
    if (no_task_status != 0)
    {
        free_binary(&return_binary);
        free_binary(&explicit_binary);
        return no_task_status;
    }

    status = os_task_create(&task, return_binary.bytes, return_binary.size, "app_main", "exit_metadata_return_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task == 0)
    {
        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("normal return metadata create");
        }
        free_binary(&return_binary);
        free_binary(&explicit_binary);
        return fail_test("normal return metadata task creation failed", code_base + 1);
    }

    log_exit_metadata_task_snapshot("normal return metadata before schedule", task);
    if (os_task_get_state(task) != OS_TASK_READY || os_task_get_run_count(task) != 0U ||
        os_task_get_exit_reason(task) != OS_TASK_EXIT_NONE || os_task_get_exit_code(task) != 0U ||
        os_get_task_count() != 1U || os_get_ready_task_count() != 1U ||
        os_get_waiting_task_count() != 0U || os_task_get_current() != 0 ||
        os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        free_binary(&return_binary);
        free_binary(&explicit_binary);
        return fail_test("normal return metadata initial expectations failed", code_base + 2);
    }

    status = os_schedule();
    log_exit_metadata_task_snapshot("normal return metadata after schedule", task);
    if (status != OS_STATUS_OK || os_task_get_state(task) != OS_TASK_DEAD ||
        os_task_get_run_count(task) != 1U ||
        os_task_get_exit_reason(task) != OS_TASK_EXIT_RETURNED ||
        os_task_get_exit_code(task) != 0U || os_get_task_count() != 0U ||
        os_get_ready_task_count() != 0U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        free_binary(&return_binary);
        free_binary(&explicit_binary);
        return fail_test("normal return metadata after schedule expectations failed", code_base + 3);
    }

    log_phase("exit metadata cleanup normal return case");
    status = os_task_delete(task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD)
    {
        free_binary(&return_binary);
        free_binary(&explicit_binary);
        return fail_test("normal return metadata cleanup delete failed", code_base + 4);
    }

    no_task_status = verify_no_tasks(code_base + 5);
    if (no_task_status != 0)
    {
        free_binary(&return_binary);
        free_binary(&explicit_binary);
        return no_task_status;
    }

    task = 0;
    log_phase("explicit WASI exit metadata case");
    status = os_task_create(&task, explicit_binary.bytes, explicit_binary.size, "_start", "exit_metadata_explicit_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task == 0)
    {
        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("explicit WASI exit metadata create");
        }
        free_binary(&return_binary);
        free_binary(&explicit_binary);
        return fail_test("explicit WASI exit metadata task creation failed", code_base + 6);
    }

    log_exit_metadata_task_snapshot("explicit WASI exit metadata before schedule", task);
    if (os_task_get_state(task) != OS_TASK_READY || os_task_get_run_count(task) != 0U ||
        os_task_get_exit_reason(task) != OS_TASK_EXIT_NONE || os_task_get_exit_code(task) != 0U ||
        os_get_task_count() != 1U || os_get_ready_task_count() != 1U ||
        os_get_waiting_task_count() != 0U || os_task_get_current() != 0 ||
        os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        free_binary(&return_binary);
        free_binary(&explicit_binary);
        return fail_test("explicit WASI exit metadata initial expectations failed", code_base + 7);
    }

    status = os_schedule();
    log_exit_metadata_task_snapshot("explicit WASI exit metadata after schedule", task);
    if (status != OS_STATUS_OK || os_task_get_state(task) != OS_TASK_DEAD ||
        os_task_get_run_count(task) != 1U ||
        os_task_get_exit_reason(task) != OS_TASK_EXIT_EXPLICIT ||
        os_task_get_exit_code(task) != 7U || os_get_task_count() != 0U ||
        os_get_ready_task_count() != 0U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        free_binary(&return_binary);
        free_binary(&explicit_binary);
        return fail_test("explicit WASI exit metadata after schedule expectations failed", code_base + 8);
    }

    status = os_schedule();
    if (status != OS_STATUS_NO_READY_TASKS)
    {
        free_binary(&return_binary);
        free_binary(&explicit_binary);
        return fail_test("explicit WASI exit metadata post-exit scheduler did not report no ready tasks", code_base + 9);
    }

    log_phase("exit metadata cleanup explicit WASI exit case");
    status = os_task_delete(task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD)
    {
        free_binary(&return_binary);
        free_binary(&explicit_binary);
        return fail_test("explicit WASI exit metadata cleanup delete failed", code_base + 10);
    }

    no_task_status = verify_no_tasks(code_base + 11);
    if (no_task_status == 0)
    {
        log_pass("WASM exit metadata test final PASS");
    }

    free_binary(&return_binary);
    free_binary(&explicit_binary);
    return no_task_status;
}



static int run_wasm_entry_return_code_test(int code_base)
{
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    int no_task_status = 0;

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    log_phase("WASM entry return code test start");
    log_info("WASM file path: build/return_code.wasm");

    no_task_status = verify_no_tasks(code_base);
    if (no_task_status != 0)
    {
        return no_task_status;
    }

    if (!read_binary_file("build/return_code.wasm", &wasm_binary))
    {
        log_fail("WASM file load failure path=build/return_code.wasm");
        return fail_test("return_code.wasm missing or unreadable for entry return code test", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, "app_main", "entry_return_code_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task == 0)
    {
        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("entry return code create");
        }
        free_binary(&wasm_binary);
        return fail_test("entry return code task creation failed", code_base + 1);
    }

    log_exit_metadata_task_snapshot("entry return code before schedule", task);
    if (os_task_get_state(task) != OS_TASK_READY || os_task_get_run_count(task) != 0U ||
        os_task_get_exit_reason(task) != OS_TASK_EXIT_NONE || os_task_get_exit_code(task) != 0U ||
        os_get_task_count() != 1U || os_get_ready_task_count() != 1U ||
        os_get_waiting_task_count() != 0U || os_task_get_current() != 0 ||
        os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("entry return code before schedule");
        }
        free_binary(&wasm_binary);
        return fail_test("entry return code initial expectations failed", code_base + 2);
    }

    status = os_schedule();
    log_exit_metadata_task_snapshot("entry return code after schedule", task);
    if (status != OS_STATUS_OK || os_task_get_state(task) != OS_TASK_DEAD ||
        os_task_get_run_count(task) != 1U ||
        os_task_get_exit_reason(task) != OS_TASK_EXIT_RETURNED ||
        os_task_get_exit_code(task) != 42U || os_get_task_count() != 0U ||
        os_get_ready_task_count() != 0U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("entry return code after schedule");
        }
        free_binary(&wasm_binary);
        return fail_test("entry return code after schedule expectations failed", code_base + 3);
    }

    status = os_schedule();
    if (status != OS_STATUS_NO_READY_TASKS)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return code post-exit scheduler did not report no ready tasks", code_base + 4);
    }

    status = os_task_delete(task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return code cleanup delete failed", code_base + 5);
    }

    no_task_status = verify_no_tasks(code_base + 6);
    if (no_task_status == 0)
    {
        log_pass("WASM entry return code test final PASS");
    }

    free_binary(&wasm_binary);
    return no_task_status;
}

static int run_queue_api_wasm_test(int code_base)
{
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    int no_task_status = 0;

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    log_phase("queue API WASM test start");
    log_info("WASM file path: build/queue_api.wasm");

    no_task_status = verify_no_tasks(code_base);
    if (no_task_status != 0)
    {
        return no_task_status;
    }

    if (!read_binary_file("build/queue_api.wasm", &wasm_binary))
    {
        log_fail("WASM file load failure path=build/queue_api.wasm");
        return fail_test("queue_api.wasm missing or unreadable for queue API WASM test", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, "app_main", "queue_api_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task == 0)
    {
        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("queue API WASM create");
        }
        free_binary(&wasm_binary);
        return fail_test("queue API WASM task creation failed", code_base + 1);
    }

    log_exit_metadata_task_snapshot("queue API WASM before schedule", task);
    if (os_task_get_state(task) != OS_TASK_READY || os_task_get_run_count(task) != 0U ||
        os_task_get_exit_reason(task) != OS_TASK_EXIT_NONE || os_task_get_exit_code(task) != 0U ||
        os_get_task_count() != 1U || os_get_ready_task_count() != 1U ||
        os_get_waiting_task_count() != 0U || os_task_get_current() != 0 ||
        os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("queue API WASM before schedule");
        }
        free_binary(&wasm_binary);
        return fail_test("queue API WASM initial expectations failed", code_base + 2);
    }

    status = os_schedule();
    log_exit_metadata_task_snapshot("queue API WASM after schedule", task);
    if (status != OS_STATUS_OK || os_task_get_state(task) != OS_TASK_DEAD ||
        os_task_get_run_count(task) != 1U ||
        os_task_get_exit_reason(task) != OS_TASK_EXIT_RETURNED ||
        os_task_get_exit_code(task) != 0U || os_get_task_count() != 0U ||
        os_get_ready_task_count() != 0U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("queue API WASM after schedule");
        }
        free_binary(&wasm_binary);
        return fail_test("queue API WASM after schedule expectations failed", code_base + 3);
    }

    status = os_schedule();
    if (status != OS_STATUS_NO_READY_TASKS)
    {
        free_binary(&wasm_binary);
        return fail_test("queue API WASM post-exit scheduler did not report no ready tasks", code_base + 4);
    }

    status = os_task_delete(task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD)
    {
        free_binary(&wasm_binary);
        return fail_test("queue API WASM cleanup delete failed", code_base + 5);
    }

    no_task_status = verify_no_tasks(code_base + 6);
    if (no_task_status == 0)
    {
        log_pass("queue API WASM test final PASS");
    }

    free_binary(&wasm_binary);
    return no_task_status;
}

static int run_wasm_entry_return_values_test(int code_base)
{
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    OsValue return_value;
    OsValue return_values[4];
    int no_task_status = 0;
    static uint8_t multi_return_wasm[] = {
        0x00U, 0x61U, 0x73U, 0x6dU, 0x01U, 0x00U, 0x00U, 0x00U,
        0x01U, 0x08U, 0x01U, 0x60U, 0x00U, 0x04U, 0x7fU, 0x7eU, 0x7dU, 0x7cU,
        0x03U, 0x02U, 0x01U, 0x00U,
        0x07U, 0x12U, 0x01U, 0x0eU,
        0x61U, 0x70U, 0x70U, 0x5fU, 0x6dU, 0x61U, 0x69U, 0x6eU,
        0x5fU, 0x6dU, 0x75U, 0x6cU, 0x74U, 0x69U, 0x00U, 0x00U,
        0x0aU, 0x16U, 0x01U, 0x14U, 0x00U,
        0x41U, 0x07U,
        0x42U, 0x09U,
        0x43U, 0x00U, 0x00U, 0x60U, 0x40U,
        0x44U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x19U, 0x40U,
        0x0bU
    };
    uint32_t return_value_count = 0U;

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;
    memset(&return_value, 0, sizeof(return_value));
    memset(return_values, 0, sizeof(return_values));

    log_phase("WASM entry return values test start");
    log_info("WASM file path: build/return_values.wasm");

    no_task_status = verify_no_tasks(code_base);
    if (no_task_status != 0)
    {
        return no_task_status;
    }

    if (!read_binary_file("build/return_values.wasm", &wasm_binary))
    {
        log_fail("WASM file load failure path=build/return_values.wasm");
        return fail_test("return_values.wasm missing or unreadable for entry return values test", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, "app_main_i32", "entry_return_i32_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return i32 task creation failed", code_base + 1);
    }

    status = os_schedule();
    if (status != OS_STATUS_OK || os_task_get_return_value(task, &return_value) != OS_STATUS_OK ||
        os_task_get_return_value_count(task) != 1U ||
        return_value.type != OS_VALUE_TYPE_I32 || return_value.value.i32 != 42U ||
        os_task_get_exit_code(task) != 42U)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return i32 value expectations failed", code_base + 2);
    }

    status = os_task_delete(task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return i32 cleanup failed", code_base + 3);
    }

    task = 0;
    memset(&return_value, 0, sizeof(return_value));
    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, "app_main_i64", "entry_return_i64_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return i64 task creation failed", code_base + 4);
    }

    status = os_schedule();
    if (status != OS_STATUS_OK || os_task_get_return_value(task, &return_value) != OS_STATUS_OK ||
        os_task_get_return_value_count(task) != 1U ||
        return_value.type != OS_VALUE_TYPE_I64 || return_value.value.i64 != 0x1122334455667788ULL ||
        os_task_get_exit_code(task) != 0U)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return i64 value expectations failed", code_base + 5);
    }

    status = os_task_delete(task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return i64 cleanup failed", code_base + 6);
    }

    task = 0;
    memset(&return_value, 0, sizeof(return_value));
    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, "app_main_f32", "entry_return_f32_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return f32 task creation failed", code_base + 7);
    }

    status = os_schedule();
    if (status != OS_STATUS_OK || os_task_get_return_value(task, &return_value) != OS_STATUS_OK ||
        os_task_get_return_value_count(task) != 1U ||
        return_value.type != OS_VALUE_TYPE_F32 || return_value.value.f32 != 12.5f ||
        os_task_get_exit_code(task) != 0U)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return f32 value expectations failed", code_base + 8);
    }

    status = os_task_delete(task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return f32 cleanup failed", code_base + 9);
    }

    task = 0;
    memset(&return_value, 0, sizeof(return_value));
    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, "app_main_f64", "entry_return_f64_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return f64 task creation failed", code_base + 10);
    }

    status = os_schedule();
    if (status != OS_STATUS_OK || os_task_get_return_value(task, &return_value) != OS_STATUS_OK ||
        os_task_get_return_value_count(task) != 1U ||
        return_value.type != OS_VALUE_TYPE_F64 || return_value.value.f64 != 123.25 ||
        os_task_get_exit_code(task) != 0U)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return f64 value expectations failed", code_base + 11);
    }

    status = os_task_delete(task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return f64 cleanup failed", code_base + 12);
    }

    task = 0;
    memset(return_values, 0, sizeof(return_values));
    status = os_task_create(&task, multi_return_wasm, (uint32_t)sizeof(multi_return_wasm), "app_main_multi", "entry_return_multi_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return multi-value task creation failed", code_base + 13);
    }

    status = os_schedule();
    if (status != OS_STATUS_OK ||
        os_task_get_return_value_count(task) != 4U ||
        os_task_get_return_value(task, &return_value) != OS_STATUS_BUFFER_TOO_SMALL ||
        os_task_get_return_values(task, return_values, 2U, &return_value_count) != OS_STATUS_BUFFER_TOO_SMALL ||
        return_value_count != 4U ||
        os_task_get_return_values(task, return_values, 4U, &return_value_count) != OS_STATUS_OK ||
        return_value_count != 4U ||
        return_values[0].type != OS_VALUE_TYPE_I32 || return_values[0].value.i32 != 7U ||
        return_values[1].type != OS_VALUE_TYPE_I64 || return_values[1].value.i64 != 9ULL ||
        return_values[2].type != OS_VALUE_TYPE_F32 || return_values[2].value.f32 != 3.5f ||
        return_values[3].type != OS_VALUE_TYPE_F64 || return_values[3].value.f64 != 6.25 ||
        os_task_get_exit_code(task) != 7U)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return multi-value expectations failed", code_base + 14);
    }

    status = os_task_delete(task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD)
    {
        free_binary(&wasm_binary);
        return fail_test("entry return multi-value cleanup failed", code_base + 15);
    }

    no_task_status = verify_no_tasks(code_base + 16);
    if (no_task_status == 0)
    {
        log_pass("WASM entry return values test final PASS");
    }

    free_binary(&wasm_binary);
    return no_task_status;
}


static int run_task_entry_mixed_scalar_args_test(int code_base)
{
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    OsValue entry_args[4];
    int no_task_status = 0;

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    log_phase("task entry mixed scalar args test start");
    log_info("WASM file path: build/entry_args_mixed_scalar.wasm");

    status = os_init();
    if (status != OS_STATUS_OK)
    {
        return fail_test("entry mixed scalar args os_init failed", code_base + 1);
    }

    no_task_status = verify_no_tasks(code_base + 2);
    if (no_task_status != 0)
    {
        return no_task_status;
    }

    if (!read_binary_file("build/entry_args_mixed_scalar.wasm", &wasm_binary))
    {
        log_fail("WASM file load failure path=build/entry_args_mixed_scalar.wasm");
        return fail_test("entry_args_mixed_scalar.wasm missing or unreadable for mixed scalar args test", TEST_FAILURE_MISSING_WASM);
    }

    entry_args[0].type = OS_VALUE_TYPE_I32;
    entry_args[0].value.i32 = 10U;
    entry_args[1].type = OS_VALUE_TYPE_F32;
    entry_args[1].value.f32 = 3.0f;
    entry_args[2].type = OS_VALUE_TYPE_I64;
    entry_args[2].value.i64 = 20ULL;
    entry_args[3].type = OS_VALUE_TYPE_F64;
    entry_args[3].value.f64 = 4.0;

    status = os_task_create_with_args(
        &task,
        wasm_binary.bytes,
        wasm_binary.size,
        "app_main",
        entry_args,
        4U,
        "entry_mixed_scalar_args_task",
        TEST_WASM_STACK_SIZE,
        OS_TASK_PRIORITY_NORMAL
    );
    if (status != OS_STATUS_OK || task == 0)
    {
        if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() != OS_STATUS_OK)
        {
            log_last_os_error("entry mixed scalar args create");
        }
        free_binary(&wasm_binary);
        return fail_test("entry mixed scalar args task creation failed", code_base + 3);
    }

    status = os_schedule();
    log_exit_metadata_task_snapshot("entry mixed scalar args after schedule", task);
    if (status != OS_STATUS_OK || os_task_get_state(task) != OS_TASK_DEAD ||
        os_task_get_exit_reason(task) != OS_TASK_EXIT_RETURNED ||
        os_task_get_exit_code(task) != 37U)
    {
        if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() != OS_STATUS_OK)
        {
            log_last_os_error("entry mixed scalar args after schedule");
        }
        free_binary(&wasm_binary);
        return fail_test("entry mixed scalar args after schedule expectations failed", code_base + 4);
    }

    status = os_task_delete(task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD)
    {
        free_binary(&wasm_binary);
        return fail_test("entry mixed scalar args cleanup delete failed", code_base + 5);
    }

    no_task_status = verify_no_tasks(code_base + 6);
    if (no_task_status == 0)
    {
        log_pass("task entry mixed scalar args final PASS");
    }

    free_binary(&wasm_binary);
    return no_task_status;
}


static int verify_failed_arg_create_clean(const char* context, OsTaskHandle task, int code_base)
{
    char message[TEST_MESSAGE_BUFFER_SIZE];

    format_message(
        message,
        TEST_MESSAGE_BUFFER_SIZE,
        "%s failed create handle=%s",
        context,
        task == 0 ? "null" : "non-null"
    );
    log_info(message);
    log_task_counters(context);
    log_last_os_error(context);

    if (task != 0)
    {
        return fail_test("entry arg validation expected null task handle", code_base);
    }

    return verify_no_tasks(code_base + 1);
}

static int reset_os_for_arg_validation_negative(const char* context, int code_base)
{
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    log_phase(context);
    os_shutdown();
    status = os_init();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_init status=%s", context, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        return fail_test("entry arg validation negative os_init failed", code_base);
    }

    return verify_no_tasks(code_base + 1);
}

static int run_task_entry_arg_validation_negative_test(int code_base)
{
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    OsValue entry_args[4];
    const char* phase = 0;
    int subcase_status = 0;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    log_phase("task entry arg validation negative test start");
    log_info("WASM file path: build/entry_args_mixed_scalar.wasm");

    status = os_init();
    if (status != OS_STATUS_OK)
    {
        return fail_test("entry arg validation negative os_init failed", code_base + 1);
    }

    subcase_status = verify_no_tasks(code_base + 2);
    if (subcase_status != 0)
    {
        return subcase_status;
    }

    if (!read_binary_file("build/entry_args_mixed_scalar.wasm", &wasm_binary))
    {
        log_fail("WASM file load failure path=build/entry_args_mixed_scalar.wasm");
        return fail_test("entry_args_mixed_scalar.wasm missing or unreadable for arg validation negative test", TEST_FAILURE_MISSING_WASM);
    }

    log_phase("entry arg validation negative null args subcase");
    task = 0;
    status = os_task_create_with_args(
        &task,
        wasm_binary.bytes,
        wasm_binary.size,
        "app_main",
        0,
        1U,
        "entry_arg_null_args_task",
        TEST_WASM_STACK_SIZE,
        OS_TASK_PRIORITY_NORMAL
    );
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "null args subcase create status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_INVALID_ARGUMENT)
    {
        free_binary(&wasm_binary);
        return fail_test("entry arg validation null args expected invalid argument", code_base + 3);
    }
    subcase_status = verify_failed_arg_create_clean("entry arg validation null args", task, code_base + 4);
    if (subcase_status != 0)
    {
        free_binary(&wasm_binary);
        return subcase_status;
    }

    subcase_status = reset_os_for_arg_validation_negative("entry arg validation reset before count mismatch", code_base + 10);
    if (subcase_status != 0)
    {
        free_binary(&wasm_binary);
        return subcase_status;
    }

    entry_args[0].type = OS_VALUE_TYPE_I32;
    entry_args[0].value.i32 = 10U;
    entry_args[1].type = OS_VALUE_TYPE_F32;
    entry_args[1].value.f32 = 3.0f;
    entry_args[2].type = OS_VALUE_TYPE_I64;
    entry_args[2].value.i64 = 20ULL;
    entry_args[3].type = OS_VALUE_TYPE_F64;
    entry_args[3].value.f64 = 4.0;

    log_phase("entry arg validation negative count mismatch subcase");
    task = 0;
    status = os_task_create_with_args(
        &task,
        wasm_binary.bytes,
        wasm_binary.size,
        "app_main",
        entry_args,
        3U,
        "entry_arg_count_mismatch_task",
        TEST_WASM_STACK_SIZE,
        OS_TASK_PRIORITY_NORMAL
    );
    phase = os_get_last_error_phase();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "count mismatch subcase create status=%s phase=%s", os_status_name(status), phase == 0 ? "null" : phase);
    log_info(message);
    if (status != OS_STATUS_INVALID_ARGUMENT)
    {
        free_binary(&wasm_binary);
        return fail_test("entry arg validation count mismatch expected invalid argument", code_base + 20);
    }
    if (phase == 0 || strcmp(phase, "validate_entry_signature") != 0)
    {
        free_binary(&wasm_binary);
        return fail_test("entry arg validation count mismatch expected validate_entry_signature phase", code_base + 21);
    }
    subcase_status = verify_failed_arg_create_clean("entry arg validation count mismatch", task, code_base + 22);
    if (subcase_status != 0)
    {
        free_binary(&wasm_binary);
        return subcase_status;
    }

    subcase_status = reset_os_for_arg_validation_negative("entry arg validation reset before type mismatch", code_base + 30);
    if (subcase_status != 0)
    {
        free_binary(&wasm_binary);
        return subcase_status;
    }

    entry_args[1].type = OS_VALUE_TYPE_I64;
    entry_args[1].value.i64 = 30ULL;

    log_phase("entry arg validation negative type mismatch subcase");
    task = 0;
    status = os_task_create_with_args(
        &task,
        wasm_binary.bytes,
        wasm_binary.size,
        "app_main",
        entry_args,
        4U,
        "entry_arg_type_mismatch_task",
        TEST_WASM_STACK_SIZE,
        OS_TASK_PRIORITY_NORMAL
    );
    phase = os_get_last_error_phase();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "type mismatch subcase create status=%s phase=%s", os_status_name(status), phase == 0 ? "null" : phase);
    log_info(message);
    if (status != OS_STATUS_INVALID_ARGUMENT)
    {
        free_binary(&wasm_binary);
        return fail_test("entry arg validation type mismatch expected invalid argument", code_base + 40);
    }
    if (phase == 0 || strcmp(phase, "validate_entry_signature") != 0)
    {
        free_binary(&wasm_binary);
        return fail_test("entry arg validation type mismatch expected validate_entry_signature phase", code_base + 41);
    }
    subcase_status = verify_failed_arg_create_clean("entry arg validation type mismatch", task, code_base + 42);
    if (subcase_status != 0)
    {
        free_binary(&wasm_binary);
        return subcase_status;
    }

    free_binary(&wasm_binary);
    log_pass("task entry arg validation negative final PASS");
    return 0;
}

static int run_wasm_get_time_host_import_test(int code_base)
{
    const char* test_name = "get_time_once.wasm";
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    OsTaskState state_after_schedule = OS_TASK_DEAD;
    uint32_t run_count_after_schedule = 0U;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int no_task_status = 0;

    log_phase("WASM get-time import test start name=get_time_once.wasm");
    log_info("WASM file path: build/get_time_once.wasm");

    status = os_init();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "get_time_once.wasm os_init result=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        return fail_test("get_time_once.wasm os_init failed", code_base);
    }

    os_tick(1234U);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "get_time_once.wasm deterministic tick=%u", os_get_tick_ms());
    log_info(message);

    if (!read_binary_file("build/get_time_once.wasm", &wasm_binary))
    {
        log_fail("WASM file load failure path=build/get_time_once.wasm");
        return fail_test("get_time_once.wasm missing or unreadable WASM file: build/get_time_once.wasm", TEST_FAILURE_MISSING_WASM);
    }

    log_pass("WASM file load success path=build/get_time_once.wasm");
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file byte size=%u", wasm_binary.size);
    log_info(message);

    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, "app_main", "get_time_once_task", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "os_task_create status=%s", os_status_name(status));
    log_info(message);
    log_task_counters("get_time_once.wasm after create");

    if (status != OS_STATUS_OK || task == 0)
    {
        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
        }
        free_binary(&wasm_binary);
        return fail_test("get_time_once.wasm os_task_create failed", code_base + 1);
    }

    if (os_task_get_state(task) != OS_TASK_READY || os_get_task_count() != 1U ||
        os_get_ready_task_count() != 1U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_task_get_run_count(task) != 0U ||
        os_get_tick_ms() != 1234U || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
        }
        free_binary(&wasm_binary);
        return fail_test("get_time_once.wasm initial task expectations failed", code_base + 2);
    }

    log_phase("get_time_once.wasm scheduler call should read env.os_get_time_ms and return");
    status = os_schedule();
    state_after_schedule = os_task_get_state(task);
    run_count_after_schedule = os_task_get_run_count(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "get_time_once.wasm schedule status=%s state=%s run_count=%u tick=%u", os_status_name(status), os_task_state_name(state_after_schedule), run_count_after_schedule, os_get_tick_ms());
    log_info(message);
    log_task_counters("get_time_once.wasm after schedule");

    if (status != OS_STATUS_OK || state_after_schedule != OS_TASK_DEAD ||
        run_count_after_schedule != 1U || os_get_task_count() != 0U ||
        os_get_ready_task_count() != 0U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_get_tick_ms() != 1234U ||
        os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
        }
        free_binary(&wasm_binary);
        return fail_test("get_time_once.wasm did not read the expected OS tick and return cleanly", code_base + 3);
    }

    status = os_schedule();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "get_time_once.wasm post-exit scheduler status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_NO_READY_TASKS || os_task_get_run_count(task) != 1U)
    {
        free_binary(&wasm_binary);
        return fail_test("get_time_once.wasm continued scheduling after return", code_base + 4);
    }

    status = os_task_delete(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "get_time_once.wasm delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD)
    {
        free_binary(&wasm_binary);
        return fail_test("get_time_once.wasm cleanup delete failed", code_base + 5);
    }

    no_task_status = verify_no_tasks(code_base + 6);
    if (no_task_status == 0)
    {
        log_task_counters("get_time_once.wasm final counters");
        log_pass("WASM get-time import test final PASS");
    }

    free_binary(&wasm_binary);
    return no_task_status;
}

static uint32_t g_custom_host_import_call_count = 0U;

static m3ApiRawFunction(smoke_host_test_value)
{
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, input);
    (void)runtime;
    (void)_ctx;
    (void)_mem;

    ++g_custom_host_import_call_count;
    m3ApiReturn(input + 42U);
}

static int run_custom_host_import_registration_test(int code_base)
{
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    int no_task_status = 0;

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;
    g_custom_host_import_call_count = 0U;

    log_phase("Custom host import registration test start");
    log_info("WASM file path: build/custom_import.wasm");

    os_shutdown();
    status = os_host_import_register("env", "host_test_value", "i(i)", smoke_host_test_value);
    if (status != OS_STATUS_ERROR)
    {
        return fail_test("custom host import registration before os_init did not fail", code_base);
    }

    status = os_init();
    if (status != OS_STATUS_OK)
    {
        return fail_test("custom host import registration os_init failed", code_base + 1);
    }

    status = os_host_import_register("env", "host_test_value", "i(i)", smoke_host_test_value);
    if (status != OS_STATUS_OK)
    {
        return fail_test("custom host import registration failed", code_base + 2);
    }

    status = os_host_import_register("env", "host_test_value", "i(i)", smoke_host_test_value);
    if (status != OS_STATUS_INVALID_ARGUMENT)
    {
        return fail_test("custom host import duplicate registration did not fail", code_base + 3);
    }

    if (!read_binary_file("build/custom_import.wasm", &wasm_binary))
    {
        log_fail("WASM file load failure path=build/custom_import.wasm");
        return fail_test("custom_import.wasm missing or unreadable WASM file: build/custom_import.wasm", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(
        &task,
        wasm_binary.bytes,
        wasm_binary.size,
        "app_main",
        "custom_host_import_task",
        TEST_WASM_STACK_SIZE,
        OS_TASK_PRIORITY_NORMAL
    );
    if (status != OS_STATUS_OK || task == 0)
    {
        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("custom host import create");
        }
        free_binary(&wasm_binary);
        return fail_test("custom host import task creation failed", code_base + 4);
    }

    status = os_schedule();
    if (status != OS_STATUS_OK || os_task_get_state(task) != OS_TASK_DEAD ||
        os_task_get_exit_reason(task) != OS_TASK_EXIT_RETURNED ||
        os_task_get_exit_code(task) != 0U ||
        g_custom_host_import_call_count != 1U)
    {
        if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("custom host import schedule");
        }
        free_binary(&wasm_binary);
        return fail_test("custom host import task did not return expected result", code_base + 5);
    }

    status = os_task_delete(task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD)
    {
        free_binary(&wasm_binary);
        return fail_test("custom host import task cleanup failed", code_base + 6);
    }

    os_host_import_clear_all();

    no_task_status = verify_no_tasks(code_base + 7);
    if (no_task_status == 0)
    {
        log_pass("Custom host import registration test final PASS");
    }

    free_binary(&wasm_binary);
    return no_task_status;
}

static int run_delay_once_wasm_app_test(const char* wasm_path, const char* entry_function_name, int code_base)
{
    const char* test_name = "delay_once.wasm";
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    OsTaskState state_after_delay;
    OsTaskState state_before_wake;
    OsTaskState state_after_wake;
    OsTaskState final_state;
    uint32_t resume_iterations = 0U;
    int task_dead = 0;
    int no_task_status = 0;

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM delay import test start name=%s", test_name);
    log_phase(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file path: %s", wasm_path);
    log_info(message);

    if (!read_binary_file(wasm_path, &wasm_binary))
    {
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file load failure path=%s", wasm_path);
        log_fail(message);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s missing or unreadable WASM file: %s", test_name, wasm_path);
        return fail_test(message, TEST_FAILURE_MISSING_WASM);
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file load success path=%s", wasm_path);
    log_pass(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file byte size=%u", wasm_binary.size);
    log_info(message);

    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, entry_function_name, test_name, TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "os_task_create status=%s", os_status_name(status));
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "task handle result=%s", task == 0 ? "null" : "non-null");
    log_info(message);

    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_task_create failed", test_name);
        return fail_test(message, code_base);
    }

    if (task == 0)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s returned a null task handle", test_name);
        return fail_test(message, code_base + 1);
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s tick count before delay=%u", test_name, os_get_tick_ms());
    log_info(message);
    log_phase("delay_once.wasm first scheduler call should hit env.os_delay_ms(10)");
    status = os_schedule();
    state_after_delay = os_task_get_state(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s state after first schedule=%s status=%s", test_name, os_task_state_name(state_after_delay), os_status_name(status));
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s counters after delay", test_name);
    log_task_counters(message);

    if (status == OS_STATUS_WASM_ERROR)
    {
        log_last_os_error(test_name);
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s returned OS_STATUS_WASM_ERROR at delay", test_name);
        return fail_test(message, code_base + 2);
    }

    if (state_after_delay == OS_TASK_DEAD)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s became dead before the wake tick", test_name);
        return fail_test(message, code_base + 3);
    }

    if (state_after_delay != OS_TASK_WAITING || os_get_waiting_task_count() != 1U || os_get_ready_task_count() != 0U)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s did not enter the expected waiting state", test_name);
        return fail_test(message, code_base + 4);
    }

    os_tick(9U);
    state_before_wake = os_task_get_state(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s tick count after +9 ms=%u state=%s", test_name, os_get_tick_ms(), os_task_state_name(state_before_wake));
    log_info(message);

    if (state_before_wake == OS_TASK_DEAD)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s became dead before the wake tick after +9 ms", test_name);
        return fail_test(message, code_base + 5);
    }

    status = os_schedule();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s status before wake=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_NO_READY_TASKS)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s expected no ready tasks before wake", test_name);
        return fail_test(message, code_base + 6);
    }

    os_tick(1U);
    state_after_wake = os_task_get_state(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s tick count after +1 ms=%u state=%s", test_name, os_get_tick_ms(), os_task_state_name(state_after_wake));
    log_info(message);

    if (state_after_wake != OS_TASK_READY)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s did not become ready at the wake tick", test_name);
        return fail_test(message, code_base + 7);
    }

    for (uint32_t iteration = 0U; iteration < TEST_MAX_SCHEDULE_ITERATIONS; ++iteration)
    {
        status = os_schedule();
        final_state = os_task_get_state(task);
        resume_iterations = iteration + 1U;
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s resume iteration=%u status=%s task_state=%s", test_name, resume_iterations, os_status_name(status), os_task_state_name(final_state));
        log_info(message);

        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
            free_binary(&wasm_binary);
            format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s returned OS_STATUS_WASM_ERROR after wake", test_name);
            return fail_test(message, code_base + 8);
        }

        if (final_state == OS_TASK_DEAD)
        {
            task_dead = 1;
            break;
        }

        if (status == OS_STATUS_NO_READY_TASKS)
        {
            free_binary(&wasm_binary);
            format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s had no ready tasks after wake before task death", test_name);
            return fail_test(message, code_base + 9);
        }

        if (status != OS_STATUS_OK)
        {
            free_binary(&wasm_binary);
            format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s returned an unexpected scheduler status after wake", test_name);
            return fail_test(message, code_base + 10);
        }
    }

    final_state = os_task_get_state(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s resume iterations=%u", test_name, resume_iterations);
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s final task state=%s", test_name, os_task_state_name(final_state));
    log_info(message);

    if (!task_dead)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s never reached dead after wake", test_name);
        return fail_test(message, code_base + 11);
    }

    status = os_task_delete(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s delete status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_task_delete failed", test_name);
        return fail_test(message, code_base + 12);
    }

    no_task_status = verify_no_tasks(code_base + 13);
    if (no_task_status == 0)
    {
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s final counters", test_name);
        log_task_counters(message);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM delay import test pass name=%s", test_name);
        log_pass(message);
    }

    free_binary(&wasm_binary);
    return no_task_status;
}



static int run_multitask_delay_fairness_test(int code_base)
{
    const char* test_name = "multitask delay fairness";
    TestBinary delay_binary;
    TestBinary simple_binary;
    OsTaskHandle delay_task = 0;
    OsTaskHandle simple_task = 0;
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    uint32_t simple_iterations = 0U;
    uint32_t delay_iterations = 0U;
    int simple_task_dead = 0;
    int delay_task_dead = 0;
    OsTaskState delay_state;
    OsTaskState simple_state;
    int no_task_status = 0;

    delay_binary.bytes = 0;
    delay_binary.size = 0U;
    simple_binary.bytes = 0;
    simple_binary.size = 0U;

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s test start", test_name);
    log_phase(message);
    log_info("delay task fixture: build/delay_once.wasm");
    log_info("simple task fixture: build/simple_loop.wasm");

    if (!read_binary_file("build/delay_once.wasm", &delay_binary))
    {
        log_fail("multitask delay fairness could not load delay_once.wasm");
        return fail_test("delay_once.wasm missing or unreadable for multitask delay fairness", TEST_FAILURE_MISSING_WASM);
    }

    if (!read_binary_file("build/simple_loop.wasm", &simple_binary))
    {
        free_binary(&delay_binary);
        log_fail("multitask delay fairness could not load simple_loop.wasm");
        return fail_test("simple_loop.wasm missing or unreadable for multitask delay fairness", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&delay_task, delay_binary.bytes, delay_binary.size, "app_main", "delay_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_HIGH);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "delay_task os_task_create status=%s handle=%s", os_status_name(status), delay_task == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || delay_task == 0)
    {
        free_binary(&simple_binary);
        free_binary(&delay_binary);
        return fail_test("multitask delay fairness failed to create delay_task", code_base);
    }

    status = os_task_create(&simple_task, simple_binary.bytes, simple_binary.size, "app_main", "simple_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "simple_task os_task_create status=%s handle=%s", os_status_name(status), simple_task == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || simple_task == 0)
    {
        free_binary(&simple_binary);
        free_binary(&delay_binary);
        return fail_test("multitask delay fairness failed to create simple_task", code_base + 1);
    }

    log_pass("multitask delay fairness created delay_task and simple_task");
    log_task_counters("multitask delay fairness after create");

    log_phase("multitask delay fairness first scheduler call should run high-priority delay_task into OS_TASK_WAITING");
    status = os_schedule();
    delay_state = os_task_get_state(delay_task);
    simple_state = os_task_get_state(simple_task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "first scheduler status=%s delay_task=%s simple_task=%s", os_status_name(status), os_task_state_name(delay_state), os_task_state_name(simple_state));
    log_info(message);
    log_task_counters("multitask delay fairness after delay_task delay");

    if (status == OS_STATUS_WASM_ERROR)
    {
        log_last_os_error(test_name);
        free_binary(&simple_binary);
        free_binary(&delay_binary);
        return fail_test("multitask delay fairness hit WASM error during first schedule", code_base + 2);
    }

    if (delay_state != OS_TASK_WAITING || simple_state != OS_TASK_READY || os_get_ready_task_count() < 1U || os_get_waiting_task_count() != 1U)
    {
        free_binary(&simple_binary);
        free_binary(&delay_binary);
        return fail_test("multitask delay fairness did not enter expected waiting/ready counters", code_base + 3);
    }
    log_pass("delay_task entered OS_TASK_WAITING while simple_task remained OS_TASK_READY");

    log_phase("multitask delay fairness running scheduler without waking delay_task until simple_task reaches OS_TASK_DEAD");
    for (uint32_t iteration = 0U; iteration < TEST_MAX_SCHEDULE_ITERATIONS; ++iteration)
    {
        status = os_schedule();
        delay_state = os_task_get_state(delay_task);
        simple_state = os_task_get_state(simple_task);
        simple_iterations = iteration + 1U;

        if (iteration == 0U || simple_state == OS_TASK_DEAD || status != OS_STATUS_OK)
        {
            format_message(message, TEST_MESSAGE_BUFFER_SIZE, "simple phase iteration=%u status=%s delay_task=%s simple_task=%s", simple_iterations, os_status_name(status), os_task_state_name(delay_state), os_task_state_name(simple_state));
            log_info(message);
        }

        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
            free_binary(&simple_binary);
            free_binary(&delay_binary);
            return fail_test("multitask delay fairness hit WASM error while simple_task ran", code_base + 4);
        }

        if (delay_state != OS_TASK_WAITING)
        {
            free_binary(&simple_binary);
            free_binary(&delay_binary);
            return fail_test("delay_task stopped waiting before tick advancement", code_base + 5);
        }

        if (simple_state == OS_TASK_DEAD)
        {
            simple_task_dead = 1;
            break;
        }

        if (status != OS_STATUS_OK)
        {
            free_binary(&simple_binary);
            free_binary(&delay_binary);
            return fail_test("unexpected scheduler status while simple_task should be runnable", code_base + 6);
        }
    }

    if (!simple_task_dead)
    {
        free_binary(&simple_binary);
        free_binary(&delay_binary);
        return fail_test("simple_task did not reach OS_TASK_DEAD while delay_task was waiting", code_base + 7);
    }

    log_pass("simple_task ran to OS_TASK_DEAD while delay_task was still OS_TASK_WAITING");
    status = os_task_delete(simple_task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "simple_task delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&simple_binary);
        free_binary(&delay_binary);
        return fail_test("multitask delay fairness failed to delete simple_task", code_base + 8);
    }
    simple_task = 0;

    os_tick(10U);
    delay_state = os_task_get_state(delay_task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "after +10 ms tick delay_task state=%s tick=%u", os_task_state_name(delay_state), os_get_tick_ms());
    log_info(message);
    if (delay_state != OS_TASK_READY)
    {
        free_binary(&simple_binary);
        free_binary(&delay_binary);
        return fail_test("delay_task did not become OS_TASK_READY after wake tick advancement", code_base + 9);
    }
    log_pass("delay_task woke after tick advancement");

    log_phase("multitask delay fairness running woken delay_task until OS_TASK_DEAD");
    for (uint32_t iteration = 0U; iteration < TEST_MAX_SCHEDULE_ITERATIONS; ++iteration)
    {
        status = os_schedule();
        delay_state = os_task_get_state(delay_task);
        delay_iterations = iteration + 1U;
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "delay phase iteration=%u status=%s delay_task=%s", delay_iterations, os_status_name(status), os_task_state_name(delay_state));
        log_info(message);

        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
            free_binary(&simple_binary);
            free_binary(&delay_binary);
            return fail_test("multitask delay fairness hit WASM error after delay_task wake", code_base + 10);
        }

        if (delay_state == OS_TASK_DEAD)
        {
            delay_task_dead = 1;
            break;
        }

        if (status == OS_STATUS_NO_READY_TASKS)
        {
            free_binary(&simple_binary);
            free_binary(&delay_binary);
            return fail_test("delay_task had no ready task before reaching OS_TASK_DEAD", code_base + 11);
        }

        if (status != OS_STATUS_OK)
        {
            free_binary(&simple_binary);
            free_binary(&delay_binary);
            return fail_test("unexpected scheduler status while delay_task resumed", code_base + 12);
        }
    }

    if (!delay_task_dead)
    {
        free_binary(&simple_binary);
        free_binary(&delay_binary);
        return fail_test("delay_task did not reach OS_TASK_DEAD after wake", code_base + 13);
    }
    log_pass("delay_task reached OS_TASK_DEAD after wake");

    status = os_task_delete(delay_task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "delay_task delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&simple_binary);
        free_binary(&delay_binary);
        return fail_test("multitask delay fairness failed to delete delay_task", code_base + 14);
    }

    no_task_status = verify_no_tasks(code_base + 15);
    if (no_task_status == 0)
    {
        log_task_counters("multitask delay fairness final counters");
        log_pass("multitask delay fairness both tasks reached OS_TASK_DEAD and final counters are clean");
    }

    free_binary(&simple_binary);
    free_binary(&delay_binary);
    return no_task_status;
}



static int run_absolute_time_update_test(int code_base)
{
    const char* test_name = "absolute time update";
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    uint32_t anchor = 0U;
    OsTaskState state_after_delay;
    OsTaskState state_before_wake;
    OsTaskState state_after_wake;
    OsTaskState final_state;
    uint32_t resume_iterations = 0U;
    int task_dead = 0;
    int no_task_status = 0;

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s test start", test_name);
    log_phase(message);
    log_info("absolute time fixture: build/delay_once.wasm");

    if (!read_binary_file("build/delay_once.wasm", &wasm_binary))
    {
        log_fail("absolute time update could not load delay_once.wasm");
        return fail_test("delay_once.wasm missing or unreadable for absolute time update", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, "app_main", "absolute_time_task", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "absolute_time_task os_task_create status=%s handle=%s", os_status_name(status), task == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || task == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("absolute time update failed to create task", code_base);
    }

    anchor = os_get_tick_ms();
    status = os_update_time_ms(anchor);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "absolute time anchor=%u status=%s", anchor, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        return fail_test("absolute time update failed to anchor time", code_base + 1);
    }

    log_phase("absolute time update first scheduler call should hit env.os_delay_ms(10)");
    status = os_schedule();
    state_after_delay = os_task_get_state(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "absolute time waiting state before wake=%s status=%s", os_task_state_name(state_after_delay), os_status_name(status));
    log_info(message);
    log_task_counters("absolute time update after delay");

    if (status == OS_STATUS_WASM_ERROR)
    {
        log_last_os_error(test_name);
        free_binary(&wasm_binary);
        return fail_test("absolute time update hit WASM error during delay", code_base + 2);
    }

    if (state_after_delay != OS_TASK_WAITING || os_get_waiting_task_count() != 1U || os_get_ready_task_count() != 0U)
    {
        free_binary(&wasm_binary);
        return fail_test("absolute time update did not enter expected waiting state", code_base + 3);
    }

    status = os_update_time_ms(anchor + 9U);
    state_before_wake = os_task_get_state(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "absolute time update to anchor + 9 ms=%u status=%s state=%s", anchor + 9U, os_status_name(status), os_task_state_name(state_before_wake));
    log_info(message);
    if (status != OS_STATUS_OK || state_before_wake != OS_TASK_WAITING)
    {
        free_binary(&wasm_binary);
        return fail_test("absolute time update woke task before anchor plus 10 ms", code_base + 4);
    }

    status = os_update_time_ms(anchor + 10U);
    state_after_wake = os_task_get_state(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "absolute time update to anchor + 10 ms=%u status=%s ready state after wake=%s", anchor + 10U, os_status_name(status), os_task_state_name(state_after_wake));
    log_info(message);
    if (status != OS_STATUS_OK || state_after_wake != OS_TASK_READY)
    {
        free_binary(&wasm_binary);
        return fail_test("absolute time update did not wake task at anchor plus 10 ms", code_base + 5);
    }

    for (uint32_t iteration = 0U; iteration < TEST_MAX_SCHEDULE_ITERATIONS; ++iteration)
    {
        status = os_schedule();
        final_state = os_task_get_state(task);
        resume_iterations = iteration + 1U;
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "absolute time resume iteration=%u status=%s task_state=%s", resume_iterations, os_status_name(status), os_task_state_name(final_state));
        log_info(message);

        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
            free_binary(&wasm_binary);
            return fail_test("absolute time update hit WASM error after wake", code_base + 6);
        }

        if (final_state == OS_TASK_DEAD)
        {
            task_dead = 1;
            break;
        }

        if (status != OS_STATUS_OK)
        {
            free_binary(&wasm_binary);
            return fail_test("absolute time update got unexpected scheduler status after wake", code_base + 7);
        }
    }

    if (!task_dead)
    {
        free_binary(&wasm_binary);
        return fail_test("absolute time update task did not reach OS_TASK_DEAD", code_base + 8);
    }

    status = os_task_delete(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "absolute time task delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        return fail_test("absolute time update failed to delete task", code_base + 9);
    }

    no_task_status = verify_no_tasks(code_base + 10);
    if (no_task_status == 0)
    {
        log_task_counters("absolute time final counters");
        log_pass("absolute time update final PASS");
    }

    free_binary(&wasm_binary);
    return no_task_status;
}

static int run_tick_source_reanchor_test(int code_base)
{
    const char* test_name = "tick source re-anchor";
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s test start", test_name);
    log_phase(message);

    os_shutdown();
    status = os_init();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_init status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        return fail_test("tick source re-anchor failed to initialize OS", code_base);
    }

    if (os_get_tick_ms() != 0U)
    {
        return fail_test("tick source re-anchor expected initial tick to be zero", code_base + 1);
    }

    status = os_update_time_ms(1000U);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s absolute anchor at 1000 status=%s tick=%u", test_name, os_status_name(status), os_get_tick_ms());
    log_info(message);
    if (status != OS_STATUS_OK || os_get_tick_ms() != 0U)
    {
        return fail_test("tick source re-anchor failed absolute anchor at 1000", code_base + 2);
    }

    status = os_update_time_ms(1005U);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s absolute advance to 1005 status=%s tick=%u", test_name, os_status_name(status), os_get_tick_ms());
    log_info(message);
    if (status != OS_STATUS_OK || os_get_tick_ms() != 5U)
    {
        return fail_test("tick source re-anchor failed absolute advance to 1005", code_base + 3);
    }

    os_tick(7U);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s manual relative tick by 7 tick=%u", test_name, os_get_tick_ms());
    log_info(message);
    if (os_get_tick_ms() != 12U)
    {
        return fail_test("tick source re-anchor failed manual relative tick by 7", code_base + 4);
    }

    status = os_update_time_ms(1012U);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s absolute re-anchor at 1012 status=%s tick=%u", test_name, os_status_name(status), os_get_tick_ms());
    log_info(message);
    if (status != OS_STATUS_OK || os_get_tick_ms() != 12U)
    {
        return fail_test("tick source re-anchor double-advanced at absolute re-anchor 1012", code_base + 5);
    }

    status = os_update_time_ms(1013U);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s absolute advance to 1013 status=%s tick=%u", test_name, os_status_name(status), os_get_tick_ms());
    log_info(message);
    if (status != OS_STATUS_OK || os_get_tick_ms() != 13U)
    {
        return fail_test("tick source re-anchor failed absolute advance to 1013", code_base + 6);
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s final tick value=%u", test_name, os_get_tick_ms());
    log_info(message);

    if (os_get_task_count() != 0U || os_get_ready_task_count() != 0U ||
        os_get_waiting_task_count() != 0U || os_task_get_current() != 0)
    {
        log_task_counters("tick source re-anchor final counters");
        return fail_test("tick source re-anchor expected clean final counters", code_base + 7);
    }

    log_task_counters("tick source re-anchor final counters");
    log_pass("tick source re-anchor final PASS");
    return 0;
}

static int run_preempt_request_api_test(int code_base)
{
    const char* test_name = "preempt request API";
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    OsTaskState final_state = OS_TASK_DEAD;
    uint32_t iterations_used = 0U;
    int task_dead = 0;
    int no_task_status = 0;

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s test start", test_name);
    log_phase(message);

    os_shutdown();
    os_request_preempt();
    log_info("preempt request before os_init returned without crashing");
    if (os_get_task_count() != 0U || os_get_ready_task_count() != 0U ||
        os_get_waiting_task_count() != 0U || os_task_get_current() != 0)
    {
        return fail_test("preempt request before init changed task counters", code_base);
    }

    status = os_init();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_init status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        return fail_test("preempt request API failed to initialize OS", code_base + 1);
    }

    os_request_preempt();
    log_info("preempt request with initialized OS and no current task returned without crashing");
    if (os_get_task_count() != 0U || os_get_ready_task_count() != 0U ||
        os_get_waiting_task_count() != 0U || os_task_get_current() != 0)
    {
        return fail_test("preempt request with no current task changed counters", code_base + 2);
    }

    if (!read_binary_file("build/simple_loop.wasm", &wasm_binary))
    {
        log_fail("preempt request API could not load simple_loop.wasm");
        return fail_test("simple_loop.wasm missing or unreadable for preempt request API", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, "app_main", "preempt_request_task", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "preempt_request_task os_task_create status=%s handle=%s", os_status_name(status), task == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || task == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("preempt request API failed to create finite WASM task", code_base + 3);
    }

    os_request_preempt();
    log_info("preempt request posted before scheduling finite WASM task");

    for (uint32_t iteration = 0U; iteration < TEST_MAX_SCHEDULE_ITERATIONS; ++iteration)
    {
        status = os_schedule();
        final_state = os_task_get_state(task);
        iterations_used = iteration + 1U;

        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "preempt request iteration=%u status=%s task_state=%s", iterations_used, os_status_name(status), os_task_state_name(final_state));
        log_info(message);

        if (status == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
            free_binary(&wasm_binary);
            return fail_test("preempt request API turned requested preemption into a WASM error", code_base + 4);
        }

        if (final_state == OS_TASK_DEAD)
        {
            task_dead = 1;
            break;
        }

        if (status == OS_STATUS_NO_READY_TASKS)
        {
            free_binary(&wasm_binary);
            return fail_test("preempt request API had no ready tasks before finite task death", code_base + 5);
        }

        if (status != OS_STATUS_OK)
        {
            free_binary(&wasm_binary);
            return fail_test("preempt request API got unexpected scheduler status", code_base + 6);
        }
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "preempt request finite task iterations=%u final_state=%s", iterations_used, os_task_state_name(final_state));
    log_info(message);
    if (!task_dead)
    {
        free_binary(&wasm_binary);
        return fail_test("preempt request API finite task did not finish", code_base + 7);
    }

    if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        log_last_os_error(test_name);
        free_binary(&wasm_binary);
        return fail_test("preempt request API left unexpected WASM diagnostics", code_base + 8);
    }

    status = os_task_delete(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "preempt_request_task delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        return fail_test("preempt request API failed to delete task", code_base + 9);
    }

    no_task_status = verify_no_tasks(code_base + 10);
    if (no_task_status == 0)
    {
        log_task_counters("preempt request API final counters");
        log_pass("preempt request API final PASS");
    }

    free_binary(&wasm_binary);
    return no_task_status;
}

static int run_long_running_fuel_slice_test(int code_base)
{
    const char* test_name = "long-running fuel slice";
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    OsTaskState task_state = OS_TASK_DEAD;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int no_task_status = 0;
    const uint32_t schedule_slices = 3U;

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s test start", test_name);
    log_phase(message);
    log_info("long-running fixture: build/spin_forever.wasm");

    if (!read_binary_file("build/spin_forever.wasm", &wasm_binary))
    {
        log_fail("long-running fuel slice could not load spin_forever.wasm");
        return fail_test("spin_forever.wasm missing or unreadable for long-running fuel slice", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, "app_main", "spin_forever_task", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "spin_forever_task os_task_create status=%s handle=%s", os_status_name(status), task == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || task == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("long-running fuel slice failed to create spin_forever_task", code_base);
    }

    for (uint32_t slice = 0U; slice < schedule_slices; ++slice)
    {
        status = os_schedule();
        task_state = os_task_get_state(task);

        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "long-running slice=%u status=%s task_state=%s current=%s last_error=%s", slice + 1U, os_status_name(status), os_task_state_name(task_state), os_task_get_current() == 0 ? "null" : "non-null", os_status_name(os_get_last_error_status()));
        log_info(message);

        if (status != OS_STATUS_OK)
        {
            if (status == OS_STATUS_WASM_ERROR)
            {
                log_last_os_error(test_name);
            }
            free_binary(&wasm_binary);
            return fail_test("long-running fuel slice expected OS_STATUS_OK at scheduler boundary", code_base + 1);
        }

        if (task_state == OS_TASK_DEAD)
        {
            free_binary(&wasm_binary);
            return fail_test("long-running fuel slice task unexpectedly reached OS_TASK_DEAD", code_base + 2);
        }

        if (task_state != OS_TASK_READY)
        {
            free_binary(&wasm_binary);
            return fail_test("long-running fuel slice task did not return to OS_TASK_READY", code_base + 3);
        }

        if (os_task_get_current() != 0)
        {
            free_binary(&wasm_binary);
            return fail_test("long-running fuel slice left a current task after scheduling", code_base + 4);
        }

        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
            free_binary(&wasm_binary);
            return fail_test("long-running fuel slice left unexpected WASM diagnostics", code_base + 5);
        }
    }

    status = os_task_delete(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "spin_forever_task delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        return fail_test("long-running fuel slice failed to delete task", code_base + 6);
    }

    no_task_status = verify_no_tasks(code_base + 7);
    if (no_task_status == 0)
    {
        log_task_counters("long-running fuel slice final counters");
        log_pass("long-running fuel slice final PASS");
    }

    free_binary(&wasm_binary);
    return no_task_status;
}


static int run_two_long_running_task_fairness_test(int code_base)
{
    const char* test_name = "two long-running task fairness";
    TestBinary wasm_binary;
    OsTaskHandle task_a = 0;
    OsTaskHandle task_b = 0;
    OsStatus status = OS_STATUS_OK;
    OsTaskState state_a = OS_TASK_DEAD;
    OsTaskState state_b = OS_TASK_DEAD;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int no_task_status = 0;
    const uint32_t schedule_slices = 6U;
    uint32_t initial_total_runs = 0U;
    uint32_t final_run_count_a = 0U;
    uint32_t final_run_count_b = 0U;
    uint32_t final_total_runs = 0U;

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s test start", test_name);
    log_phase(message);
    log_info("two long-running fairness fixture: build/spin_forever.wasm");

    if (!read_binary_file("build/spin_forever.wasm", &wasm_binary))
    {
        log_fail("two long-running task fairness could not load spin_forever.wasm");
        return fail_test("spin_forever.wasm missing or unreadable for two long-running task fairness", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&task_a, wasm_binary.bytes, wasm_binary.size, "app_main", "spin_forever_task_a", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "spin_forever_task_a os_task_create status=%s handle=%s", os_status_name(status), task_a == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || task_a == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("two long-running task fairness failed to create spin_forever_task_a", code_base);
    }

    status = os_task_create(&task_b, wasm_binary.bytes, wasm_binary.size, "app_main", "spin_forever_task_b", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "spin_forever_task_b os_task_create status=%s handle=%s", os_status_name(status), task_b == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || task_b == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("two long-running task fairness failed to create spin_forever_task_b", code_base + 1);
    }

    state_a = os_task_get_state(task_a);
    state_b = os_task_get_state(task_b);
    log_task_counters("two long-running task fairness after create");
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "initial states task_a=%s task_b=%s run_count_a=%u run_count_b=%u",
        os_task_state_name(state_a), os_task_state_name(state_b), os_task_get_run_count(task_a), os_task_get_run_count(task_b));
    log_info(message);

    if (state_a != OS_TASK_READY || state_b != OS_TASK_READY ||
        os_get_task_count() != 2U || os_get_ready_task_count() != 2U ||
        os_get_waiting_task_count() != 0U || os_task_get_current() != 0 ||
        os_task_get_run_count(task_a) != 0U || os_task_get_run_count(task_b) != 0U)
    {
        free_binary(&wasm_binary);
        return fail_test("two long-running task fairness initial expectations failed", code_base + 2);
    }

    if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        log_last_os_error(test_name);
        free_binary(&wasm_binary);
        return fail_test("two long-running task fairness started with unexpected WASM diagnostics", code_base + 3);
    }

    initial_total_runs = os_task_get_run_count(task_a) + os_task_get_run_count(task_b);

    for (uint32_t slice = 0U; slice < schedule_slices; ++slice)
    {
        status = os_schedule();
        state_a = os_task_get_state(task_a);
        state_b = os_task_get_state(task_b);

        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "fairness slice=%u status=%s task_a=%s task_b=%s run_count_a=%u run_count_b=%u current=%s last_error=%s",
            slice + 1U, os_status_name(status), os_task_state_name(state_a), os_task_state_name(state_b),
            os_task_get_run_count(task_a), os_task_get_run_count(task_b),
            os_task_get_current() == 0 ? "null" : "non-null", os_status_name(os_get_last_error_status()));
        log_info(message);

        if (status != OS_STATUS_OK)
        {
            if (status == OS_STATUS_WASM_ERROR)
            {
                log_last_os_error(test_name);
            }
            free_binary(&wasm_binary);
            return fail_test("two long-running task fairness expected OS_STATUS_OK at scheduler boundary", code_base + 4);
        }

        if (state_a != OS_TASK_READY || state_b != OS_TASK_READY ||
            os_get_task_count() != 2U || os_get_ready_task_count() != 2U ||
            os_get_waiting_task_count() != 0U || os_task_get_current() != 0)
        {
            free_binary(&wasm_binary);
            return fail_test("two long-running task fairness tasks did not remain alive and READY after a slice", code_base + 5);
        }

        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(test_name);
            free_binary(&wasm_binary);
            return fail_test("two long-running task fairness left unexpected WASM diagnostics", code_base + 6);
        }
    }

    final_run_count_a = os_task_get_run_count(task_a);
    final_run_count_b = os_task_get_run_count(task_b);
    final_total_runs = final_run_count_a + final_run_count_b;
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "fairness final run counts task_a=%u task_b=%u total=%u", final_run_count_a, final_run_count_b, final_total_runs);
    log_info(message);

    if (final_total_runs != initial_total_runs + schedule_slices)
    {
        free_binary(&wasm_binary);
        return fail_test("two long-running task fairness total run count did not match scheduled slices", code_base + 7);
    }

    if (final_run_count_a == 0U || final_run_count_b == 0U ||
        final_run_count_a == schedule_slices || final_run_count_b == schedule_slices)
    {
        free_binary(&wasm_binary);
        return fail_test("two long-running task fairness did not give both tasks scheduler slices", code_base + 8);
    }
    log_pass("two long-running task fairness both tasks received bounded scheduler slices");

    status = os_task_delete(task_a);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "spin_forever_task_a delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        return fail_test("two long-running task fairness failed to delete task_a", code_base + 9);
    }

    status = os_task_delete(task_b);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "spin_forever_task_b delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        return fail_test("two long-running task fairness failed to delete task_b", code_base + 10);
    }

    if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        log_last_os_error(test_name);
        free_binary(&wasm_binary);
        return fail_test("two long-running task fairness cleanup left unexpected WASM diagnostics", code_base + 11);
    }

    no_task_status = verify_no_tasks(code_base + 12);
    if (no_task_status == 0)
    {
        log_task_counters("two long-running task fairness final counters");
        log_pass("two long-running task fairness final PASS");
    }

    free_binary(&wasm_binary);
    return no_task_status;
}


static int run_priority_scheduler_policy_test(int code_base)
{
    const char* test_name = "priority scheduler policy";
    TestBinary wasm_binary;
    OsTaskHandle low_task = 0;
    OsTaskHandle high_task = 0;
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s test start", test_name);
    log_phase(message);
    log_info("priority scheduler policy fixture: build/spin_forever.wasm");

    if (!read_binary_file("build/spin_forever.wasm", &wasm_binary))
    {
        log_fail("priority scheduler policy could not load spin_forever.wasm");
        return fail_test("spin_forever.wasm missing or unreadable for priority scheduler policy", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&low_task, wasm_binary.bytes, wasm_binary.size, "app_main", "priority_low_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_LOW);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority_low_task create status=%s handle=%s", os_status_name(status), low_task == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || low_task == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("priority scheduler policy failed to create low-priority task", code_base);
    }

    status = os_task_create(&high_task, wasm_binary.bytes, wasm_binary.size, "app_main", "priority_high_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_HIGH);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority_high_task create status=%s handle=%s", os_status_name(status), high_task == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || high_task == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("priority scheduler policy failed to create high-priority task", code_base + 1);
    }

    log_task_counters("priority scheduler policy after create");
    if (os_task_get_state(low_task) != OS_TASK_READY || os_task_get_state(high_task) != OS_TASK_READY ||
        os_get_task_count() != 2U || os_get_ready_task_count() != 2U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_task_get_run_count(low_task) != 0U || os_task_get_run_count(high_task) != 0U)
    {
        free_binary(&wasm_binary);
        return fail_test("priority scheduler policy initial expectations failed", code_base + 2);
    }

    if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        log_last_os_error(test_name);
        free_binary(&wasm_binary);
        return fail_test("priority scheduler policy started with unexpected WASM diagnostics", code_base + 3);
    }

    for (uint32_t slice = 0U; slice < 3U; ++slice)
    {
        status = os_schedule();
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority both-ready slice=%u status=%s low_state=%s high_state=%s low_runs=%u high_runs=%u current=%s last_error=%s",
            slice + 1U, os_status_name(status), os_task_state_name(os_task_get_state(low_task)), os_task_state_name(os_task_get_state(high_task)),
            os_task_get_run_count(low_task), os_task_get_run_count(high_task), os_task_get_current() == 0 ? "null" : "non-null", os_status_name(os_get_last_error_status()));
        log_info(message);
        if (status != OS_STATUS_OK || os_task_get_state(low_task) != OS_TASK_READY || os_task_get_state(high_task) != OS_TASK_READY || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name);
            free_binary(&wasm_binary);
            return fail_test("priority scheduler policy high-priority READY slice expectations failed", code_base + 4);
        }
    }

    if (os_task_get_run_count(high_task) != 3U || os_task_get_run_count(low_task) != 0U)
    {
        free_binary(&wasm_binary);
        return fail_test("priority scheduler policy did not keep low-priority task from running while high-priority task was READY", code_base + 5);
    }

    status = os_task_suspend(high_task);
    log_task_counters("priority scheduler policy after high suspend");
    if (status != OS_STATUS_OK || os_task_get_state(high_task) != OS_TASK_SUSPENDED || os_task_get_state(low_task) != OS_TASK_READY ||
        os_get_task_count() != 2U || os_get_ready_task_count() != 1U || os_get_waiting_task_count() != 0U)
    {
        free_binary(&wasm_binary);
        return fail_test("priority scheduler policy failed high-priority suspend expectations", code_base + 6);
    }

    for (uint32_t slice = 0U; slice < 2U; ++slice)
    {
        status = os_schedule();
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority high-suspended slice=%u status=%s low_state=%s high_state=%s low_runs=%u high_runs=%u current=%s last_error=%s",
            slice + 1U, os_status_name(status), os_task_state_name(os_task_get_state(low_task)), os_task_state_name(os_task_get_state(high_task)),
            os_task_get_run_count(low_task), os_task_get_run_count(high_task), os_task_get_current() == 0 ? "null" : "non-null", os_status_name(os_get_last_error_status()));
        log_info(message);
        if (status != OS_STATUS_OK || os_task_get_state(low_task) != OS_TASK_READY || os_task_get_state(high_task) != OS_TASK_SUSPENDED || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name);
            free_binary(&wasm_binary);
            return fail_test("priority scheduler policy low-priority slice while high suspended failed", code_base + 7);
        }
    }

    if (os_task_get_run_count(low_task) != 2U || os_task_get_run_count(high_task) != 3U)
    {
        free_binary(&wasm_binary);
        return fail_test("priority scheduler policy low-priority run count while high suspended failed", code_base + 8);
    }

    status = os_task_resume(high_task);
    log_task_counters("priority scheduler policy after high resume");
    if (status != OS_STATUS_OK || os_task_get_state(low_task) != OS_TASK_READY || os_task_get_state(high_task) != OS_TASK_READY ||
        os_get_task_count() != 2U || os_get_ready_task_count() != 2U || os_get_waiting_task_count() != 0U)
    {
        free_binary(&wasm_binary);
        return fail_test("priority scheduler policy failed high-priority resume expectations", code_base + 9);
    }

    for (uint32_t slice = 0U; slice < 2U; ++slice)
    {
        status = os_schedule();
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority after-resume slice=%u status=%s low_state=%s high_state=%s low_runs=%u high_runs=%u current=%s last_error=%s",
            slice + 1U, os_status_name(status), os_task_state_name(os_task_get_state(low_task)), os_task_state_name(os_task_get_state(high_task)),
            os_task_get_run_count(low_task), os_task_get_run_count(high_task), os_task_get_current() == 0 ? "null" : "non-null", os_status_name(os_get_last_error_status()));
        log_info(message);
        if (status != OS_STATUS_OK || os_task_get_state(low_task) != OS_TASK_READY || os_task_get_state(high_task) != OS_TASK_READY || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name);
            free_binary(&wasm_binary);
            return fail_test("priority scheduler policy high-priority after resume slice expectations failed", code_base + 10);
        }
    }

    if (os_task_get_run_count(high_task) != 5U || os_task_get_run_count(low_task) != 2U)
    {
        free_binary(&wasm_binary);
        return fail_test("priority scheduler policy high-priority task did not win again after resume", code_base + 11);
    }

    status = os_task_delete(low_task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority_low_task delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        return fail_test("priority scheduler policy failed to delete low-priority task", code_base + 12);
    }

    status = os_task_delete(high_task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority_high_task delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&wasm_binary);
        return fail_test("priority scheduler policy failed to delete high-priority task", code_base + 13);
    }

    if (os_get_task_count() != 0U || os_get_ready_task_count() != 0U || os_get_waiting_task_count() != 0U || os_task_get_current() != 0)
    {
        free_binary(&wasm_binary);
        return fail_test("priority scheduler policy final counters were not clean", code_base + 14);
    }

    if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        log_last_os_error(test_name);
        free_binary(&wasm_binary);
        return fail_test("priority scheduler policy cleanup left unexpected WASM diagnostics", code_base + 15);
    }

    log_task_counters("priority scheduler policy final counters");
    log_pass("priority scheduler policy final PASS");
    free_binary(&wasm_binary);
    return 0;
}



static void log_scheduler_policy_task(const char* label, OsTaskHandle task)
{
    char message[TEST_MESSAGE_BUFFER_SIZE];
    format_message(
        message,
        TEST_MESSAGE_BUFFER_SIZE,
        "%s name=%s priority=%u state=%s run_count=%u",
        label,
        os_task_get_name(task) == 0 ? "null" : os_task_get_name(task),
        os_task_get_priority(task),
        os_task_state_name(os_task_get_state(task)),
        os_task_get_run_count(task)
    );
    log_info(message);
}

static void log_wake_policy_snapshot(const char* label, OsTaskHandle task_a, OsTaskHandle task_b, OsTaskHandle task_c)
{
    char message[TEST_MESSAGE_BUFFER_SIZE];
    log_task_counters(label);
    log_scheduler_policy_task("wake policy task_a", task_a);
    log_scheduler_policy_task("wake policy task_b", task_b);
    if (task_c != 0)
    {
        log_scheduler_policy_task("wake policy task_c", task_c);
    }
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s tick=%u last_error=%s", label, os_get_tick_ms(), os_status_name(os_get_last_error_status()));
    log_info(message);
}

static void log_priority_change_snapshot(const char* label, OsTaskHandle task_a, OsTaskHandle task_b)
{
    char message[TEST_MESSAGE_BUFFER_SIZE];
    log_task_counters(label);
    log_scheduler_policy_task("priority change task_a", task_a);
    log_scheduler_policy_task("priority change task_b", task_b);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s last_error=%s", label, os_status_name(os_get_last_error_status()));
    log_info(message);
}

static int verify_priority_change_ready_snapshot(OsTaskHandle task_a, OsTaskHandle task_b, uint32_t expected_priority_a, uint32_t expected_priority_b, uint32_t expected_run_count_a, uint32_t expected_run_count_b)
{
    if (os_task_get_state(task_a) != OS_TASK_READY || os_task_get_state(task_b) != OS_TASK_READY ||
        os_get_task_count() != 2U || os_get_ready_task_count() != 2U || os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 || os_task_get_priority(task_a) != expected_priority_a || os_task_get_priority(task_b) != expected_priority_b ||
        os_task_get_run_count(task_a) != expected_run_count_a || os_task_get_run_count(task_b) != expected_run_count_b ||
        os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error("priority change scheduler policy");
        }
        return 0;
    }

    return 1;
}

static int run_priority_change_scheduler_policy_test(int code_base)
{
    const char* test_name = "priority change scheduler policy";
    TestBinary wasm_binary;
    OsTaskHandle task_a = 0;
    OsTaskHandle task_b = 0;
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int no_task_status = 0;

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s test start", test_name);
    log_phase(message);
    log_info("priority change scheduler policy fixture: build/spin_forever.wasm");

    if (!read_binary_file("build/spin_forever.wasm", &wasm_binary))
    {
        log_fail("priority change scheduler policy could not load spin_forever.wasm");
        return fail_test("spin_forever.wasm missing or unreadable for priority change scheduler policy", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&task_a, wasm_binary.bytes, wasm_binary.size, "app_main", "priority_change_task_a", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority_change_task_a create status=%s handle=%s", os_status_name(status), task_a == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || task_a == 0) { free_binary(&wasm_binary); return fail_test("priority change scheduler policy failed to create task_a", code_base); }

    status = os_task_create(&task_b, wasm_binary.bytes, wasm_binary.size, "app_main", "priority_change_task_b", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority_change_task_b create status=%s handle=%s", os_status_name(status), task_b == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || task_b == 0) { free_binary(&wasm_binary); return fail_test("priority change scheduler policy failed to create task_b", code_base + 1); }

    log_phase("priority change scheduler policy initial equal-priority phase");
    log_priority_change_snapshot("priority change initial", task_a, task_b);
    if (!verify_priority_change_ready_snapshot(task_a, task_b, OS_TASK_PRIORITY_NORMAL, OS_TASK_PRIORITY_NORMAL, 0U, 0U)) { free_binary(&wasm_binary); return fail_test("priority change scheduler policy initial expectations failed", code_base + 2); }

    for (uint32_t slice = 0U; slice < 4U; ++slice)
    {
        status = os_schedule();
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority change equal slice=%u status=%s task_a_runs=%u task_b_runs=%u current=%s last_error=%s", slice + 1U, os_status_name(status), os_task_get_run_count(task_a), os_task_get_run_count(task_b), os_task_get_current() == 0 ? "null" : "non-null", os_status_name(os_get_last_error_status()));
        log_info(message);
        if (status != OS_STATUS_OK || os_task_get_state(task_a) != OS_TASK_READY || os_task_get_state(task_b) != OS_TASK_READY || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        { if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&wasm_binary); return fail_test("priority change scheduler policy equal-priority slice expectations failed", code_base + 3); }
    }
    log_priority_change_snapshot("priority change after equal-priority phase", task_a, task_b);
    if (!verify_priority_change_ready_snapshot(task_a, task_b, OS_TASK_PRIORITY_NORMAL, OS_TASK_PRIORITY_NORMAL, 2U, 2U)) { free_binary(&wasm_binary); return fail_test("priority change scheduler policy equal-priority run counts failed", code_base + 4); }

    log_phase("priority change scheduler policy priority raised phase");
    status = os_task_set_priority(task_b, OS_TASK_PRIORITY_HIGH);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority_change_task_b raise priority status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("priority change scheduler policy failed to raise task_b priority", code_base + 5); }
    log_priority_change_snapshot("priority change after priority raise", task_a, task_b);
    if (!verify_priority_change_ready_snapshot(task_a, task_b, OS_TASK_PRIORITY_NORMAL, OS_TASK_PRIORITY_HIGH, 2U, 2U)) { free_binary(&wasm_binary); return fail_test("priority change scheduler policy priority raise snapshot failed", code_base + 6); }

    for (uint32_t slice = 0U; slice < 3U; ++slice)
    {
        status = os_schedule();
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority change raised slice=%u status=%s task_a_runs=%u task_b_runs=%u current=%s last_error=%s", slice + 1U, os_status_name(status), os_task_get_run_count(task_a), os_task_get_run_count(task_b), os_task_get_current() == 0 ? "null" : "non-null", os_status_name(os_get_last_error_status()));
        log_info(message);
        if (status != OS_STATUS_OK || os_task_get_state(task_a) != OS_TASK_READY || os_task_get_state(task_b) != OS_TASK_READY || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        { if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&wasm_binary); return fail_test("priority change scheduler policy raised-priority slice expectations failed", code_base + 7); }
    }
    log_priority_change_snapshot("priority change after raised-priority phase", task_a, task_b);
    if (!verify_priority_change_ready_snapshot(task_a, task_b, OS_TASK_PRIORITY_NORMAL, OS_TASK_PRIORITY_HIGH, 2U, 5U)) { free_binary(&wasm_binary); return fail_test("priority change scheduler policy raised-priority run counts failed", code_base + 8); }

    log_phase("priority change scheduler policy priority restored phase");
    status = os_task_set_priority(task_b, OS_TASK_PRIORITY_NORMAL);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority_change_task_b restore priority status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("priority change scheduler policy failed to restore task_b priority", code_base + 9); }
    log_priority_change_snapshot("priority change after priority restore", task_a, task_b);
    if (!verify_priority_change_ready_snapshot(task_a, task_b, OS_TASK_PRIORITY_NORMAL, OS_TASK_PRIORITY_NORMAL, 2U, 5U)) { free_binary(&wasm_binary); return fail_test("priority change scheduler policy priority restore snapshot failed", code_base + 10); }

    for (uint32_t slice = 0U; slice < 4U; ++slice)
    {
        status = os_schedule();
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority change restored slice=%u status=%s task_a_runs=%u task_b_runs=%u current=%s last_error=%s", slice + 1U, os_status_name(status), os_task_get_run_count(task_a), os_task_get_run_count(task_b), os_task_get_current() == 0 ? "null" : "non-null", os_status_name(os_get_last_error_status()));
        log_info(message);
        if (status != OS_STATUS_OK || os_task_get_state(task_a) != OS_TASK_READY || os_task_get_state(task_b) != OS_TASK_READY || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        { if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&wasm_binary); return fail_test("priority change scheduler policy restored-priority slice expectations failed", code_base + 11); }
    }
    log_priority_change_snapshot("priority change after restored-priority phase", task_a, task_b);
    if (!verify_priority_change_ready_snapshot(task_a, task_b, OS_TASK_PRIORITY_NORMAL, OS_TASK_PRIORITY_NORMAL, 4U, 7U)) { free_binary(&wasm_binary); return fail_test("priority change scheduler policy restored-priority fairness failed", code_base + 12); }

    log_phase("priority change scheduler policy cleanup");
    status = os_task_delete(task_a);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority_change_task_a delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("priority change scheduler policy failed to delete task_a", code_base + 13); }
    status = os_task_delete(task_b);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "priority_change_task_b delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("priority change scheduler policy failed to delete task_b", code_base + 14); }
    if (os_get_last_error_status() == OS_STATUS_WASM_ERROR) { log_last_os_error(test_name); free_binary(&wasm_binary); return fail_test("priority change scheduler policy cleanup left unexpected WASM diagnostics", code_base + 15); }

    no_task_status = verify_no_tasks(code_base + 16);
    if (no_task_status == 0)
    {
        log_task_counters("priority change scheduler policy final counters");
        log_last_os_error("priority change scheduler policy cleanup");
        log_pass("priority change scheduler policy final PASS");
    }

    free_binary(&wasm_binary);
    return no_task_status;
}


static void log_waiting_priority_snapshot(const char* label, OsTaskHandle waiting_task, OsTaskHandle peer_task)
{
    char message[TEST_MESSAGE_BUFFER_SIZE];
    log_task_counters(label);
    log_scheduler_policy_task("waiting priority task", waiting_task);
    log_scheduler_policy_task("waiting priority peer", peer_task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s last_error=%s", label, os_status_name(os_get_last_error_status()));
    log_info(message);
}

static int verify_waiting_priority_common(const char* context, OsTaskHandle waiting_task, OsTaskHandle peer_task, OsTaskState expected_waiting_state, OsTaskState expected_peer_state, uint32_t expected_waiting_priority, uint32_t expected_peer_priority, uint32_t expected_waiting_runs, uint32_t expected_peer_runs, uint32_t expected_ready_count, uint32_t expected_waiting_count)
{
    if (os_task_get_state(waiting_task) != expected_waiting_state ||
        os_task_get_state(peer_task) != expected_peer_state ||
        os_task_get_priority(waiting_task) != expected_waiting_priority ||
        os_task_get_priority(peer_task) != expected_peer_priority ||
        os_task_get_run_count(waiting_task) != expected_waiting_runs ||
        os_task_get_run_count(peer_task) != expected_peer_runs ||
        os_get_task_count() != 2U ||
        os_get_ready_task_count() != expected_ready_count ||
        os_get_waiting_task_count() != expected_waiting_count ||
        os_task_get_current() != 0 ||
        os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(context);
        }
        return 0;
    }

    return 1;
}

static int run_waiting_priority_raise_policy_test(int code_base)
{
    const char* test_name = "waiting priority raise policy";
    TestBinary delay_binary;
    TestBinary spin_binary;
    OsTaskHandle waiting_task = 0;
    OsTaskHandle peer_task = 0;
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int no_task_status = 0;

    delay_binary.bytes = 0;
    delay_binary.size = 0U;
    spin_binary.bytes = 0;
    spin_binary.size = 0U;

    log_phase("waiting priority raise setup");
    log_info("waiting priority raise fixtures: build/delay_once.wasm and build/spin_forever.wasm");
    if (!read_binary_file("build/delay_once.wasm", &delay_binary) || !read_binary_file("build/spin_forever.wasm", &spin_binary))
    {
        free_binary(&delay_binary);
        free_binary(&spin_binary);
        return fail_test("waiting priority raise fixtures missing or unreadable", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&waiting_task, delay_binary.bytes, delay_binary.size, "app_main", "waiting_raise_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "waiting_raise_task create status=%s handle=%s", os_status_name(status), waiting_task == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || waiting_task == 0) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority raise failed to create waiting task", code_base); }
    status = os_task_create(&peer_task, spin_binary.bytes, spin_binary.size, "app_main", "waiting_raise_peer_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "waiting_raise_peer_task create status=%s handle=%s", os_status_name(status), peer_task == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || peer_task == 0) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority raise failed to create peer task", code_base + 1); }
    log_waiting_priority_snapshot("waiting priority raise setup", waiting_task, peer_task);
    if (!verify_waiting_priority_common(test_name, waiting_task, peer_task, OS_TASK_READY, OS_TASK_READY, OS_TASK_PRIORITY_NORMAL, OS_TASK_PRIORITY_NORMAL, 0U, 0U, 2U, 0U)) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority raise setup expectations failed", code_base + 2); }

    status = os_schedule();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "waiting priority raise delay schedule status=%s", os_status_name(status));
    log_info(message);
    log_waiting_priority_snapshot("waiting priority raise after delay", waiting_task, peer_task);
    if (status != OS_STATUS_OK || !verify_waiting_priority_common(test_name, waiting_task, peer_task, OS_TASK_WAITING, OS_TASK_READY, OS_TASK_PRIORITY_NORMAL, OS_TASK_PRIORITY_NORMAL, 1U, 0U, 1U, 1U)) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority raise delay expectations failed", code_base + 3); }

    status = os_schedule();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "waiting priority raise peer schedule status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK || !verify_waiting_priority_common(test_name, waiting_task, peer_task, OS_TASK_WAITING, OS_TASK_READY, OS_TASK_PRIORITY_NORMAL, OS_TASK_PRIORITY_NORMAL, 1U, 1U, 1U, 1U)) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority raise peer schedule expectations failed", code_base + 4); }

    status = os_task_set_priority(waiting_task, OS_TASK_PRIORITY_HIGH);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "waiting_raise_task raise priority status=%s", os_status_name(status));
    log_info(message);
    log_waiting_priority_snapshot("waiting priority raise after priority update", waiting_task, peer_task);
    if (status != OS_STATUS_OK || !verify_waiting_priority_common(test_name, waiting_task, peer_task, OS_TASK_WAITING, OS_TASK_READY, OS_TASK_PRIORITY_HIGH, OS_TASK_PRIORITY_NORMAL, 1U, 1U, 1U, 1U)) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority raise priority update expectations failed", code_base + 5); }

    os_tick(10U);
    log_waiting_priority_snapshot("waiting priority raise after wake", waiting_task, peer_task);
    if (!verify_waiting_priority_common(test_name, waiting_task, peer_task, OS_TASK_READY, OS_TASK_READY, OS_TASK_PRIORITY_HIGH, OS_TASK_PRIORITY_NORMAL, 1U, 1U, 2U, 0U)) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority raise wake expectations failed", code_base + 6); }
    status = os_schedule();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "waiting priority raise key schedule status=%s", os_status_name(status));
    log_info(message);
    log_waiting_priority_snapshot("waiting priority raise key schedule", waiting_task, peer_task);
    if (status != OS_STATUS_OK || os_task_get_run_count(waiting_task) != 2U || os_task_get_run_count(peer_task) != 1U || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR) { if (os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority raise key scheduler selection failed", code_base + 7); }

    log_phase("waiting priority raise cleanup");
    status = os_task_delete(waiting_task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority raise failed to delete waiting task", code_base + 8); }
    status = os_task_delete(peer_task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority raise failed to delete peer task", code_base + 9); }
    no_task_status = verify_no_tasks(code_base + 10);
    if (no_task_status == 0) { log_task_counters("waiting priority raise cleanup"); log_pass("waiting priority raise policy final PASS"); }
    free_binary(&delay_binary);
    free_binary(&spin_binary);
    return no_task_status;
}

static int run_waiting_priority_lower_policy_test(int code_base)
{
    const char* test_name = "waiting priority lower policy";
    TestBinary delay_binary;
    TestBinary spin_binary;
    OsTaskHandle waiting_task = 0;
    OsTaskHandle peer_task = 0;
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int no_task_status = 0;

    delay_binary.bytes = 0;
    delay_binary.size = 0U;
    spin_binary.bytes = 0;
    spin_binary.size = 0U;

    log_phase("waiting priority lower setup");
    log_info("waiting priority lower fixtures: build/delay_once.wasm and build/spin_forever.wasm");
    if (!read_binary_file("build/delay_once.wasm", &delay_binary) || !read_binary_file("build/spin_forever.wasm", &spin_binary))
    {
        free_binary(&delay_binary);
        free_binary(&spin_binary);
        return fail_test("waiting priority lower fixtures missing or unreadable", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&waiting_task, delay_binary.bytes, delay_binary.size, "app_main", "waiting_lower_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || waiting_task == 0) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority lower failed to create waiting task", code_base); }
    status = os_task_create(&peer_task, spin_binary.bytes, spin_binary.size, "app_main", "waiting_lower_peer_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || peer_task == 0) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority lower failed to create peer task", code_base + 1); }
    log_waiting_priority_snapshot("waiting priority lower setup", waiting_task, peer_task);
    if (!verify_waiting_priority_common(test_name, waiting_task, peer_task, OS_TASK_READY, OS_TASK_READY, OS_TASK_PRIORITY_NORMAL, OS_TASK_PRIORITY_NORMAL, 0U, 0U, 2U, 0U)) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority lower setup expectations failed", code_base + 2); }

    status = os_schedule();
    log_waiting_priority_snapshot("waiting priority lower after delay", waiting_task, peer_task);
    if (status != OS_STATUS_OK || !verify_waiting_priority_common(test_name, waiting_task, peer_task, OS_TASK_WAITING, OS_TASK_READY, OS_TASK_PRIORITY_NORMAL, OS_TASK_PRIORITY_NORMAL, 1U, 0U, 1U, 1U)) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority lower delay expectations failed", code_base + 3); }
    status = os_schedule();
    if (status != OS_STATUS_OK || !verify_waiting_priority_common(test_name, waiting_task, peer_task, OS_TASK_WAITING, OS_TASK_READY, OS_TASK_PRIORITY_NORMAL, OS_TASK_PRIORITY_NORMAL, 1U, 1U, 1U, 1U)) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority lower peer schedule expectations failed", code_base + 4); }

    status = os_task_set_priority(waiting_task, OS_TASK_PRIORITY_LOW);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "waiting_lower_task lower priority status=%s", os_status_name(status));
    log_info(message);
    log_waiting_priority_snapshot("waiting priority lower after priority update", waiting_task, peer_task);
    if (status != OS_STATUS_OK || !verify_waiting_priority_common(test_name, waiting_task, peer_task, OS_TASK_WAITING, OS_TASK_READY, OS_TASK_PRIORITY_LOW, OS_TASK_PRIORITY_NORMAL, 1U, 1U, 1U, 1U)) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority lower priority update expectations failed", code_base + 5); }

    os_tick(10U);
    log_waiting_priority_snapshot("waiting priority lower after wake", waiting_task, peer_task);
    if (!verify_waiting_priority_common(test_name, waiting_task, peer_task, OS_TASK_READY, OS_TASK_READY, OS_TASK_PRIORITY_LOW, OS_TASK_PRIORITY_NORMAL, 1U, 1U, 2U, 0U)) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority lower wake expectations failed", code_base + 6); }
    status = os_schedule();
    log_waiting_priority_snapshot("waiting priority lower key schedule", waiting_task, peer_task);
    if (status != OS_STATUS_OK || !verify_waiting_priority_common(test_name, waiting_task, peer_task, OS_TASK_READY, OS_TASK_READY, OS_TASK_PRIORITY_LOW, OS_TASK_PRIORITY_NORMAL, 1U, 2U, 2U, 0U)) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority lower key scheduler selection failed", code_base + 7); }
    status = os_schedule();
    log_waiting_priority_snapshot("waiting priority lower second key schedule", waiting_task, peer_task);
    if (status != OS_STATUS_OK || !verify_waiting_priority_common(test_name, waiting_task, peer_task, OS_TASK_READY, OS_TASK_READY, OS_TASK_PRIORITY_LOW, OS_TASK_PRIORITY_NORMAL, 1U, 3U, 2U, 0U)) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority lower second scheduler selection failed", code_base + 8); }

    log_phase("waiting priority lower cleanup");
    status = os_task_delete(waiting_task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority lower failed to delete waiting task", code_base + 9); }
    status = os_task_delete(peer_task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("waiting priority lower failed to delete peer task", code_base + 10); }
    no_task_status = verify_no_tasks(code_base + 11);
    if (no_task_status == 0) { log_task_counters("waiting priority lower cleanup"); log_pass("waiting priority lower policy final PASS"); }
    free_binary(&delay_binary);
    free_binary(&spin_binary);
    return no_task_status;
}

static int run_waiting_priority_change_scheduler_policy_test(int code_base)
{
    int status = run_waiting_priority_raise_policy_test(code_base);
    if (status != 0)
    {
        return status;
    }

    return run_waiting_priority_lower_policy_test(code_base + 20);
}



static void log_suspend_resume_snapshot(const char* label, OsTaskHandle task_a, OsTaskHandle task_b, OsTaskHandle task_c)
{
    char message[TEST_MESSAGE_BUFFER_SIZE];
    log_task_counters(label);
    log_scheduler_policy_task("suspend resume task_a", task_a);
    log_scheduler_policy_task("suspend resume task_b", task_b);
    if (task_c != 0)
    {
        log_scheduler_policy_task("suspend resume task_c", task_c);
    }
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s last_error=%s", label, os_status_name(os_get_last_error_status()));
    log_info(message);
}

static int verify_suspend_resume_two_task_snapshot(const char* context, OsTaskHandle high_task, OsTaskHandle low_task, OsTaskState expected_high_state, OsTaskState expected_low_state, uint32_t expected_high_runs, uint32_t expected_low_runs, uint32_t expected_ready_count)
{
    if (os_task_get_state(high_task) != expected_high_state ||
        os_task_get_state(low_task) != expected_low_state ||
        os_task_get_priority(high_task) != OS_TASK_PRIORITY_HIGH ||
        os_task_get_priority(low_task) != OS_TASK_PRIORITY_LOW ||
        os_task_get_run_count(high_task) != expected_high_runs ||
        os_task_get_run_count(low_task) != expected_low_runs ||
        os_get_task_count() != 2U ||
        os_get_ready_task_count() != expected_ready_count ||
        os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 ||
        os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(context);
        }
        return 0;
    }

    return 1;
}

static int verify_suspend_resume_equal_snapshot(const char* context, OsTaskHandle task_a, OsTaskHandle task_b, OsTaskHandle task_c, OsTaskState expected_b_state, uint32_t expected_a_runs, uint32_t expected_b_runs, uint32_t expected_c_runs, uint32_t expected_ready_count)
{
    if (os_task_get_state(task_a) != OS_TASK_READY ||
        os_task_get_state(task_b) != expected_b_state ||
        os_task_get_state(task_c) != OS_TASK_READY ||
        os_task_get_priority(task_a) != OS_TASK_PRIORITY_NORMAL ||
        os_task_get_priority(task_b) != OS_TASK_PRIORITY_NORMAL ||
        os_task_get_priority(task_c) != OS_TASK_PRIORITY_NORMAL ||
        os_task_get_run_count(task_a) != expected_a_runs ||
        os_task_get_run_count(task_b) != expected_b_runs ||
        os_task_get_run_count(task_c) != expected_c_runs ||
        os_get_task_count() != 3U ||
        os_get_ready_task_count() != expected_ready_count ||
        os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 ||
        os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(context);
        }
        return 0;
    }

    return 1;
}

static int run_suspend_resume_high_priority_policy_test(int code_base)
{
    const char* test_name = "suspend resume high-priority policy";
    TestBinary wasm_binary;
    OsTaskHandle high_task = 0;
    OsTaskHandle low_task = 0;
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int no_task_status = 0;

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    log_phase("high-priority suspend setup");
    log_info("suspend resume high-priority fixture: build/spin_forever.wasm");
    if (!read_binary_file("build/spin_forever.wasm", &wasm_binary))
    {
        return fail_test("spin_forever.wasm missing or unreadable for suspend resume high-priority policy", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&high_task, wasm_binary.bytes, wasm_binary.size, "app_main", "suspend_high_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_HIGH);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "suspend_high_task create status=%s handle=%s", os_status_name(status), high_task == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || high_task == 0) { free_binary(&wasm_binary); return fail_test("suspend resume high-priority failed to create high task", code_base); }
    status = os_task_create(&low_task, wasm_binary.bytes, wasm_binary.size, "app_main", "suspend_low_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_LOW);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "suspend_low_task create status=%s handle=%s", os_status_name(status), low_task == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || low_task == 0) { free_binary(&wasm_binary); return fail_test("suspend resume high-priority failed to create low task", code_base + 1); }
    log_suspend_resume_snapshot("high-priority suspend setup", high_task, low_task, 0);
    if (!verify_suspend_resume_two_task_snapshot(test_name, high_task, low_task, OS_TASK_READY, OS_TASK_READY, 0U, 0U, 2U)) { free_binary(&wasm_binary); return fail_test("suspend resume high-priority setup expectations failed", code_base + 2); }

    status = os_task_suspend(high_task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "suspend_high_task suspend status=%s", os_status_name(status));
    log_info(message);
    log_phase("high-priority after suspend");
    log_suspend_resume_snapshot("high-priority after suspend", high_task, low_task, 0);
    if (status != OS_STATUS_OK || !verify_suspend_resume_two_task_snapshot(test_name, high_task, low_task, OS_TASK_SUSPENDED, OS_TASK_READY, 0U, 0U, 1U)) { free_binary(&wasm_binary); return fail_test("suspend resume high-priority suspend expectations failed", code_base + 3); }

    log_phase("high-priority while suspended scheduling");
    for (uint32_t slice = 0U; slice < 2U; ++slice)
    {
        status = os_schedule();
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "high-priority suspended slice=%u status=%s high_runs=%u low_runs=%u current=%s last_error=%s", slice + 1U, os_status_name(status), os_task_get_run_count(high_task), os_task_get_run_count(low_task), os_task_get_current() == 0 ? "null" : "non-null", os_status_name(os_get_last_error_status()));
        log_info(message);
        if (status != OS_STATUS_OK || !verify_suspend_resume_two_task_snapshot(test_name, high_task, low_task, OS_TASK_SUSPENDED, OS_TASK_READY, 0U, slice + 1U, 1U)) { free_binary(&wasm_binary); return fail_test("suspend resume high-priority suspended scheduling failed", code_base + 4); }
    }

    status = os_task_resume(high_task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "suspend_high_task resume status=%s", os_status_name(status));
    log_info(message);
    log_phase("high-priority after resume");
    log_suspend_resume_snapshot("high-priority after resume", high_task, low_task, 0);
    if (status != OS_STATUS_OK || !verify_suspend_resume_two_task_snapshot(test_name, high_task, low_task, OS_TASK_READY, OS_TASK_READY, 0U, 2U, 2U)) { free_binary(&wasm_binary); return fail_test("suspend resume high-priority resume expectations failed", code_base + 5); }

    log_phase("high-priority key schedule");
    status = os_schedule();
    log_suspend_resume_snapshot("high-priority key schedule", high_task, low_task, 0);
    if (status != OS_STATUS_OK || !verify_suspend_resume_two_task_snapshot(test_name, high_task, low_task, OS_TASK_READY, OS_TASK_READY, 1U, 2U, 2U)) { free_binary(&wasm_binary); return fail_test("suspend resume high-priority key scheduler selection failed", code_base + 6); }

    log_phase("cleanup");
    status = os_task_delete(high_task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "suspend_high_task delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("suspend resume high-priority failed to delete high task", code_base + 7); }
    status = os_task_delete(low_task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "suspend_low_task delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("suspend resume high-priority failed to delete low task", code_base + 8); }
    no_task_status = verify_no_tasks(code_base + 9);
    if (no_task_status == 0) { log_task_counters("suspend resume high-priority cleanup"); log_pass("suspend resume high-priority policy final PASS"); }
    free_binary(&wasm_binary);
    return no_task_status;
}

static int run_suspend_resume_equal_priority_policy_test(int code_base)
{
    const char* test_name = "suspend resume equal-priority policy";
    TestBinary wasm_binary;
    OsTaskHandle task_a = 0;
    OsTaskHandle task_b = 0;
    OsTaskHandle task_c = 0;
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int no_task_status = 0;

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    log_phase("equal-priority baseline");
    log_info("suspend resume equal-priority fixture: build/spin_forever.wasm");
    if (!read_binary_file("build/spin_forever.wasm", &wasm_binary))
    {
        return fail_test("spin_forever.wasm missing or unreadable for suspend resume equal-priority policy", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&task_a, wasm_binary.bytes, wasm_binary.size, "app_main", "suspend_equal_a_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task_a == 0) { free_binary(&wasm_binary); return fail_test("suspend resume equal-priority failed to create task_a", code_base); }
    status = os_task_create(&task_b, wasm_binary.bytes, wasm_binary.size, "app_main", "suspend_equal_b_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task_b == 0) { free_binary(&wasm_binary); return fail_test("suspend resume equal-priority failed to create task_b", code_base + 1); }
    status = os_task_create(&task_c, wasm_binary.bytes, wasm_binary.size, "app_main", "suspend_equal_c_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || task_c == 0) { free_binary(&wasm_binary); return fail_test("suspend resume equal-priority failed to create task_c", code_base + 2); }
    log_suspend_resume_snapshot("equal-priority baseline setup", task_a, task_b, task_c);
    if (!verify_suspend_resume_equal_snapshot(test_name, task_a, task_b, task_c, OS_TASK_READY, 0U, 0U, 0U, 3U)) { free_binary(&wasm_binary); return fail_test("suspend resume equal-priority setup expectations failed", code_base + 3); }

    for (uint32_t slice = 0U; slice < 3U; ++slice)
    {
        status = os_schedule();
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "equal-priority baseline slice=%u status=%s runs=%u/%u/%u current=%s last_error=%s", slice + 1U, os_status_name(status), os_task_get_run_count(task_a), os_task_get_run_count(task_b), os_task_get_run_count(task_c), os_task_get_current() == 0 ? "null" : "non-null", os_status_name(os_get_last_error_status()));
        log_info(message);
        if (status != OS_STATUS_OK || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR) { if (os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&wasm_binary); return fail_test("suspend resume equal-priority baseline slice failed", code_base + 4); }
    }
    log_suspend_resume_snapshot("equal-priority baseline", task_a, task_b, task_c);
    if (!verify_suspend_resume_equal_snapshot(test_name, task_a, task_b, task_c, OS_TASK_READY, 1U, 1U, 1U, 3U)) { free_binary(&wasm_binary); return fail_test("suspend resume equal-priority baseline expectations failed", code_base + 5); }

    status = os_task_suspend(task_b);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "suspend_equal_b_task suspend status=%s", os_status_name(status));
    log_info(message);
    log_phase("equal-priority after suspend");
    log_suspend_resume_snapshot("equal-priority after suspend", task_a, task_b, task_c);
    if (status != OS_STATUS_OK || !verify_suspend_resume_equal_snapshot(test_name, task_a, task_b, task_c, OS_TASK_SUSPENDED, 1U, 1U, 1U, 2U)) { free_binary(&wasm_binary); return fail_test("suspend resume equal-priority suspend expectations failed", code_base + 6); }

    log_phase("equal-priority while suspended scheduling");
    for (uint32_t slice = 0U; slice < 2U; ++slice)
    {
        status = os_schedule();
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "equal-priority suspended slice=%u status=%s runs=%u/%u/%u current=%s last_error=%s", slice + 1U, os_status_name(status), os_task_get_run_count(task_a), os_task_get_run_count(task_b), os_task_get_run_count(task_c), os_task_get_current() == 0 ? "null" : "non-null", os_status_name(os_get_last_error_status()));
        log_info(message);
        if (status != OS_STATUS_OK || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR || os_task_get_state(task_b) != OS_TASK_SUSPENDED || os_task_get_run_count(task_b) != 1U) { if (os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&wasm_binary); return fail_test("suspend resume equal-priority suspended slice failed", code_base + 7); }
    }
    log_suspend_resume_snapshot("equal-priority while suspended scheduling", task_a, task_b, task_c);
    if (!verify_suspend_resume_equal_snapshot(test_name, task_a, task_b, task_c, OS_TASK_SUSPENDED, 2U, 1U, 2U, 2U)) { free_binary(&wasm_binary); return fail_test("suspend resume equal-priority suspended scheduling expectations failed", code_base + 8); }

    status = os_task_resume(task_b);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "suspend_equal_b_task resume status=%s", os_status_name(status));
    log_info(message);
    log_phase("equal-priority after resume");
    log_suspend_resume_snapshot("equal-priority after resume", task_a, task_b, task_c);
    if (status != OS_STATUS_OK || !verify_suspend_resume_equal_snapshot(test_name, task_a, task_b, task_c, OS_TASK_READY, 2U, 1U, 2U, 3U)) { free_binary(&wasm_binary); return fail_test("suspend resume equal-priority resume expectations failed", code_base + 9); }

    log_phase("equal-priority post-resume scheduling");
    for (uint32_t slice = 0U; slice < 3U; ++slice)
    {
        status = os_schedule();
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "equal-priority post-resume slice=%u status=%s runs=%u/%u/%u current=%s last_error=%s", slice + 1U, os_status_name(status), os_task_get_run_count(task_a), os_task_get_run_count(task_b), os_task_get_run_count(task_c), os_task_get_current() == 0 ? "null" : "non-null", os_status_name(os_get_last_error_status()));
        log_info(message);
        if (status != OS_STATUS_OK || os_task_get_state(task_a) != OS_TASK_READY || os_task_get_state(task_b) != OS_TASK_READY || os_task_get_state(task_c) != OS_TASK_READY || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR) { if (os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&wasm_binary); return fail_test("suspend resume equal-priority post-resume slice failed", code_base + 10); }
    }
    log_suspend_resume_snapshot("equal-priority post-resume scheduling", task_a, task_b, task_c);
    if (!verify_suspend_resume_equal_snapshot(test_name, task_a, task_b, task_c, OS_TASK_READY, 3U, 2U, 3U, 3U)) { free_binary(&wasm_binary); return fail_test("suspend resume equal-priority post-resume fairness failed", code_base + 11); }

    log_phase("cleanup");
    status = os_task_delete(task_a);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("suspend resume equal-priority failed to delete task_a", code_base + 12); }
    status = os_task_delete(task_b);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("suspend resume equal-priority failed to delete task_b", code_base + 13); }
    status = os_task_delete(task_c);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("suspend resume equal-priority failed to delete task_c", code_base + 14); }
    no_task_status = verify_no_tasks(code_base + 15);
    if (no_task_status == 0) { log_task_counters("suspend resume equal-priority cleanup"); log_pass("suspend resume equal-priority policy final PASS"); }
    free_binary(&wasm_binary);
    return no_task_status;
}

static int run_suspend_resume_scheduler_policy_test(int code_base)
{
    int status = run_suspend_resume_high_priority_policy_test(code_base);
    if (status != 0)
    {
        return status;
    }

    return run_suspend_resume_equal_priority_policy_test(code_base + 30);
}


static void log_task_delete_scheduler_pointer_snapshot(const char* label, OsTaskHandle task_a, OsTaskHandle task_b, OsTaskHandle task_c)
{
    char message[TEST_MESSAGE_BUFFER_SIZE];
    log_task_counters(label);
    if (task_a != 0)
    {
        log_scheduler_policy_task("delete pointer task_a", task_a);
    }
    if (task_b != 0)
    {
        log_scheduler_policy_task("delete pointer task_b", task_b);
    }
    if (task_c != 0)
    {
        log_scheduler_policy_task("delete pointer task_c", task_c);
    }
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s last_error=%s", label, os_status_name(os_get_last_error_status()));
    log_info(message);
}

static int verify_task_delete_scheduler_pointer_snapshot(
    const char* context,
    OsTaskHandle task_a,
    OsTaskHandle task_b,
    OsTaskHandle task_c,
    uint32_t expected_task_count,
    uint32_t expected_ready_count,
    uint32_t expected_a_runs,
    uint32_t expected_b_runs,
    uint32_t expected_c_runs)
{
    if ((task_a != 0 && (os_task_get_state(task_a) != OS_TASK_READY || os_task_get_priority(task_a) != OS_TASK_PRIORITY_NORMAL || os_task_get_run_count(task_a) != expected_a_runs)) ||
        (task_b != 0 && (os_task_get_state(task_b) != OS_TASK_READY || os_task_get_priority(task_b) != OS_TASK_PRIORITY_NORMAL || os_task_get_run_count(task_b) != expected_b_runs)) ||
        (task_c != 0 && (os_task_get_state(task_c) != OS_TASK_READY || os_task_get_priority(task_c) != OS_TASK_PRIORITY_NORMAL || os_task_get_run_count(task_c) != expected_c_runs)) ||
        os_get_task_count() != expected_task_count ||
        os_get_ready_task_count() != expected_ready_count ||
        os_get_waiting_task_count() != 0U ||
        os_task_get_current() != 0 ||
        os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
        {
            log_last_os_error(context);
        }
        return 0;
    }

    return 1;
}

static int create_task_delete_scheduler_pointer_tasks(const TestBinary* wasm_binary, const char* name_a, const char* name_b, const char* name_c, OsTaskHandle* task_a, OsTaskHandle* task_b, OsTaskHandle* task_c, int code_base)
{
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    status = os_task_create(task_a, wasm_binary->bytes, wasm_binary->size, "app_main", name_a, TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s create status=%s handle=%s", name_a, os_status_name(status), *task_a == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || *task_a == 0) { return fail_test("task delete scheduler pointer failed to create task_a", code_base); }

    status = os_task_create(task_b, wasm_binary->bytes, wasm_binary->size, "app_main", name_b, TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s create status=%s handle=%s", name_b, os_status_name(status), *task_b == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || *task_b == 0) { return fail_test("task delete scheduler pointer failed to create task_b", code_base + 1); }

    status = os_task_create(task_c, wasm_binary->bytes, wasm_binary->size, "app_main", name_c, TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s create status=%s handle=%s", name_c, os_status_name(status), *task_c == 0 ? "null" : "non-null");
    log_info(message);
    if (status != OS_STATUS_OK || *task_c == 0) { return fail_test("task delete scheduler pointer failed to create task_c", code_base + 2); }

    return 0;
}

static int run_delete_last_scheduled_task_policy_test(int code_base)
{
    const char* test_name = "delete last scheduled task policy";
    TestBinary wasm_binary;
    OsTaskHandle task_a = 0;
    OsTaskHandle task_b = 0;
    OsTaskHandle task_c = 0;
    OsStatus status = OS_STATUS_OK;
    int no_task_status = 0;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;
    log_phase("delete last scheduled task setup");
    log_info("delete last scheduled task fixture: build/spin_forever.wasm");
    if (!read_binary_file("build/spin_forever.wasm", &wasm_binary)) { return fail_test("spin_forever.wasm missing or unreadable for delete last scheduled task policy", TEST_FAILURE_MISSING_WASM); }
    no_task_status = create_task_delete_scheduler_pointer_tasks(&wasm_binary, "delete_last_a_task", "delete_last_b_task", "delete_last_c_task", &task_a, &task_b, &task_c, code_base);
    if (no_task_status != 0) { free_binary(&wasm_binary); return no_task_status; }
    log_task_delete_scheduler_pointer_snapshot("delete last scheduled task setup", task_a, task_b, task_c);
    if (!verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 3U, 3U, 0U, 0U, 0U)) { free_binary(&wasm_binary); return fail_test("delete last scheduled task setup expectations failed", code_base + 3); }

    status = os_schedule();
    log_phase("delete last scheduled task after initial schedule");
    log_task_delete_scheduler_pointer_snapshot("delete last scheduled task after initial schedule", task_a, task_b, task_c);
    if (status != OS_STATUS_OK || !verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 3U, 3U, 1U, 0U, 0U)) { free_binary(&wasm_binary); return fail_test("delete last scheduled task initial schedule expectations failed", code_base + 4); }

    status = os_task_delete(task_a);
    task_a = 0;
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "delete_last_a_task delete status=%s", os_status_name(status));
    log_info(message);
    log_phase("delete last scheduled task after delete");
    log_task_delete_scheduler_pointer_snapshot("delete last scheduled task after delete", task_a, task_b, task_c);
    if (status != OS_STATUS_OK || !verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 2U, 2U, 0U, 0U, 0U)) { free_binary(&wasm_binary); return fail_test("delete last scheduled task delete expectations failed", code_base + 5); }

    status = os_schedule();
    log_phase("delete last scheduled task key schedule");
    log_task_delete_scheduler_pointer_snapshot("delete last scheduled task key schedule", task_a, task_b, task_c);
    if (status != OS_STATUS_OK || !verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 2U, 2U, 0U, 1U, 0U)) { free_binary(&wasm_binary); return fail_test("delete last scheduled task key scheduler selection failed", code_base + 6); }

    status = os_schedule();
    log_task_delete_scheduler_pointer_snapshot("delete last scheduled task follow-up schedule", task_a, task_b, task_c);
    if (status != OS_STATUS_OK || !verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 2U, 2U, 0U, 1U, 1U)) { free_binary(&wasm_binary); return fail_test("delete last scheduled task follow-up scheduler selection failed", code_base + 7); }

    log_phase("cleanup");
    status = os_task_delete(task_b);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("delete last scheduled task failed to delete task_b", code_base + 8); }
    status = os_task_delete(task_c);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("delete last scheduled task failed to delete task_c", code_base + 9); }
    no_task_status = verify_no_tasks(code_base + 10);
    if (no_task_status == 0) { log_task_counters("delete last scheduled task cleanup"); log_last_os_error("delete last scheduled task cleanup"); log_pass("delete last scheduled task policy final PASS"); }
    free_binary(&wasm_binary);
    return no_task_status;
}

static int run_delete_before_last_scheduled_task_policy_test(int code_base)
{
    const char* test_name = "delete before last scheduled task policy";
    TestBinary wasm_binary;
    OsTaskHandle task_a = 0;
    OsTaskHandle task_b = 0;
    OsTaskHandle task_c = 0;
    OsStatus status = OS_STATUS_OK;
    int no_task_status = 0;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;
    log_phase("delete before last scheduled task setup");
    log_info("delete before last scheduled task fixture: build/spin_forever.wasm");
    if (!read_binary_file("build/spin_forever.wasm", &wasm_binary)) { return fail_test("spin_forever.wasm missing or unreadable for delete before last scheduled task policy", TEST_FAILURE_MISSING_WASM); }
    no_task_status = create_task_delete_scheduler_pointer_tasks(&wasm_binary, "delete_before_a_task", "delete_before_b_task", "delete_before_c_task", &task_a, &task_b, &task_c, code_base);
    if (no_task_status != 0) { free_binary(&wasm_binary); return no_task_status; }
    log_task_delete_scheduler_pointer_snapshot("delete before last scheduled task setup", task_a, task_b, task_c);
    if (!verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 3U, 3U, 0U, 0U, 0U)) { free_binary(&wasm_binary); return fail_test("delete before last scheduled task setup expectations failed", code_base + 3); }

    status = os_schedule();
    if (status == OS_STATUS_OK) { status = os_schedule(); }
    log_phase("delete before last scheduled task after baseline schedules");
    log_task_delete_scheduler_pointer_snapshot("delete before last scheduled task after baseline schedules", task_a, task_b, task_c);
    if (status != OS_STATUS_OK || !verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 3U, 3U, 1U, 1U, 0U)) { free_binary(&wasm_binary); return fail_test("delete before last scheduled task baseline schedule expectations failed", code_base + 4); }

    status = os_task_delete(task_a);
    task_a = 0;
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "delete_before_a_task delete status=%s", os_status_name(status));
    log_info(message);
    log_phase("delete before last scheduled task after delete");
    log_task_delete_scheduler_pointer_snapshot("delete before last scheduled task after delete", task_a, task_b, task_c);
    if (status != OS_STATUS_OK || !verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 2U, 2U, 0U, 1U, 0U)) { free_binary(&wasm_binary); return fail_test("delete before last scheduled task delete expectations failed", code_base + 5); }

    status = os_schedule();
    log_phase("delete before last scheduled task key schedule");
    log_task_delete_scheduler_pointer_snapshot("delete before last scheduled task key schedule", task_a, task_b, task_c);
    if (status != OS_STATUS_OK || !verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 2U, 2U, 0U, 1U, 1U)) { free_binary(&wasm_binary); return fail_test("delete before last scheduled task key scheduler selection failed", code_base + 6); }

    status = os_schedule();
    log_task_delete_scheduler_pointer_snapshot("delete before last scheduled task follow-up schedule", task_a, task_b, task_c);
    if (status != OS_STATUS_OK || !verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 2U, 2U, 0U, 2U, 1U)) { free_binary(&wasm_binary); return fail_test("delete before last scheduled task follow-up scheduler selection failed", code_base + 7); }

    log_phase("cleanup");
    status = os_task_delete(task_b);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("delete before last scheduled task failed to delete task_b", code_base + 8); }
    status = os_task_delete(task_c);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("delete before last scheduled task failed to delete task_c", code_base + 9); }
    no_task_status = verify_no_tasks(code_base + 10);
    if (no_task_status == 0) { log_task_counters("delete before last scheduled task cleanup"); log_last_os_error("delete before last scheduled task cleanup"); log_pass("delete before last scheduled task policy final PASS"); }
    free_binary(&wasm_binary);
    return no_task_status;
}

static int run_delete_after_last_scheduled_task_policy_test(int code_base)
{
    const char* test_name = "delete after last scheduled task policy";
    TestBinary wasm_binary;
    OsTaskHandle task_a = 0;
    OsTaskHandle task_b = 0;
    OsTaskHandle task_c = 0;
    OsStatus status = OS_STATUS_OK;
    int no_task_status = 0;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;
    log_phase("delete after last scheduled task setup");
    log_info("delete after last scheduled task fixture: build/spin_forever.wasm");
    if (!read_binary_file("build/spin_forever.wasm", &wasm_binary)) { return fail_test("spin_forever.wasm missing or unreadable for delete after last scheduled task policy", TEST_FAILURE_MISSING_WASM); }
    no_task_status = create_task_delete_scheduler_pointer_tasks(&wasm_binary, "delete_after_a_task", "delete_after_b_task", "delete_after_c_task", &task_a, &task_b, &task_c, code_base);
    if (no_task_status != 0) { free_binary(&wasm_binary); return no_task_status; }
    log_task_delete_scheduler_pointer_snapshot("delete after last scheduled task setup", task_a, task_b, task_c);
    if (!verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 3U, 3U, 0U, 0U, 0U)) { free_binary(&wasm_binary); return fail_test("delete after last scheduled task setup expectations failed", code_base + 3); }

    status = os_schedule();
    log_phase("delete after last scheduled task after baseline schedule");
    log_task_delete_scheduler_pointer_snapshot("delete after last scheduled task after baseline schedule", task_a, task_b, task_c);
    if (status != OS_STATUS_OK || !verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 3U, 3U, 1U, 0U, 0U)) { free_binary(&wasm_binary); return fail_test("delete after last scheduled task baseline schedule expectations failed", code_base + 4); }

    status = os_task_delete(task_c);
    task_c = 0;
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "delete_after_c_task delete status=%s", os_status_name(status));
    log_info(message);
    log_phase("delete after last scheduled task after delete");
    log_task_delete_scheduler_pointer_snapshot("delete after last scheduled task after delete", task_a, task_b, task_c);
    if (status != OS_STATUS_OK || !verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 2U, 2U, 1U, 0U, 0U)) { free_binary(&wasm_binary); return fail_test("delete after last scheduled task delete expectations failed", code_base + 5); }

    status = os_schedule();
    log_phase("delete after last scheduled task key schedule");
    log_task_delete_scheduler_pointer_snapshot("delete after last scheduled task key schedule", task_a, task_b, task_c);
    if (status != OS_STATUS_OK || !verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 2U, 2U, 1U, 1U, 0U)) { free_binary(&wasm_binary); return fail_test("delete after last scheduled task key scheduler selection failed", code_base + 6); }

    status = os_schedule();
    log_task_delete_scheduler_pointer_snapshot("delete after last scheduled task follow-up schedule", task_a, task_b, task_c);
    if (status != OS_STATUS_OK || !verify_task_delete_scheduler_pointer_snapshot(test_name, task_a, task_b, task_c, 2U, 2U, 2U, 1U, 0U)) { free_binary(&wasm_binary); return fail_test("delete after last scheduled task follow-up scheduler selection failed", code_base + 7); }

    log_phase("cleanup");
    status = os_task_delete(task_a);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("delete after last scheduled task failed to delete task_a", code_base + 8); }
    status = os_task_delete(task_b);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("delete after last scheduled task failed to delete task_b", code_base + 9); }
    no_task_status = verify_no_tasks(code_base + 10);
    if (no_task_status == 0) { log_task_counters("delete after last scheduled task cleanup"); log_last_os_error("delete after last scheduled task cleanup"); log_pass("delete after last scheduled task policy final PASS"); }
    free_binary(&wasm_binary);
    return no_task_status;
}

static int run_task_delete_scheduler_pointer_policy_test(int code_base)
{
    int status = run_delete_last_scheduled_task_policy_test(code_base);
    if (status != 0)
    {
        return status;
    }

    status = run_delete_before_last_scheduled_task_policy_test(code_base + 30);
    if (status != 0)
    {
        return status;
    }

    return run_delete_after_last_scheduled_task_policy_test(code_base + 60);
}


static int run_tick_wraparound_delay_wake_policy_test(int code_base)
{
    const char* test_name = "tick wraparound delay wake";
    TestBinary delay_binary;
    TestBinary spin_binary;
    OsTaskHandle delay_task = 0;
    OsTaskHandle peer_task = 0;
    OsStatus status = OS_STATUS_OK;
    OsTaskState delay_state = OS_TASK_DEAD;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    uint32_t current_tick = 0U;
    int no_task_status = 0;

    delay_binary.bytes = 0;
    delay_binary.size = 0U;
    spin_binary.bytes = 0;
    spin_binary.size = 0U;

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s test start", test_name);
    log_phase(message);
    log_info("tick wraparound fixtures: build/delay_once.wasm and build/spin_forever.wasm");

    no_task_status = verify_no_tasks(code_base);
    if (no_task_status != 0)
    {
        return no_task_status;
    }

    log_phase("tick wraparound setup");
    current_tick = os_get_tick_ms();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "tick wraparound current tick before move=%u target=%u", current_tick, TEST_TICK_WRAP_START);
    log_info(message);
    os_tick(TEST_TICK_WRAP_START - current_tick);

    log_phase("after moving tick near wrap");
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "tick wraparound tick=%u", os_get_tick_ms());
    log_info(message);
    log_last_os_error(test_name);
    if (os_get_tick_ms() != TEST_TICK_WRAP_START)
    {
        return fail_test("tick wraparound failed to move OS tick to near-wrap target", code_base + 4);
    }

    if (!read_binary_file("build/delay_once.wasm", &delay_binary) ||
        !read_binary_file("build/spin_forever.wasm", &spin_binary))
    {
        free_binary(&delay_binary);
        free_binary(&spin_binary);
        return fail_test("tick wraparound fixtures missing or unreadable", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&delay_task, delay_binary.bytes, delay_binary.size, "app_main", "tick_wrap_delay_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || delay_task == 0) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("tick wraparound failed to create delay task", code_base + 5); }
    status = os_task_create(&peer_task, spin_binary.bytes, spin_binary.size, "app_main", "tick_wrap_peer_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || peer_task == 0) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("tick wraparound failed to create peer task", code_base + 6); }

    log_wake_policy_snapshot("tick wraparound initial", delay_task, peer_task, 0);
    if (os_task_get_state(delay_task) != OS_TASK_READY || os_task_get_state(peer_task) != OS_TASK_READY ||
        os_task_get_priority(delay_task) != OS_TASK_PRIORITY_NORMAL || os_task_get_priority(peer_task) != OS_TASK_PRIORITY_NORMAL ||
        os_get_task_count() != 2U || os_get_ready_task_count() != 2U || os_get_waiting_task_count() != 0U || os_task_get_current() != 0 ||
        os_task_get_run_count(delay_task) != 0U || os_task_get_run_count(peer_task) != 0U || os_get_tick_ms() != TEST_TICK_WRAP_START ||
        os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    { if (os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("tick wraparound initial expectations failed", code_base + 7); }

    status = os_schedule();
    log_phase("after delay task enters waiting");
    log_wake_policy_snapshot("tick wraparound after delay task enters waiting", delay_task, peer_task, 0);
    if (status != OS_STATUS_OK || os_task_get_state(delay_task) != OS_TASK_WAITING || os_task_get_state(peer_task) != OS_TASK_READY ||
        os_task_get_run_count(delay_task) != 1U || os_task_get_run_count(peer_task) != 0U || os_get_task_count() != 2U ||
        os_get_ready_task_count() != 1U || os_get_waiting_task_count() != 1U || os_task_get_current() != 0 ||
        os_get_tick_ms() != TEST_TICK_WRAP_START || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    { if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("tick wraparound delay waiting expectations failed", code_base + 8); }

    os_tick(5U);
    log_phase("after early 5 ms tick");
    log_wake_policy_snapshot("tick wraparound after early 5 ms tick", delay_task, peer_task, 0);
    if (os_get_tick_ms() != 0xFFFFFFFDU || os_task_get_state(delay_task) != OS_TASK_WAITING || os_task_get_state(peer_task) != OS_TASK_READY ||
        os_get_ready_task_count() != 1U || os_get_waiting_task_count() != 1U || os_task_get_run_count(delay_task) != 1U ||
        os_task_get_run_count(peer_task) != 0U || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    { if (os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("tick wraparound woke delay task before wrapped deadline", code_base + 9); }

    status = os_schedule();
    log_phase("while waiting peer schedule");
    log_wake_policy_snapshot("tick wraparound while waiting peer schedule", delay_task, peer_task, 0);
    if (status != OS_STATUS_OK || os_task_get_run_count(peer_task) != 1U || os_task_get_state(delay_task) != OS_TASK_WAITING ||
        os_get_ready_task_count() != 1U || os_get_waiting_task_count() != 1U || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    { if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("tick wraparound peer schedule while delayed failed", code_base + 10); }

    os_tick(5U);
    log_phase("after wrapped wake tick");
    log_wake_policy_snapshot("tick wraparound after wrapped wake tick", delay_task, peer_task, 0);
    if (os_get_tick_ms() != 0x00000002U || os_task_get_state(delay_task) != OS_TASK_READY || os_task_get_state(peer_task) != OS_TASK_READY ||
        os_get_ready_task_count() != 2U || os_get_waiting_task_count() != 0U || os_task_get_run_count(delay_task) != 1U ||
        os_task_get_run_count(peer_task) != 1U || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    { if (os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("tick wraparound did not wake delay task at wrapped deadline", code_base + 11); }

    status = os_schedule();
    log_phase("after resumed delay task schedule");
    delay_state = os_task_get_state(delay_task);
    log_wake_policy_snapshot("tick wraparound after resumed delay task schedule", delay_task, peer_task, 0);
    if (status != OS_STATUS_OK || os_task_get_run_count(delay_task) != 2U || os_task_get_run_count(peer_task) != 1U ||
        os_get_waiting_task_count() != 0U || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    { if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("tick wraparound resumed delay task expectations failed", code_base + 12); }

    if (delay_state == OS_TASK_DEAD)
    {
        if (os_get_task_count() != 1U || os_get_ready_task_count() != 1U) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("tick wraparound dead delay task counters failed", code_base + 13); }
    }
    else if (delay_state == OS_TASK_READY)
    {
        if (os_get_task_count() != 2U || os_get_ready_task_count() != 2U) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("tick wraparound ready delay task counters failed", code_base + 14); }
    }
    else
    {
        free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("tick wraparound delay task ended in unexpected state", code_base + 15);
    }

    log_phase("cleanup");
    if (delay_state != OS_TASK_DEAD)
    {
        status = os_task_delete(delay_task);
        if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("tick wraparound failed to delete delay task", code_base + 16); }
    }
    status = os_task_delete(peer_task);
    if (status != OS_STATUS_OK && status != OS_STATUS_TASK_DEAD) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("tick wraparound failed to delete peer task", code_base + 17); }

    log_task_counters("tick wraparound cleanup");
    log_last_os_error("tick wraparound cleanup");
    if (os_get_last_error_status() == OS_STATUS_WASM_ERROR) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("tick wraparound cleanup left unexpected WASM diagnostics", code_base + 18); }

    no_task_status = verify_no_tasks(code_base + 19);
    if (no_task_status == 0)
    {
        log_pass("tick wraparound delay wake policy final PASS");
    }

    free_binary(&delay_binary);
    free_binary(&spin_binary);
    return no_task_status;
}

static int run_equal_priority_wake_ordering_test(int code_base)
{
    const char* test_name = "equal-priority wake ordering";
    TestBinary delay_binary;
    TestBinary spin_binary;
    OsTaskHandle wake_task = 0;
    OsTaskHandle peer_b_task = 0;
    OsTaskHandle peer_c_task = 0;
    OsStatus status = OS_STATUS_OK;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int no_task_status = 0;

    delay_binary.bytes = 0;
    delay_binary.size = 0U;
    spin_binary.bytes = 0;
    spin_binary.size = 0U;

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s test start", test_name);
    log_phase(message);
    log_info("equal-priority wake ordering fixtures: build/delay_once.wasm and build/spin_forever.wasm");

    if (!read_binary_file("build/delay_once.wasm", &delay_binary) ||
        !read_binary_file("build/spin_forever.wasm", &spin_binary))
    {
        free_binary(&delay_binary);
        free_binary(&spin_binary);
        return fail_test("wake ordering fixture missing or unreadable", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&wake_task, delay_binary.bytes, delay_binary.size, "app_main", "equal_wake_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || wake_task == 0) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("equal wake task create failed", code_base); }
    status = os_task_create(&peer_b_task, spin_binary.bytes, spin_binary.size, "app_main", "equal_peer_b_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || peer_b_task == 0) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("equal peer b task create failed", code_base + 1); }
    status = os_task_create(&peer_c_task, spin_binary.bytes, spin_binary.size, "app_main", "equal_peer_c_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_NORMAL);
    if (status != OS_STATUS_OK || peer_c_task == 0) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("equal peer c task create failed", code_base + 2); }

    log_wake_policy_snapshot("equal-priority initial", wake_task, peer_b_task, peer_c_task);
    if (os_task_get_state(wake_task) != OS_TASK_READY || os_task_get_state(peer_b_task) != OS_TASK_READY || os_task_get_state(peer_c_task) != OS_TASK_READY ||
        os_get_task_count() != 3U || os_get_ready_task_count() != 3U || os_get_waiting_task_count() != 0U || os_task_get_current() != 0 ||
        os_task_get_run_count(wake_task) != 0U || os_task_get_run_count(peer_b_task) != 0U || os_task_get_run_count(peer_c_task) != 0U ||
        os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        if (os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name);
        free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("equal-priority wake ordering initial expectations failed", code_base + 3);
    }

    status = os_schedule();
    log_wake_policy_snapshot("equal-priority after wake task delay", wake_task, peer_b_task, peer_c_task);
    if (status != OS_STATUS_OK || os_task_get_state(wake_task) != OS_TASK_WAITING || os_task_get_run_count(wake_task) != 1U ||
        os_task_get_run_count(peer_b_task) != 0U || os_task_get_run_count(peer_c_task) != 0U || os_get_ready_task_count() != 2U ||
        os_get_waiting_task_count() != 1U || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    { if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("equal-priority first schedule expectations failed", code_base + 4); }

    status = os_schedule();
    log_wake_policy_snapshot("equal-priority after peer b slice", wake_task, peer_b_task, peer_c_task);
    if (status != OS_STATUS_OK || os_task_get_run_count(peer_b_task) != 1U || os_task_get_run_count(peer_c_task) != 0U ||
        os_task_get_state(wake_task) != OS_TASK_WAITING || os_get_ready_task_count() != 2U || os_get_waiting_task_count() != 1U || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    { if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("equal-priority peer b schedule expectations failed", code_base + 5); }

    os_tick(10U);
    log_wake_policy_snapshot("equal-priority after manual wake tick", wake_task, peer_b_task, peer_c_task);
    if (os_task_get_state(wake_task) != OS_TASK_READY || os_get_task_count() != 3U || os_get_ready_task_count() != 3U || os_get_waiting_task_count() != 0U)
    { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("equal-priority wake tick expectations failed", code_base + 6); }

    status = os_schedule();
    log_wake_policy_snapshot("equal-priority after key peer c slice", wake_task, peer_b_task, peer_c_task);
    if (status != OS_STATUS_OK || os_task_get_run_count(peer_c_task) != 1U || os_task_get_run_count(peer_b_task) != 1U ||
        os_task_get_run_count(wake_task) != 1U || os_task_get_state(wake_task) != OS_TASK_READY || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    { if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("equal-priority woken task skipped ready peer", code_base + 7); }

    status = os_schedule();
    log_wake_policy_snapshot("equal-priority after woken task resume", wake_task, peer_b_task, peer_c_task);
    if (status != OS_STATUS_OK || os_task_get_run_count(wake_task) != 2U || os_task_get_run_count(peer_b_task) != 1U ||
        os_task_get_run_count(peer_c_task) != 1U || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    { if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("equal-priority woken task resume expectations failed", code_base + 8); }

    status = os_task_delete(wake_task);
    if (status != OS_STATUS_OK) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("equal-priority wake task delete failed", code_base + 9); }
    status = os_task_delete(peer_b_task);
    if (status != OS_STATUS_OK) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("equal-priority peer b delete failed", code_base + 10); }
    status = os_task_delete(peer_c_task);
    if (status != OS_STATUS_OK) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("equal-priority peer c delete failed", code_base + 11); }

    no_task_status = verify_no_tasks(code_base + 12);
    if (no_task_status == 0) log_pass("equal-priority wake ordering final PASS");
    free_binary(&delay_binary);
    free_binary(&spin_binary);
    return no_task_status;
}

static int run_high_priority_wake_ordering_test(int code_base)
{
    const char* test_name = "higher-priority wake precedence";
    TestBinary delay_binary;
    TestBinary spin_binary;
    OsTaskHandle high_wake_task = 0;
    OsTaskHandle low_ready_task = 0;
    OsStatus status = OS_STATUS_OK;
    int no_task_status = 0;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    delay_binary.bytes = 0; delay_binary.size = 0U; spin_binary.bytes = 0; spin_binary.size = 0U;
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s test start", test_name);
    log_phase(message);
    log_info("higher-priority wake precedence fixtures: build/delay_once.wasm and build/spin_forever.wasm");

    if (!read_binary_file("build/delay_once.wasm", &delay_binary) || !read_binary_file("build/spin_forever.wasm", &spin_binary))
    { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("higher-priority wake fixture missing or unreadable", TEST_FAILURE_MISSING_WASM); }

    status = os_task_create(&high_wake_task, delay_binary.bytes, delay_binary.size, "app_main", "high_wake_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_HIGH);
    if (status != OS_STATUS_OK || high_wake_task == 0) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("high wake task create failed", code_base); }
    status = os_task_create(&low_ready_task, spin_binary.bytes, spin_binary.size, "app_main", "low_ready_task", TEST_WASM_STACK_SIZE, OS_TASK_PRIORITY_LOW);
    if (status != OS_STATUS_OK || low_ready_task == 0) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("low ready task create failed", code_base + 1); }

    log_wake_policy_snapshot("higher-priority initial", high_wake_task, low_ready_task, 0);
    if (os_task_get_state(high_wake_task) != OS_TASK_READY || os_task_get_state(low_ready_task) != OS_TASK_READY || os_get_task_count() != 2U ||
        os_get_ready_task_count() != 2U || os_get_waiting_task_count() != 0U || os_task_get_current() != 0 || os_task_get_run_count(high_wake_task) != 0U ||
        os_task_get_run_count(low_ready_task) != 0U || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    { if (os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("higher-priority wake initial expectations failed", code_base + 2); }

    status = os_schedule();
    log_wake_policy_snapshot("higher-priority after high delay", high_wake_task, low_ready_task, 0);
    if (status != OS_STATUS_OK || os_task_get_state(high_wake_task) != OS_TASK_WAITING || os_task_get_run_count(high_wake_task) != 1U ||
        os_task_get_run_count(low_ready_task) != 0U || os_get_ready_task_count() != 1U || os_get_waiting_task_count() != 1U || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    { if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("higher-priority first schedule expectations failed", code_base + 3); }

    status = os_schedule();
    log_wake_policy_snapshot("higher-priority after low slice", high_wake_task, low_ready_task, 0);
    if (status != OS_STATUS_OK || os_task_get_run_count(low_ready_task) != 1U || os_task_get_state(high_wake_task) != OS_TASK_WAITING || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    { if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("higher-priority low slice expectations failed", code_base + 4); }

    os_tick(10U);
    log_wake_policy_snapshot("higher-priority after manual wake tick", high_wake_task, low_ready_task, 0);
    if (os_task_get_state(high_wake_task) != OS_TASK_READY || os_task_get_state(low_ready_task) != OS_TASK_READY || os_get_ready_task_count() != 2U || os_get_waiting_task_count() != 0U)
    { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("higher-priority wake tick expectations failed", code_base + 5); }

    status = os_schedule();
    log_wake_policy_snapshot("higher-priority after key high wake slice", high_wake_task, low_ready_task, 0);
    if (status != OS_STATUS_OK || os_task_get_run_count(high_wake_task) != 2U || os_task_get_run_count(low_ready_task) != 1U || os_task_get_current() != 0 || os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    { if (status == OS_STATUS_WASM_ERROR || os_get_last_error_status() == OS_STATUS_WASM_ERROR) log_last_os_error(test_name); free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("higher-priority woken task did not preempt lower-priority ready task", code_base + 6); }

    status = os_task_delete(high_wake_task);
    if (status != OS_STATUS_OK) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("high wake task delete failed", code_base + 7); }
    status = os_task_delete(low_ready_task);
    if (status != OS_STATUS_OK) { free_binary(&delay_binary); free_binary(&spin_binary); return fail_test("low ready task delete failed", code_base + 8); }

    no_task_status = verify_no_tasks(code_base + 9);
    if (no_task_status == 0) log_pass("higher-priority wake precedence final PASS");
    free_binary(&delay_binary);
    free_binary(&spin_binary);
    return no_task_status;
}

static int run_wake_time_scheduler_policy_test(int code_base)
{
    int status = run_equal_priority_wake_ordering_test(code_base);
    if (status != 0)
    {
        return status;
    }

    return run_high_priority_wake_ordering_test(code_base + 40);
}

static int verify_no_unexpected_wasm_diagnostics(const char* test_name, int code)
{
    if (os_get_last_error_status() == OS_STATUS_WASM_ERROR)
    {
        log_last_os_error(test_name);
        return fail_test("task deletion state coverage left unexpected WASM diagnostics", code);
    }

    return 0;
}

static int run_task_deletion_state_coverage_test(int code_base)
{
    const char* test_name = "task deletion state coverage";
    TestBinary simple_binary;
    TestBinary delay_binary;
    TestBinary spin_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    OsTaskState state = OS_TASK_DEAD;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int no_task_status = 0;
    int diagnostics_status = 0;

    simple_binary.bytes = 0;
    simple_binary.size = 0U;
    delay_binary.bytes = 0;
    delay_binary.size = 0U;
    spin_binary.bytes = 0;
    spin_binary.size = 0U;

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s test start", test_name);
    log_phase(message);

    if (!read_binary_file("build/simple_loop.wasm", &simple_binary))
    {
        log_fail("task deletion state coverage could not load simple_loop.wasm");
        return fail_test("simple_loop.wasm missing or unreadable for task deletion state coverage", TEST_FAILURE_MISSING_WASM);
    }

    if (!read_binary_file("build/delay_once.wasm", &delay_binary))
    {
        free_binary(&simple_binary);
        log_fail("task deletion state coverage could not load delay_once.wasm");
        return fail_test("delay_once.wasm missing or unreadable for task deletion state coverage", TEST_FAILURE_MISSING_WASM);
    }

    if (!read_binary_file("build/spin_forever.wasm", &spin_binary))
    {
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        log_fail("task deletion state coverage could not load spin_forever.wasm");
        return fail_test("spin_forever.wasm missing or unreadable for task deletion state coverage", TEST_FAILURE_MISSING_WASM);
    }

    log_phase("task deletion state coverage READY task scenario");
    status = os_task_create(&task, simple_binary.bytes, simple_binary.size, "app_main", "delete_ready_task", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    state = os_task_get_state(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "delete_ready_task create status=%s state=%s handle=%s", os_status_name(status), os_task_state_name(state), task == 0 ? "null" : "non-null");
    log_info(message);
    log_task_counters("delete_ready_task after create");
    if (status != OS_STATUS_OK || task == 0 || state != OS_TASK_READY ||
        os_get_task_count() != 1U || os_get_ready_task_count() != 1U ||
        os_get_waiting_task_count() != 0U)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return fail_test("READY deletion scenario did not create the expected READY task", code_base);
    }

    status = os_task_delete(task);
    task = 0;
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "delete_ready_task delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return fail_test("READY deletion scenario failed to delete task", code_base + 1);
    }
    diagnostics_status = verify_no_unexpected_wasm_diagnostics(test_name, code_base + 2);
    if (diagnostics_status != 0)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return diagnostics_status;
    }
    no_task_status = verify_no_tasks(code_base + 3);
    if (no_task_status != 0)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return no_task_status;
    }
    log_pass("task deletion state coverage READY task scenario PASS");

    log_phase("task deletion state coverage WAITING task scenario");
    status = os_task_create(&task, delay_binary.bytes, delay_binary.size, "app_main", "delete_waiting_task", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    if (status != OS_STATUS_OK || task == 0)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return fail_test("WAITING deletion scenario failed to create task", code_base + 4);
    }
    status = os_schedule();
    state = os_task_get_state(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "delete_waiting_task first schedule status=%s state=%s", os_status_name(status), os_task_state_name(state));
    log_info(message);
    log_task_counters("delete_waiting_task after delay");
    if (status != OS_STATUS_OK || state != OS_TASK_WAITING || os_get_waiting_task_count() != 1U)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return fail_test("WAITING deletion scenario did not enter OS_TASK_WAITING", code_base + 5);
    }
    status = os_task_delete(task);
    task = 0;
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "delete_waiting_task delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return fail_test("WAITING deletion scenario failed to delete task", code_base + 6);
    }
    diagnostics_status = verify_no_unexpected_wasm_diagnostics(test_name, code_base + 7);
    if (diagnostics_status != 0)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return diagnostics_status;
    }
    no_task_status = verify_no_tasks(code_base + 8);
    if (no_task_status != 0)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return no_task_status;
    }
    log_pass("task deletion state coverage WAITING task scenario PASS");

    log_phase("task deletion state coverage SUSPENDED task scenario");
    status = os_task_create(&task, simple_binary.bytes, simple_binary.size, "app_main", "delete_suspended_task", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    if (status != OS_STATUS_OK || task == 0)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return fail_test("SUSPENDED deletion scenario failed to create task", code_base + 9);
    }
    status = os_task_suspend(task);
    state = os_task_get_state(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "delete_suspended_task suspend status=%s state=%s", os_status_name(status), os_task_state_name(state));
    log_info(message);
    log_task_counters("delete_suspended_task after suspend");
    if (status != OS_STATUS_OK || state != OS_TASK_SUSPENDED ||
        os_get_task_count() != 1U || os_get_ready_task_count() != 0U ||
        os_get_waiting_task_count() != 0U)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return fail_test("SUSPENDED deletion scenario did not enter the expected suspended counters", code_base + 10);
    }
    status = os_task_delete(task);
    task = 0;
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "delete_suspended_task delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return fail_test("SUSPENDED deletion scenario failed to delete task", code_base + 11);
    }
    diagnostics_status = verify_no_unexpected_wasm_diagnostics(test_name, code_base + 12);
    if (diagnostics_status != 0)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return diagnostics_status;
    }
    no_task_status = verify_no_tasks(code_base + 13);
    if (no_task_status != 0)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return no_task_status;
    }
    log_pass("task deletion state coverage SUSPENDED task scenario PASS");

    log_phase("task deletion state coverage long-running READY-after-slice scenario");
    status = os_task_create(&task, spin_binary.bytes, spin_binary.size, "app_main", "delete_spin_forever_task", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    if (status != OS_STATUS_OK || task == 0)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return fail_test("long-running READY-after-slice deletion scenario failed to create task", code_base + 14);
    }
    status = os_schedule();
    state = os_task_get_state(task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "delete_spin_forever_task schedule status=%s state=%s current=%s", os_status_name(status), os_task_state_name(state), os_task_get_current() == 0 ? "null" : "non-null");
    log_info(message);
    log_task_counters("delete_spin_forever_task after bounded fuel slice");
    if (status != OS_STATUS_OK || state != OS_TASK_READY || state == OS_TASK_DEAD || os_task_get_current() != 0)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return fail_test("long-running READY-after-slice deletion scenario did not return to clean READY state", code_base + 15);
    }
    status = os_task_delete(task);
    task = 0;
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "delete_spin_forever_task delete status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return fail_test("long-running READY-after-slice deletion scenario failed to delete task", code_base + 16);
    }
    diagnostics_status = verify_no_unexpected_wasm_diagnostics(test_name, code_base + 17);
    if (diagnostics_status != 0)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return diagnostics_status;
    }
    no_task_status = verify_no_tasks(code_base + 18);
    if (no_task_status != 0)
    {
        free_binary(&spin_binary);
        free_binary(&delay_binary);
        free_binary(&simple_binary);
        return no_task_status;
    }
    log_pass("task deletion state coverage long-running READY-after-slice scenario PASS");

    log_task_counters("task deletion state coverage final counters");
    log_pass("task deletion state coverage final PASS");

    free_binary(&spin_binary);
    free_binary(&delay_binary);
    free_binary(&simple_binary);
    return 0;
}


static int run_task_snapshot_host_api_test(int code_base)
{
    const char* test_name = "task snapshot host API";
    TestBinary wasm_binary;
    OsTaskHandle source_task = 0;
    OsTaskHandle restore_task = 0;
    OsStatus status = OS_STATUS_OK;
    OsTaskState source_state = OS_TASK_DEAD;
    uint32_t snapshot_size = 0U;
    uint32_t written_size = 0U;
    uint32_t small_written_size = 0U;
    uint8_t small_buffer[1];
    uint8_t* snapshot_buffer = 0;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;
    small_buffer[0] = 0U;

    log_phase(test_name);

    status = os_init();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_init status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK) { return fail_test("task snapshot host API os_init failed", code_base); }

    status = os_task_get_snapshot_size(0, &snapshot_size);
    if (status != OS_STATUS_INVALID_ARGUMENT) { return fail_test("snapshot size accepted null task", code_base + 1); }
    status = os_task_get_snapshot_size(source_task, 0);
    if (status != OS_STATUS_INVALID_ARGUMENT) { return fail_test("snapshot size accepted null out_size", code_base + 2); }
    status = os_task_save_snapshot(0, small_buffer, 1U, &written_size);
    if (status != OS_STATUS_INVALID_ARGUMENT) { return fail_test("snapshot save accepted null task", code_base + 3); }
    status = os_task_save_snapshot(source_task, 0, 1U, &written_size);
    if (status != OS_STATUS_INVALID_ARGUMENT) { return fail_test("snapshot save accepted null buffer", code_base + 4); }
    status = os_task_save_snapshot(source_task, small_buffer, 0U, &written_size);
    if (status != OS_STATUS_INVALID_ARGUMENT) { return fail_test("snapshot save accepted zero buffer size", code_base + 5); }
    status = os_task_save_snapshot(source_task, small_buffer, 1U, 0);
    if (status != OS_STATUS_INVALID_ARGUMENT) { return fail_test("snapshot save accepted null out_size", code_base + 6); }
    status = os_task_load_snapshot(0, small_buffer, 1U);
    if (status != OS_STATUS_INVALID_ARGUMENT) { return fail_test("snapshot load accepted null task", code_base + 7); }
    status = os_task_load_snapshot(source_task, 0, 1U);
    if (status != OS_STATUS_INVALID_ARGUMENT) { return fail_test("snapshot load accepted null buffer", code_base + 8); }
    status = os_task_load_snapshot(source_task, small_buffer, 0U);
    if (status != OS_STATUS_INVALID_ARGUMENT) { return fail_test("snapshot load accepted zero buffer size", code_base + 9); }

    if (!read_binary_file("build/snapshot_yield.wasm", &wasm_binary))
    {
        log_fail("WASM file load failure path=build/snapshot_yield.wasm");
        return fail_test("missing snapshot_yield.wasm", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&source_task, wasm_binary.bytes, wasm_binary.size, "app_main", "snapshot_source", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s create source status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK) { free_binary(&wasm_binary); return fail_test("snapshot source task create failed", code_base + 10); }

    status = os_schedule();
    source_state = os_task_get_state(source_task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s first schedule status=%s source_state=%s", test_name, os_status_name(status), os_task_state_name(source_state));
    log_info(message);
    if (status != OS_STATUS_OK || source_state == OS_TASK_DEAD) { free_binary(&wasm_binary); return fail_test("snapshot source did not suspend/yield", code_base + 11); }

    status = os_task_get_snapshot_size(source_task, &snapshot_size);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s snapshot size status=%s size=%u", test_name, os_status_name(status), snapshot_size);
    log_info(message);
    if (status != OS_STATUS_OK || snapshot_size == 0U) { free_binary(&wasm_binary); return fail_test("snapshot size query failed", code_base + 12); }

    small_written_size = 0U;
    status = os_task_save_snapshot(source_task, small_buffer, 1U, &small_written_size);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s small save status=%s required=%u", test_name, os_status_name(status), small_written_size);
    log_info(message);
    if (status != OS_STATUS_BUFFER_TOO_SMALL || (small_written_size != 0U && small_written_size <= 1U)) { free_binary(&wasm_binary); return fail_test("snapshot small buffer behavior failed", code_base + 13); }

    snapshot_buffer = (uint8_t*)malloc(snapshot_size);
    if (snapshot_buffer == 0) { free_binary(&wasm_binary); return fail_test("snapshot buffer allocation failed", code_base + 14); }

    status = os_task_save_snapshot(source_task, snapshot_buffer, snapshot_size, &written_size);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s save status=%s written=%u capacity=%u", test_name, os_status_name(status), written_size, snapshot_size);
    log_info(message);
    if (status != OS_STATUS_OK || written_size == 0U || written_size > snapshot_size) { free(snapshot_buffer); free_binary(&wasm_binary); return fail_test("snapshot save failed", code_base + 15); }

    status = os_task_create(&restore_task, wasm_binary.bytes, wasm_binary.size, "app_main", "snapshot_restore", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    if (status != OS_STATUS_OK) { free(snapshot_buffer); free_binary(&wasm_binary); return fail_test("snapshot restore task create failed", code_base + 16); }

    status = os_task_load_snapshot(restore_task, snapshot_buffer, written_size);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s load status=%s restore_state=%s", test_name, os_status_name(status), os_task_state_name(os_task_get_state(restore_task)));
    log_info(message);
    if (status != OS_STATUS_OK) { free(snapshot_buffer); free_binary(&wasm_binary); return fail_test("snapshot load failed", code_base + 17); }

    status = os_schedule();
    log_exit_metadata_task_snapshot("snapshot restore after resume", restore_task);
    if (status != OS_STATUS_OK || os_task_get_state(restore_task) != OS_TASK_DEAD || os_task_get_exit_reason(restore_task) != OS_TASK_EXIT_RETURNED || os_task_get_exit_code(restore_task) != 12U)
    {
        free(snapshot_buffer);
        free_binary(&wasm_binary);
        return fail_test("snapshot restored task did not return expected result", code_base + 18);
    }

    status = os_task_delete(source_task);
    if (status != OS_STATUS_OK) { free(snapshot_buffer); free_binary(&wasm_binary); return fail_test("snapshot source delete failed", code_base + 19); }
    status = os_task_delete(restore_task);
    if (status != OS_STATUS_OK) { free(snapshot_buffer); free_binary(&wasm_binary); return fail_test("snapshot restore delete failed", code_base + 20); }

    free(snapshot_buffer);
    free_binary(&wasm_binary);

    if (verify_no_tasks(code_base + 21) != 0) { return code_base + 21; }

    log_pass("task snapshot host API test final PASS");
    return 0;
}

static int run_task_snapshot_after_source_delete_test(int code_base)
{
    const char* test_name = "task snapshot after source delete";
    TestBinary wasm_binary;
    OsTaskHandle source_task = 0;
    OsTaskHandle restore_task = 0;
    OsStatus status = OS_STATUS_OK;
    OsTaskState source_state = OS_TASK_DEAD;
    uint32_t snapshot_size = 0U;
    uint32_t written_size = 0U;
    uint8_t* snapshot_buffer = 0;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int no_task_status = 0;

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    log_phase(test_name);

    os_shutdown();
    status = os_init();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_init status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK) { return fail_test("task snapshot after source delete os_init failed", code_base); }

    if (!read_binary_file("build/snapshot_yield.wasm", &wasm_binary))
    {
        log_fail("WASM file load failure path=build/snapshot_yield.wasm");
        return fail_test("missing snapshot_yield.wasm for source delete snapshot test", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&source_task, wasm_binary.bytes, wasm_binary.size, "app_main", "snapshot_delete_source", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s create source status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK || source_task == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("snapshot source delete test source task create failed", code_base + 1);
    }

    status = os_schedule();
    source_state = os_task_get_state(source_task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s first schedule status=%s source_state=%s", test_name, os_status_name(status), os_task_state_name(source_state));
    log_info(message);
    if (status != OS_STATUS_OK || source_state == OS_TASK_DEAD)
    {
        (void)os_task_delete(source_task);
        free_binary(&wasm_binary);
        return fail_test("snapshot source delete test source task did not reach snapshot state", code_base + 2);
    }

    status = os_task_get_snapshot_size(source_task, &snapshot_size);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s snapshot size status=%s size=%u", test_name, os_status_name(status), snapshot_size);
    log_info(message);
    if (status != OS_STATUS_OK || snapshot_size == 0U)
    {
        (void)os_task_delete(source_task);
        free_binary(&wasm_binary);
        return fail_test("snapshot source delete test snapshot size query failed", code_base + 3);
    }

    snapshot_buffer = (uint8_t*)malloc(snapshot_size);
    if (snapshot_buffer == 0)
    {
        (void)os_task_delete(source_task);
        free_binary(&wasm_binary);
        return fail_test("snapshot source delete test snapshot buffer allocation failed", code_base + 4);
    }

    status = os_task_save_snapshot(source_task, snapshot_buffer, snapshot_size, &written_size);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s save status=%s written=%u capacity=%u", test_name, os_status_name(status), written_size, snapshot_size);
    log_info(message);
    if (status != OS_STATUS_OK || written_size == 0U || written_size > snapshot_size)
    {
        free(snapshot_buffer);
        (void)os_task_delete(source_task);
        free_binary(&wasm_binary);
        return fail_test("snapshot source delete test snapshot save failed", code_base + 5);
    }

    status = os_task_delete(source_task);
    source_task = 0;
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s delete source status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free(snapshot_buffer);
        free_binary(&wasm_binary);
        return fail_test("snapshot source delete test source delete failed", code_base + 6);
    }

    no_task_status = verify_no_tasks(code_base + 7);
    if (no_task_status != 0)
    {
        free(snapshot_buffer);
        free_binary(&wasm_binary);
        return no_task_status;
    }

    status = os_task_create(&restore_task, wasm_binary.bytes, wasm_binary.size, "app_main", "snapshot_delete_restore", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s create restore status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK || restore_task == 0)
    {
        free(snapshot_buffer);
        free_binary(&wasm_binary);
        return fail_test("snapshot source delete test restore task create failed", code_base + 11);
    }

    status = os_task_load_snapshot(restore_task, snapshot_buffer, written_size);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s load status=%s restore_state=%s", test_name, os_status_name(status), os_task_state_name(os_task_get_state(restore_task)));
    log_info(message);
    if (status != OS_STATUS_OK || os_task_get_state(restore_task) != OS_TASK_READY)
    {
        (void)os_task_delete(restore_task);
        free(snapshot_buffer);
        free_binary(&wasm_binary);
        return fail_test("snapshot source delete test snapshot load failed", code_base + 12);
    }

    status = os_schedule();
    log_exit_metadata_task_snapshot("snapshot after source delete restore after resume", restore_task);
    if (status != OS_STATUS_OK || os_task_get_state(restore_task) != OS_TASK_DEAD || os_task_get_exit_reason(restore_task) != OS_TASK_EXIT_RETURNED || os_task_get_exit_code(restore_task) != 12U)
    {
        (void)os_task_delete(restore_task);
        free(snapshot_buffer);
        free_binary(&wasm_binary);
        return fail_test("snapshot source delete test restored task did not return expected result", code_base + 13);
    }

    status = os_task_delete(restore_task);
    restore_task = 0;
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s delete restore status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free(snapshot_buffer);
        free_binary(&wasm_binary);
        return fail_test("snapshot source delete test restore delete failed", code_base + 14);
    }

    free(snapshot_buffer);
    snapshot_buffer = 0;
    free_binary(&wasm_binary);

    no_task_status = verify_no_tasks(code_base + 15);
    if (no_task_status != 0)
    {
        return no_task_status;
    }

    log_pass("task snapshot after source delete final PASS");
    return 0;
}

static int run_task_snapshot_waiting_restore_test(int code_base)
{
    const char* test_name = "task snapshot waiting restore";
    TestBinary wasm_binary;
    OsTaskHandle source_task = 0;
    OsTaskHandle restore_task = 0;
    OsStatus status = OS_STATUS_OK;
    OsTaskState source_state = OS_TASK_DEAD;
    uint32_t snapshot_size = 0U;
    uint32_t written_size = 0U;
    uint8_t* snapshot_buffer = 0;
    char message[TEST_MESSAGE_BUFFER_SIZE];
    int no_task_status = 0;

    wasm_binary.bytes = 0;
    wasm_binary.size = 0U;

    log_phase(test_name);

    os_shutdown();
    status = os_init();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s os_init status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK) { return fail_test("task snapshot waiting restore os_init failed", code_base); }

    if (!read_binary_file("build/snapshot_delay.wasm", &wasm_binary))
    {
        log_fail("WASM file load failure path=build/snapshot_delay.wasm");
        return fail_test("missing snapshot_delay.wasm for waiting restore snapshot test", TEST_FAILURE_MISSING_WASM);
    }

    status = os_task_create(&source_task, wasm_binary.bytes, wasm_binary.size, "app_main", "snapshot_wait_source", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s create source status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK || source_task == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("snapshot waiting restore source task create failed", code_base + 1);
    }

    status = os_schedule();
    source_state = os_task_get_state(source_task);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s first schedule status=%s source_state=%s", test_name, os_status_name(status), os_task_state_name(source_state));
    log_info(message);
    if (status != OS_STATUS_OK || source_state != OS_TASK_WAITING)
    {
        (void)os_task_delete(source_task);
        free_binary(&wasm_binary);
        return fail_test("snapshot waiting restore source task did not enter waiting state", code_base + 2);
    }

    status = os_task_get_snapshot_size(source_task, &snapshot_size);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s snapshot size status=%s size=%u", test_name, os_status_name(status), snapshot_size);
    log_info(message);
    if (status != OS_STATUS_OK || snapshot_size == 0U)
    {
        (void)os_task_delete(source_task);
        free_binary(&wasm_binary);
        return fail_test("snapshot waiting restore snapshot size query failed", code_base + 3);
    }

    snapshot_buffer = (uint8_t*)malloc(snapshot_size);
    if (snapshot_buffer == 0)
    {
        (void)os_task_delete(source_task);
        free_binary(&wasm_binary);
        return fail_test("snapshot waiting restore snapshot buffer allocation failed", code_base + 4);
    }

    status = os_task_save_snapshot(source_task, snapshot_buffer, snapshot_size, &written_size);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s save status=%s written=%u capacity=%u", test_name, os_status_name(status), written_size, snapshot_size);
    log_info(message);
    if (status != OS_STATUS_OK || written_size == 0U || written_size > snapshot_size)
    {
        free(snapshot_buffer);
        (void)os_task_delete(source_task);
        free_binary(&wasm_binary);
        return fail_test("snapshot waiting restore snapshot save failed", code_base + 5);
    }

    status = os_task_delete(source_task);
    source_task = 0;
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s delete source status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free(snapshot_buffer);
        free_binary(&wasm_binary);
        return fail_test("snapshot waiting restore source delete failed", code_base + 6);
    }

    no_task_status = verify_no_tasks(code_base + 7);
    if (no_task_status != 0)
    {
        free(snapshot_buffer);
        free_binary(&wasm_binary);
        return no_task_status;
    }

    os_tick(10U);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s advanced host time tick=%u", test_name, os_get_tick_ms());
    log_info(message);

    status = os_task_create(&restore_task, wasm_binary.bytes, wasm_binary.size, "app_main", "snapshot_wait_restore", TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s create restore status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK || restore_task == 0)
    {
        free(snapshot_buffer);
        free_binary(&wasm_binary);
        return fail_test("snapshot waiting restore restore task create failed", code_base + 11);
    }

    status = os_task_load_snapshot(restore_task, snapshot_buffer, written_size);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s load status=%s restore_state=%s", test_name, os_status_name(status), os_task_state_name(os_task_get_state(restore_task)));
    log_info(message);
    if (status != OS_STATUS_OK || os_task_get_state(restore_task) != OS_TASK_READY)
    {
        (void)os_task_delete(restore_task);
        free(snapshot_buffer);
        free_binary(&wasm_binary);
        return fail_test("snapshot waiting restore snapshot load failed", code_base + 12);
    }

    status = os_schedule();
    log_exit_metadata_task_snapshot("snapshot waiting restore after resume", restore_task);
    if (status != OS_STATUS_OK || os_task_get_state(restore_task) != OS_TASK_DEAD || os_task_get_exit_reason(restore_task) != OS_TASK_EXIT_RETURNED || os_task_get_exit_code(restore_task) != 21U)
    {
        (void)os_task_delete(restore_task);
        free(snapshot_buffer);
        free_binary(&wasm_binary);
        return fail_test("snapshot waiting restore restored task did not return expected result", code_base + 13);
    }

    status = os_task_delete(restore_task);
    restore_task = 0;
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s delete restore status=%s", test_name, os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        free(snapshot_buffer);
        free_binary(&wasm_binary);
        return fail_test("snapshot waiting restore restore delete failed", code_base + 14);
    }

    free(snapshot_buffer);
    snapshot_buffer = 0;
    free_binary(&wasm_binary);

    no_task_status = verify_no_tasks(code_base + 15);
    if (no_task_status != 0)
    {
        return no_task_status;
    }

    log_pass("task snapshot waiting restore final PASS");
    return 0;
}

static int run_bad_import_wasm_diagnostics_test(const char* wasm_path, const char* entry_function_name, int code_base)
{
    const char* test_name = "bad_import.wasm";
    TestBinary wasm_binary;
    OsTaskHandle task = 0;
    OsStatus status = OS_STATUS_OK;
    const char* phase = 0;
    const char* result = 0;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM bad import diagnostics test start name=%s", test_name);
    log_phase(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file path: %s", wasm_path);
    log_info(message);

    if (!read_binary_file(wasm_path, &wasm_binary))
    {
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file load failure path=%s", wasm_path);
        log_fail(message);
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "%s missing or unreadable WASM file: %s", test_name, wasm_path);
        return fail_test(message, TEST_FAILURE_MISSING_WASM);
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file load success path=%s", wasm_path);
    log_pass(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "WASM file byte size=%u", wasm_binary.size);
    log_info(message);

    status = os_task_create(&task, wasm_binary.bytes, wasm_binary.size, entry_function_name, test_name, TEST_WASM_STACK_SIZE, TEST_WASM_PRIORITY);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "os_task_create status=%s", os_status_name(status));
    log_info(message);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "task handle result=%s", task == 0 ? "null" : "non-null");
    log_info(message);
    log_last_os_error(test_name);

    phase = os_get_last_error_phase();
    result = os_get_last_error_result();

    if (status != OS_STATUS_WASM_ERROR)
    {
        free_binary(&wasm_binary);
        return fail_test("bad_import.wasm expected OS_STATUS_WASM_ERROR", code_base);
    }

    if (task != 0)
    {
        free_binary(&wasm_binary);
        return fail_test("bad_import.wasm expected null task handle", code_base + 1);
    }

    if (os_get_last_error_status() != OS_STATUS_WASM_ERROR)
    {
        free_binary(&wasm_binary);
        return fail_test("bad_import.wasm expected diagnostic status", code_base + 2);
    }

    if (phase == 0 || strcmp(phase, "none") == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("bad_import.wasm expected diagnostic phase", code_base + 3);
    }

    if (result == 0)
    {
        free_binary(&wasm_binary);
        return fail_test("bad_import.wasm expected diagnostic result", code_base + 4);
    }

    if (os_get_task_count() != 0U || os_get_ready_task_count() != 0U || os_get_waiting_task_count() != 0U || os_task_get_current() != 0)
    {
        free_binary(&wasm_binary);
        return fail_test("bad_import.wasm expected empty task counters", code_base + 5);
    }

    log_task_counters("bad_import.wasm final counters");
    log_pass("WASM bad import diagnostics test pass name=bad_import.wasm");
    free_binary(&wasm_binary);
    return 0;
}

int main(void)
{
    OsStatus status = OS_STATUS_OK;
    int no_task_status = 0;
    int wasm_test_status = 0;
    int failure_count = 0;
    int first_failure_code = 0;
    char message[TEST_MESSAGE_BUFFER_SIZE];

    if (!log_open())
    {
        return 90;
    }

    log_phase("smoke test start");

    hal_init();
    log_info("hal_init called");

    status = os_init();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "os_init result=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_OK)
    {
        record_test_status("os_init", fail_test("os_init failed", TEST_FAILURE_INIT), &failure_count, &first_failure_code);
    }

    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "current tick count=%u", os_get_tick_ms());
    log_info(message);
    if (os_get_tick_ms() != 0U)
    {
        record_test_status("initial tick", fail_test("expected initial tick count to be zero", TEST_FAILURE_TICK_ZERO), &failure_count, &first_failure_code);
    }

    no_task_status = verify_no_tasks(TEST_FAILURE_EMPTY_OS_INITIAL);
    record_test_status("host/no-task check", no_task_status, &failure_count, &first_failure_code);

    no_task_status = run_hal_native_time_test(TEST_FAILURE_HAL_NATIVE_TIME);
    record_test_status("host/no-task check", no_task_status, &failure_count, &first_failure_code);

    os_tick(1U);
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "current tick count=%u", os_get_tick_ms());
    log_info(message);
    if (os_get_tick_ms() != 1U)
    {
        record_test_status("tick advance", fail_test("expected tick count to advance to one", TEST_FAILURE_TICK_ADVANCE), &failure_count, &first_failure_code);
    }

    status = os_schedule();
    format_message(message, TEST_MESSAGE_BUFFER_SIZE, "empty OS scheduler status=%s", os_status_name(status));
    log_info(message);
    if (status != OS_STATUS_NO_READY_TASKS)
    {
        record_test_status("empty schedule", fail_test("expected no ready tasks before creating WASM tasks", TEST_FAILURE_EMPTY_SCHEDULE), &failure_count, &first_failure_code);
    }

    no_task_status = verify_no_tasks(TEST_FAILURE_EMPTY_OS_AFTER_SCHEDULE);
    record_test_status("host/no-task check", no_task_status, &failure_count, &first_failure_code);

    wasm_test_status = run_task_id_host_api_test(15);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_task_find_by_id_host_api_test(18);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_task_id_list_host_api_test(19);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_finite_wasm_app_test("empty_start.wasm", "build/empty_start.wasm", "app_main", 20);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_finite_wasm_app_test("simple_loop.wasm", "build/simple_loop.wasm", "app_main", 40);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_yield_once_wasm_app_test("build/yield_once.wasm", "app_main", 60);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_delay_once_wasm_app_test("build/delay_once.wasm", "app_main", 80);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_wasi_exit_test(100);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_wasm_exit_metadata_test(105);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_wasm_entry_return_code_test(108);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_queue_api_wasm_test(760);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_wasm_entry_return_values_test(700);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_task_entry_mixed_scalar_args_test(560);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_task_entry_arg_validation_negative_test(580);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_wasm_get_time_host_import_test(110);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_custom_host_import_registration_test(112);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_task_snapshot_host_api_test(115);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_task_snapshot_after_source_delete_test(117);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_task_snapshot_waiting_restore_test(520);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_bad_import_wasm_diagnostics_test("build/bad_import.wasm", "app_main", 120);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_multitask_delay_fairness_test(130);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_absolute_time_update_test(150);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_tick_source_reanchor_test(160);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_preempt_request_api_test(180);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_long_running_fuel_slice_test(200);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_two_long_running_task_fairness_test(220);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_priority_scheduler_policy_test(240);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_wake_time_scheduler_policy_test(260);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_priority_change_scheduler_policy_test(300);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_waiting_priority_change_scheduler_policy_test(320);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_suspend_resume_scheduler_policy_test(360);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_task_delete_scheduler_pointer_policy_test(400);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_tick_wraparound_delay_wake_policy_test(480);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    wasm_test_status = run_task_deletion_state_coverage_test(500);
    record_test_status("wasm test", wasm_test_status, &failure_count, &first_failure_code);

    log_task_counters("final");
    if (failure_count != 0)
    {
        format_message(message, TEST_MESSAGE_BUFFER_SIZE, "smoke test final FAIL failure_count=%d first_failure_code=%d", failure_count, first_failure_code);
        log_fail(message);
        printf("Smoke test FAIL. %d failure(s). Log written to %s\n", failure_count, TEST_LOG_PATH);
        shutdown_harness();
        return first_failure_code;
    }

    log_pass("smoke test final PASS");
    printf("Smoke test PASS. Log written to %s\n", TEST_LOG_PATH);

    shutdown_harness();
    return 0;
}
