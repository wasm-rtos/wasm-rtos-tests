#include "os.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct WasmBinary
{
    uint8_t* bytes;
    uint32_t size;
} WasmBinary;

static FILE* g_log;
static int g_failures;

static void expect(int condition, const char* message)
{
    fprintf(stdout, "%s %s\n", condition ? "PASS" : "FAIL", message);
    fprintf(g_log, "%s %s\n", condition ? "PASS" : "FAIL", message);
    fflush(g_log);
    if (!condition)
    {
        ++g_failures;
    }
}

static int load_wasm(WasmBinary* binary)
{
    FILE* file = fopen("task_delay_until_abort_wait.wasm", "rb");
    long size;

    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (size = ftell(file)) <= 0L ||
        (unsigned long)size > 0xFFFFFFFFUL ||
        fseek(file, 0L, SEEK_SET) != 0)
    {
        if (file != NULL)
        {
            fclose(file);
        }
        return 0;
    }

    binary->bytes = (uint8_t*)malloc((size_t)size);
    if (binary->bytes == NULL ||
        fread(binary->bytes, 1U, (size_t)size, file) != (size_t)size)
    {
        free(binary->bytes);
        binary->bytes = NULL;
        fclose(file);
        return 0;
    }

    fclose(file);
    binary->size = (uint32_t)size;
    return 1;
}

static OsStatus create_task(
    OsTaskHandle* out_task,
    WasmBinary* wasm,
    const char* entry,
    const char* name,
    uint32_t argument,
    uint32_t priority
)
{
    OsValue arg;

    arg.type = OS_VALUE_TYPE_I32;
    arg.value.i32 = argument;
    return os_task_create_with_args(
        out_task,
        wasm->bytes,
        wasm->size,
        entry,
        &arg,
        1U,
        name,
        64U * 1024U,
        priority
    );
}

static void delete_task(OsTaskHandle* task)
{
    if (*task != NULL)
    {
        expect(os_task_delete(*task) == OS_STATUS_OK, "delete task");
        *task = NULL;
    }
}

static void reset_os(void)
{
    os_shutdown();
    expect(os_init() == OS_STATUS_OK, "reset OS");
}

static void test_periodic_phase_and_missed_period(WasmBinary* wasm)
{
    OsTaskHandle task = NULL;

    reset_os();
    expect(create_task(
               &task,
               wasm,
               "app_main_periodic",
               "periodic",
               0U,
               OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create periodic task");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_WAITING &&
               os_get_next_wakeup_ms() == 10U,
           "first delay uses the absolute 10 ms phase");

    os_tick(13U);
    expect(os_task_get_state(task) == OS_TASK_READY,
           "late first deadline wakes periodic task");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_WAITING &&
               os_get_next_wakeup_ms() == 7U,
           "late execution keeps the original phase without drift");

    os_tick(7U);
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_WAITING &&
               os_get_next_wakeup_ms() == 10U,
           "on-time execution advances by one fixed period");

    os_tick(15U);
    expect(os_task_get_state(task) == OS_TASK_READY,
           "missed third period wakes at the late host time");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_WAITING &&
               os_get_next_wakeup_ms() == 5U,
           "missed period catches up to the next phase boundary");

    os_tick(5U);
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_DEAD &&
               os_task_get_exit_code(task) == 40U,
           "periodic task finishes exactly on the fourth boundary");
    delete_task(&task);
}

static void test_wraparound(WasmBinary* wasm)
{
    OsTaskHandle task = NULL;

    reset_os();
    os_tick(UINT32_MAX - 5U);
    expect(os_get_tick_ms() == UINT32_MAX - 5U,
           "move clock near uint32 wraparound");
    expect(create_task(
               &task,
               wasm,
               "app_main_periodic",
               "periodic_wrap",
               0U,
               OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create wraparound periodic task");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_WAITING &&
               os_get_next_wakeup_ms() == 10U,
           "delay-until deadline crosses tick wraparound");
    os_tick(9U);
    expect(os_task_get_state(task) == OS_TASK_WAITING &&
               os_get_next_wakeup_ms() == 1U,
           "wrapped deadline does not fire early");
    os_tick(1U);
    expect(os_task_get_state(task) == OS_TASK_READY &&
               os_get_tick_ms() == 4U,
           "wrapped deadline fires at the exact tick");
    delete_task(&task);
}

static void test_lateness_above_int32(WasmBinary* wasm)
{
    OsTaskHandle task = NULL;
    const uint32_t late_tick_ms = (uint32_t)INT32_MAX + 100U;

    reset_os();
    expect(create_task(
               &task,
               wasm,
               "app_main_periodic",
               "periodic_large_lateness",
               0U,
               OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create periodic task for large lateness");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_WAITING,
           "large-lateness periodic task starts first period");
    os_tick(late_tick_ms);
    expect(os_task_get_state(task) == OS_TASK_READY,
           "large elapsed delta readies periodic task");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_DEAD &&
               os_task_get_exit_code(task) == late_tick_ms,
           "lateness above INT32_MAX cannot strand periodic catch-up");
    delete_task(&task);
}

static void test_abort_delay_until(WasmBinary* wasm)
{
    OsTaskHandle task = NULL;
    OsTaskHandle ready_task = NULL;

    reset_os();
    expect(create_task(
               &ready_task,
               wasm,
               "app_main_abort_periodic",
               "not_waiting",
               0U,
               OS_TASK_PRIORITY_LOW) == OS_STATUS_OK,
           "create ready task for negative abort test");
    expect(os_task_abort_wait(ready_task) == OS_STATUS_NOT_WAITING,
           "abort rejects a task that is not waiting");
    delete_task(&ready_task);

    expect(create_task(
               &task,
               wasm,
               "app_main_abort_periodic",
               "abort_periodic",
               0U,
               OS_TASK_PRIORITY_HIGH) == OS_STATUS_OK,
           "create delay-until abort task");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_WAITING &&
               os_get_next_wakeup_ms() == 100U,
           "delay-until task blocks on absolute deadline");
    expect(os_task_abort_wait(task) == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_READY &&
               os_task_get_last_wait_status(task) == OS_STATUS_ABORTED &&
               os_get_next_wakeup_ms() == OS_WAIT_FOREVER,
           "abort removes delay deadline and readies task");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_DEAD &&
               os_task_get_exit_code(task) == OS_STATUS_ABORTED,
           "blocked delay-until import resumes with ABORTED");
    expect(os_task_abort_wait(task) == OS_STATUS_TASK_DEAD,
           "abort rejects a dead task");
    delete_task(&task);
}

static void test_abort_delay_from_wasm(WasmBinary* wasm)
{
    OsTaskHandle delayed = NULL;
    OsTaskHandle controller = NULL;
    uint32_t delayed_id = 0U;

    reset_os();
    expect(create_task(
               &delayed,
               wasm,
               "app_main_abort_delay",
               "legacy_delay",
               0U,
               OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create legacy delay task");
    delayed_id = os_task_get_id(delayed);
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(delayed) == OS_TASK_WAITING,
           "legacy delay task blocks");
    expect(create_task(
               &controller,
               wasm,
               "app_main_abort_controller",
               "abort_controller",
               delayed_id,
               OS_TASK_PRIORITY_HIGH) == OS_STATUS_OK,
           "create WASM abort controller");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(controller) == OS_TASK_DEAD &&
               os_task_get_exit_code(controller) == OS_STATUS_OK &&
               os_task_get_state(delayed) == OS_TASK_READY,
           "common os_task_abort_wait import wakes target");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(delayed) == OS_TASK_DEAD &&
               os_task_get_exit_code(delayed) == 0x11U &&
               os_task_get_last_wait_status(delayed) == OS_STATUS_ABORTED,
           "void legacy delay resumes after abort");
    delete_task(&controller);
    delete_task(&delayed);
}

static void test_abort_synchronization_waits(WasmBinary* wasm)
{
    OsTaskHandle semaphore_task = NULL;
    OsTaskHandle owner = NULL;
    OsTaskHandle waiter = NULL;
    OsMutexHandle mutex = NULL;
    uint32_t mutex_id = 0U;

    reset_os();
    expect(create_task(
               &semaphore_task,
               wasm,
               "app_main_abort_semaphore",
               "abort_semaphore",
               0U,
               OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create semaphore waiter");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(semaphore_task) == OS_TASK_WAITING,
           "semaphore task waits forever");
    expect(os_task_abort_wait(semaphore_task) == OS_STATUS_OK &&
               os_task_get_state(semaphore_task) == OS_TASK_READY,
           "abort wakes synchronization waiter");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_exit_code(semaphore_task) == OS_STATUS_ABORTED,
           "semaphore take resumes with ABORTED");
    delete_task(&semaphore_task);

    reset_os();
    expect(os_mutex_create(&mutex) == OS_STATUS_OK,
           "create mutex for inheritance abort");
    mutex_id = os_mutex_get_id(mutex);
    expect(create_task(
               &owner,
               wasm,
               "app_main_mutex_owner",
               "mutex_owner",
               mutex_id,
               OS_TASK_PRIORITY_LOW) == OS_STATUS_OK,
           "create low-priority mutex owner");
    expect(os_schedule() == OS_STATUS_OK &&
               os_mutex_get_owner(mutex) == owner,
           "owner locks mutex and delays");
    expect(create_task(
               &waiter,
               wasm,
               "app_main_mutex_waiter",
               "mutex_waiter",
               mutex_id,
               OS_TASK_PRIORITY_HIGH) == OS_STATUS_OK,
           "create high-priority mutex waiter");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(waiter) == OS_TASK_WAITING &&
               os_task_get_priority(owner) == OS_TASK_PRIORITY_HIGH,
           "blocked waiter raises owner priority");
    expect(os_task_abort_wait(waiter) == OS_STATUS_OK &&
               os_task_get_priority(owner) == OS_TASK_PRIORITY_LOW,
           "aborting mutex wait removes inherited priority");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_exit_code(waiter) == OS_STATUS_ABORTED,
           "mutex lock resumes with ABORTED");
    delete_task(&waiter);
    delete_task(&owner);
    os_mutex_delete(mutex);
}

static void test_invalid_period(WasmBinary* wasm)
{
    OsTaskHandle task = NULL;

    reset_os();
    expect(create_task(
               &task,
               wasm,
               "app_main_invalid_period",
               "invalid_period",
               0U,
               OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create invalid-period task");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_DEAD &&
               os_task_get_exit_code(task) == OS_STATUS_INVALID_ARGUMENT,
           "zero delay-until period is rejected");
    delete_task(&task);
}

int main(void)
{
    WasmBinary wasm = { NULL, 0U };

    g_log = fopen("task_delay_until_abort_wait.log", "w");
    if (g_log == NULL)
    {
        return 1;
    }

    expect(load_wasm(&wasm), "load task delay/abort WASM");
    if (wasm.bytes != NULL)
    {
        expect(os_init() == OS_STATUS_OK, "initialize OS");
        test_periodic_phase_and_missed_period(&wasm);
        test_wraparound(&wasm);
        test_lateness_above_int32(&wasm);
        test_abort_delay_until(&wasm);
        test_abort_delay_from_wasm(&wasm);
        test_abort_synchronization_waits(&wasm);
        test_invalid_period(&wasm);
        os_shutdown();
    }

    free(wasm.bytes);
    fclose(g_log);
    return g_failures == 0 ? 0 : 1;
}
