#include "os.h"
#include "hal.h"
#include "wasm3/source/m3_env.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct WasmBinary
{
    uint8_t* bytes;
    uint32_t size;
} WasmBinary;

typedef enum HostTestMode
{
    HOST_TEST_VALUE = 0,
    HOST_TEST_NATIVE_WAIT,
    HOST_TEST_STALE_CLOCK_DELAY
} HostTestMode;

typedef struct FakeClock
{
    uint32_t now_ms;
} FakeClock;

static FILE* g_log = NULL;
static int g_failures = 0;
static OsQueueHandle g_queue = NULL;
static OsStatus g_native_wait_status = OS_STATUS_OK;
static OsTaskState g_native_wait_task_state = OS_TASK_DEAD;
static OsStatus g_delay_status = OS_STATUS_OK;
static HostTestMode g_test_mode = HOST_TEST_VALUE;
static FakeClock g_clock = {0};

static void log_message(const char* format, ...)
{
    va_list args;

    va_start(args, format);
    vprintf(format, args);
    printf("\n");
    va_end(args);

    if (g_log != NULL)
    {
        va_start(args, format);
        vfprintf(g_log, format, args);
        fprintf(g_log, "\n");
        fflush(g_log);
        va_end(args);
    }
}

static void expect(int condition, const char* message)
{
    log_message("%s %s", condition ? "PASS" : "FAIL", message);
    if (!condition)
    {
        ++g_failures;
    }
}

static int load_wasm(WasmBinary* binary)
{
    FILE* file = fopen("custom_import.wasm", "rb");
    long size;

    binary->bytes = NULL;
    binary->size = 0U;

    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 || (size = ftell(file)) <= 0L ||
        (unsigned long)size > 0xFFFFFFFFUL || fseek(file, 0L, SEEK_SET) != 0)
    {
        if (file != NULL)
            fclose(file);
        expect(0, "open custom_import.wasm");
        return 0;
    }

    binary->bytes = (uint8_t*)malloc((size_t)size);
    if (binary->bytes == NULL || fread(binary->bytes, 1U, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        free(binary->bytes);
        binary->bytes = NULL;
        expect(0, "read custom_import.wasm");
        return 0;
    }

    fclose(file);
    binary->size = (uint32_t)size;
    return 1;
}

static int schedule_until_dead(OsTaskHandle task)
{
    uint32_t iteration;

    for (iteration = 0U; iteration < 10U; ++iteration)
    {
        if (os_schedule() != OS_STATUS_OK)
        {
            return 0;
        }
        if (os_task_get_state(task) == OS_TASK_DEAD)
        {
            return 1;
        }
    }

    return 0;
}

static uint32_t fake_now_ms(void* context)
{
    return ((FakeClock*)context)->now_ms;
}

static m3ApiRawFunction(host_test_value)
{
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, input);
    uint32_t local_output = 0U;
    (void)runtime;
    (void)_ctx;
    (void)_mem;

    if (g_test_mode == HOST_TEST_NATIVE_WAIT)
    {
        g_native_wait_status = os_queue_receive_wait(
            g_queue,
            &local_output,
            OS_WAIT_FOREVER
        );
        g_native_wait_task_state = os_task_get_state(os_task_get_current());
        m3ApiReturn(
            g_native_wait_status == OS_STATUS_UNSUPPORTED &&
            g_native_wait_task_state == OS_TASK_RUNNING
                ? input + 42U
                : 0U
        );
    }

    if (g_test_mode == HOST_TEST_STALE_CLOCK_DELAY)
    {
        g_clock.now_ms = 1100U;
        g_delay_status = os_task_delay_ms(5U);
        m3ApiReturn(
            g_delay_status == OS_STATUS_OK
                ? input + 42U
                : 0U
        );
    }

    m3ApiReturn(input + 42U);
}

int main(void)
{
    WasmBinary wasm;
    OsClockPort clock_port;
    OsTaskHandle task = NULL;
    OsStatus status;

    g_log = fopen("custom_import.log", "w");
    if (g_log == NULL)
    {
        return 1;
    }

    hal_init();
    expect(os_init() == OS_STATUS_OK, "initialize OS");

    status = os_host_import_register("env", "host_test_value", "i(i)", host_test_value);
    expect(status == OS_STATUS_OK, "register env.host_test_value");

    if (load_wasm(&wasm))
    {
        status = os_task_create(&task, wasm.bytes, wasm.size, "app_main", "custom_import", 64U * 1024U,
                                OS_TASK_PRIORITY_NORMAL);
        expect(status == OS_STATUS_OK && task != NULL, "create task with custom import");

        if (task != NULL)
        {
            expect(schedule_until_dead(task), "task reaches DEAD");
            expect(os_task_get_exit_reason(task) == OS_TASK_EXIT_RETURNED, "task returned normally");
            expect(os_task_get_exit_code(task) == 0U, "guest observed 37 + 42 = 79");
            expect(os_task_delete(task) == OS_STATUS_OK, "delete task");
            task = NULL;
        }

        expect(os_queue_create(&g_queue, sizeof(uint32_t), 1U) == OS_STATUS_OK,
               "create empty queue for native wait safety");
        g_test_mode = HOST_TEST_NATIVE_WAIT;
        status = os_task_create(
            &task,
            wasm.bytes,
            wasm.size,
            "app_main",
            "custom_import_native_wait",
            64U * 1024U,
            OS_TASK_PRIORITY_NORMAL
        );
        expect(status == OS_STATUS_OK && task != NULL,
               "create task for native wait safety");

        if (task != NULL)
        {
            expect(schedule_until_dead(task),
                   "native-buffer receive does not suspend custom import");
            expect(g_native_wait_status == OS_STATUS_UNSUPPORTED,
                   "blocking receive rejects transient native buffer");
            expect(g_native_wait_task_state == OS_TASK_RUNNING,
                   "rejected native wait leaves task running");
            expect(os_task_get_exit_code(task) == 0U,
                   "guest observes safe native-buffer rejection");
            expect(os_task_delete(task) == OS_STATUS_OK,
                   "delete native wait safety task");
            task = NULL;
        }

        os_queue_delete(g_queue);
        g_queue = NULL;

        g_test_mode = HOST_TEST_STALE_CLOCK_DELAY;
        g_clock.now_ms = 1000U;
        clock_port.now_ms = fake_now_ms;
        clock_port.arm_wakeup = NULL;
        clock_port.cancel_wakeup = NULL;
        clock_port.context = &g_clock;
        expect(os_clock_port_set(&clock_port) == OS_STATUS_OK,
               "register clock for delayed custom import");
        status = os_task_create(
            &task,
            wasm.bytes,
            wasm.size,
            "app_main",
            "custom_import_clock_delay",
            64U * 1024U,
            OS_TASK_PRIORITY_NORMAL
        );
        expect(status == OS_STATUS_OK && task != NULL,
               "create task for delayed custom import");

        if (task != NULL)
        {
            expect(os_schedule() == OS_STATUS_OK &&
                       g_delay_status == OS_STATUS_OK &&
                       os_task_get_state(task) == OS_TASK_WAITING,
                   "custom import delays after clock advances");
            expect(os_get_tick_ms() == 100U &&
                       os_get_next_wakeup_ms() == 5U,
                   "delay synchronizes clock before deadline");
            expect(os_clock_poll() == OS_STATUS_OK &&
                       os_task_get_state(task) == OS_TASK_WAITING,
                   "immediate clock poll does not expire synchronized delay");
            g_clock.now_ms = 1105U;
            expect(os_clock_poll() == OS_STATUS_OK &&
                       os_task_get_state(task) == OS_TASK_READY,
                   "synchronized delay wakes at exact deadline");
            expect(schedule_until_dead(task) &&
                       os_task_get_exit_code(task) == 0U,
                   "task resumes after synchronized custom-import delay");
            expect(os_task_delete(task) == OS_STATUS_OK,
                   "delete delayed custom-import task");
            task = NULL;
        }
        os_clock_port_clear();

        free(wasm.bytes);
    }

    os_host_import_clear_all();
    expect(os_get_task_count() == 0U, "OS has no remaining tasks");

    log_message("%s custom_import", g_failures == 0 ? "PASS" : "FAIL");
    os_shutdown();
    hal_shutdown();
    fclose(g_log);

    return g_failures == 0 ? 0 : 1;
}
