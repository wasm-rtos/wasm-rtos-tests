#include "os.h"
#include "hal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct WasmBinary
{
    uint8_t* bytes;
    uint32_t size;
} WasmBinary;

typedef struct FakeClock
{
    uint32_t now_ms;
    uint32_t arm_count;
    uint32_t cancel_count;
    uint32_t last_delay_ms;
    uint8_t armed;
} FakeClock;

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
    FILE* file = fopen("software_timer.wasm", "rb");
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
        expect(os_task_delete(*task) == OS_STATUS_OK, "delete timer task");
        *task = NULL;
    }
}

static void reset_os(void)
{
    os_shutdown();
    expect(os_init() == OS_STATUS_OK, "reset OS for timer scenario");
}

static uint32_t fake_now_ms(void* context)
{
    return ((FakeClock*)context)->now_ms;
}

static void fake_arm_wakeup(void* context, uint32_t delay_ms)
{
    FakeClock* clock = (FakeClock*)context;

    ++clock->arm_count;
    clock->last_delay_ms = delay_ms;
    clock->armed = 1U;
}

static void fake_cancel_wakeup(void* context)
{
    FakeClock* clock = (FakeClock*)context;

    ++clock->cancel_count;
    clock->armed = 0U;
}

static void test_wasm_timer_imports(WasmBinary* wasm)
{
    OsTaskHandle task = NULL;

    reset_os();
    expect(create_task(
               &task,
               wasm,
               "app_main_wasm_timer",
               "wasm_timer",
               0U,
               OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create WASM timer API task");
    expect(os_schedule() == OS_STATUS_OK,
           "WASM creates and starts one-shot timer");
    expect(os_task_get_state(task) == OS_TASK_WAITING,
           "WASM timer task waits for notification");
    expect(os_get_timer_count() == 1U && os_get_next_wakeup_ms() == 3U,
           "WASM change-period import sets nearest deadline");

    os_tick(2U);
    expect(os_task_get_state(task) == OS_TASK_WAITING,
           "WASM timer does not fire early");
    os_tick(1U);
    expect(os_task_get_state(task) == OS_TASK_READY,
           "WASM timer fires at exact deadline");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_DEAD &&
               os_task_get_exit_code(task) == 0x5aU,
           "WASM timer imports return notification value");
    expect(os_get_timer_count() == 0U,
           "WASM deletes completed timer");
    delete_task(&task);
}

static void test_exact_deadline_and_timeout_tie(WasmBinary* wasm)
{
    OsTaskHandle task = NULL;
    OsTimerHandle timer = NULL;
    uint32_t timer_id = 0U;

    reset_os();
    expect(create_task(
               &task,
               wasm,
               "app_main_wait_timeout",
               "timer_timeout_tie",
               5U,
               OS_TASK_PRIORITY_HIGH) == OS_STATUS_OK,
           "create timer timeout-tie task");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_WAITING,
           "notification task waits with timeout");
    expect(os_get_next_wakeup_ms() == 5U,
           "task timeout contributes nearest wakeup");
    expect(os_timer_create(
               &timer,
               5U,
               0U,
               task,
               0x77U,
               OS_NOTIFY_SET_VALUE_WITH_OVERWRITE) == OS_STATUS_OK,
           "create host one-shot timer");
    timer_id = os_timer_get_id(timer);
    expect(timer_id != 0U && os_timer_find_by_id(timer_id) == timer,
           "timer ID resolves to handle");
    expect(!os_timer_is_active(timer) && os_timer_get_period_ms(timer) == 5U,
           "new timer is inactive with requested period");
    expect(os_timer_start(timer) == OS_STATUS_OK,
           "start host one-shot timer");

    os_tick(4U);
    expect(os_task_get_state(task) == OS_TASK_WAITING &&
               os_get_next_wakeup_ms() == 1U,
           "one-shot timer waits until exact deadline");
    os_tick(1U);
    expect(os_task_get_state(task) == OS_TASK_READY &&
               os_task_get_last_wait_status(task) == OS_STATUS_OK,
           "timer notification wins an equal timeout deadline");
    expect(!os_timer_is_active(timer),
           "one-shot timer becomes inactive before notification resumes");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_exit_code(task) == 0x77U,
           "one-shot notification reaches WASM task");
    expect(os_get_timer_count() == 0U &&
               os_timer_find_by_id(timer_id) == NULL,
           "dead notification target releases its timers");
    delete_task(&task);
}

static void test_timer_controls(WasmBinary* wasm)
{
    OsTaskHandle task = NULL;
    OsTimerHandle timer = NULL;

    reset_os();
    expect(create_task(
               &task,
               wasm,
               "app_main_wait_forever",
               "timer_controls",
               0U,
               OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create timer-control waiter");
    expect(os_schedule() == OS_STATUS_OK,
           "timer-control waiter blocks");
    expect(os_timer_create(
               &timer,
               5U,
               0U,
               task,
               0x33U,
               OS_NOTIFY_SET_VALUE_WITH_OVERWRITE) == OS_STATUS_OK,
           "create controllable timer");
    expect(os_timer_start(timer) == OS_STATUS_OK,
           "start controllable timer");
    os_tick(2U);
    expect(os_timer_reset(timer) == OS_STATUS_OK &&
               os_get_next_wakeup_ms() == 5U,
           "reset restarts timer from current time");
    os_tick(4U);
    expect(os_task_get_state(task) == OS_TASK_WAITING,
           "reset timer remains pending before new deadline");
    expect(os_timer_change_period(timer, 3U) == OS_STATUS_OK &&
               os_timer_get_period_ms(timer) == 3U &&
               os_get_next_wakeup_ms() == 3U,
           "change period restarts active timer");
    os_tick(2U);
    expect(os_timer_stop(timer) == OS_STATUS_OK &&
               !os_timer_is_active(timer) &&
               os_get_next_wakeup_ms() == OS_WAIT_FOREVER,
           "stop removes timer from nearest wakeup");
    os_tick(100U);
    expect(os_task_get_state(task) == OS_TASK_WAITING,
           "stopped timer does not notify task");
    expect(os_timer_start(timer) == OS_STATUS_OK,
           "restart stopped timer");
    os_tick(3U);
    expect(os_task_get_state(task) == OS_TASK_READY,
           "restarted timer uses changed period");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_exit_code(task) == 0x33U,
           "restarted timer delivers notification");
    delete_task(&task);
}

static void test_auto_reload_coalescing(WasmBinary* wasm)
{
    OsTaskHandle task = NULL;
    OsTimerHandle timer = NULL;

    reset_os();
    expect(create_task(
               &task,
               wasm,
               "app_main_take_twice",
               "auto_reload",
               0U,
               OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create auto-reload notification task");
    expect(os_schedule() == OS_STATUS_OK,
           "auto-reload task waits for first period");
    expect(os_timer_create(
               &timer,
               4U,
               1U,
               task,
               0U,
               OS_NOTIFY_INCREMENT) == OS_STATUS_OK &&
               os_timer_start(timer) == OS_STATUS_OK,
           "start auto-reload increment timer");

    os_tick(10U);
    expect(os_task_get_state(task) == OS_TASK_READY,
           "late tick coalesces elapsed timer periods");
    expect(os_timer_is_active(timer) && os_get_next_wakeup_ms() == 2U,
           "auto-reload keeps original phase after late tick");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_WAITING,
           "task consumes one coalesced notification and waits again");
    os_tick(1U);
    expect(os_task_get_state(task) == OS_TASK_WAITING,
           "phase-preserved timer does not fire early");
    os_tick(1U);
    expect(os_task_get_state(task) == OS_TASK_READY,
           "phase-preserved timer fires at next boundary");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_exit_code(task) == 11U,
           "auto-reload delivers one notification per update");
    expect(os_get_timer_count() == 0U,
           "task exit removes its auto-reload timer");
    delete_task(&task);
}

static void test_target_delete_and_wraparound(WasmBinary* wasm)
{
    OsTaskHandle task = NULL;
    OsTimerHandle timer = NULL;
    uint32_t timer_id = 0U;

    reset_os();
    expect(create_task(
               &task,
               wasm,
               "app_main_wait_forever",
               "deleted_timer_target",
               0U,
               OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create deletable timer target");
    expect(os_schedule() == OS_STATUS_OK,
           "deletable timer target blocks");
    expect(os_timer_create(
               &timer,
               8U,
               1U,
               task,
               0U,
               OS_NOTIFY_INCREMENT) == OS_STATUS_OK &&
               os_timer_start(timer) == OS_STATUS_OK,
           "create timer owned by deletable target");
    timer_id = os_timer_get_id(timer);
    delete_task(&task);
    expect(os_get_timer_count() == 0U &&
               os_timer_find_by_id(timer_id) == NULL &&
               os_get_next_wakeup_ms() == OS_WAIT_FOREVER,
           "task deletion removes timers and wakeups");

    reset_os();
    os_tick(UINT32_MAX - 2U);
    expect(os_get_tick_ms() == UINT32_MAX - 2U,
           "advance logical time near uint32 wraparound");
    expect(create_task(
               &task,
               wasm,
               "app_main_wait_forever",
               "timer_wraparound",
               0U,
               OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create wraparound timer target");
    expect(os_schedule() == OS_STATUS_OK,
           "wraparound timer target blocks");
    expect(os_timer_create(
               &timer,
               5U,
               0U,
               task,
               0x66U,
               OS_NOTIFY_SET_VALUE_WITH_OVERWRITE) == OS_STATUS_OK &&
               os_timer_start(timer) == OS_STATUS_OK,
           "start timer across tick wraparound");
    os_tick(4U);
    expect(os_get_tick_ms() == 1U &&
               os_task_get_state(task) == OS_TASK_WAITING,
           "wrapped timer remains pending before deadline");
    os_tick(1U);
    expect(os_get_tick_ms() == 2U &&
               os_task_get_state(task) == OS_TASK_READY,
           "wrapped timer fires at modular deadline");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_exit_code(task) == 0x66U,
           "wrapped timer delivers notification");
    delete_task(&task);
}

static void test_tickless_clock_port(WasmBinary* wasm)
{
    FakeClock clock = {0};
    OsClockPort port;
    OsClockPort invalid_port;
    OsTaskHandle task = NULL;
    OsTimerHandle timer = NULL;

    reset_os();
    clock.now_ms = 1000U;
    invalid_port.now_ms = fake_now_ms;
    invalid_port.arm_wakeup = fake_arm_wakeup;
    invalid_port.cancel_wakeup = NULL;
    invalid_port.context = &clock;
    expect(os_clock_port_set(&invalid_port) == OS_STATUS_INVALID_ARGUMENT,
           "reject incomplete tickless clock port");

    port.now_ms = fake_now_ms;
    port.arm_wakeup = fake_arm_wakeup;
    port.cancel_wakeup = fake_cancel_wakeup;
    port.context = &clock;
    expect(os_clock_port_set(&port) == OS_STATUS_OK &&
               os_clock_is_tickless() && os_get_tick_ms() == 0U,
           "register tickless clock and establish baseline");
    expect(create_task(
               &task,
               wasm,
               "app_main_wait_forever",
               "tickless_target",
               0U,
               OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create tickless timer target");
    expect(os_schedule() == OS_STATUS_OK,
           "tickless target blocks");
    expect(os_timer_create(
               &timer,
               5U,
               0U,
               task,
               0x88U,
               OS_NOTIFY_SET_VALUE_WITH_OVERWRITE) == OS_STATUS_OK &&
               os_timer_start(timer) == OS_STATUS_OK,
           "start timer through tickless port");
    expect(clock.armed && clock.arm_count == 1U &&
               clock.last_delay_ms == 5U,
           "hardware alarm receives nearest deadline");
    expect(os_timer_stop(timer) == OS_STATUS_OK &&
               !clock.armed && clock.cancel_count == 1U,
           "stopping nearest timer cancels hardware alarm");
    expect(os_timer_start(timer) == OS_STATUS_OK &&
               clock.armed && clock.arm_count == 2U &&
               clock.last_delay_ms == 5U,
           "restarting timer rearms hardware alarm");

    clock.now_ms = 1002U;
    expect(os_schedule() == OS_STATUS_NO_READY_TASKS &&
               os_get_tick_ms() == 2U && clock.arm_count == 2U,
           "scheduler polls absolute clock without periodic os_tick");

    clock.armed = 0U;
    clock.now_ms = 1003U;
    expect(os_clock_wakeup() == OS_STATUS_OK &&
               clock.armed && clock.arm_count == 3U &&
               clock.last_delay_ms == 2U,
           "early hardware wake rearms remaining deadline");

    clock.armed = 0U;
    clock.now_ms = 1005U;
    expect(os_clock_wakeup() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_READY,
           "hardware wake advances absolute time and expires timer");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_exit_code(task) == 0x88U,
           "tickless timer resumes target task");
    expect(!clock.armed && os_get_timer_count() == 0U,
           "tickless port has no stale alarm after completion");
    delete_task(&task);
    os_clock_port_clear();
    expect(!os_clock_is_tickless(), "clear tickless clock port");
}

static void test_polled_clock_fallback(WasmBinary* wasm)
{
    FakeClock clock = {0};
    OsClockPort port;
    OsTaskHandle task = NULL;
    OsTimerHandle timer = NULL;

    reset_os();
    clock.now_ms = 2000U;
    port.now_ms = fake_now_ms;
    port.arm_wakeup = NULL;
    port.cancel_wakeup = NULL;
    port.context = &clock;
    expect(os_clock_port_set(&port) == OS_STATUS_OK &&
               !os_clock_is_tickless(),
           "register clock-only polling fallback");
    expect(create_task(
               &task,
               wasm,
               "app_main_wait_forever",
               "polled_clock_target",
               0U,
               OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create polled-clock timer target");
    expect(os_schedule() == OS_STATUS_OK,
           "polled-clock target blocks");
    expect(os_timer_create(
               &timer,
               4U,
               0U,
               task,
               0x99U,
               OS_NOTIFY_SET_VALUE_WITH_OVERWRITE) == OS_STATUS_OK &&
               os_timer_start(timer) == OS_STATUS_OK,
           "start timer without hardware alarm");

    clock.now_ms = 2003U;
    expect(os_schedule() == OS_STATUS_NO_READY_TASKS &&
               os_get_tick_ms() == 3U,
           "polling fallback tracks absolute time before deadline");
    clock.now_ms = 2004U;
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(task) == OS_TASK_DEAD &&
               os_task_get_exit_code(task) == 0x99U,
           "polling fallback fires timer without os_tick call");
    delete_task(&task);
    os_clock_port_clear();
}

int main(void)
{
    WasmBinary wasm = {0};

    g_log = fopen("software_timer.log", "w");
    if (g_log == NULL)
    {
        return 1;
    }

    hal_init();
    expect(os_init() == OS_STATUS_OK, "initialize OS");
    expect(load_wasm(&wasm), "load software timer test module");

    if (wasm.bytes != NULL)
    {
        test_wasm_timer_imports(&wasm);
        test_exact_deadline_and_timeout_tie(&wasm);
        test_timer_controls(&wasm);
        test_auto_reload_coalescing(&wasm);
        test_target_delete_and_wraparound(&wasm);
        test_tickless_clock_port(&wasm);
        test_polled_clock_fallback(&wasm);
    }

    free(wasm.bytes);
    os_shutdown();
    hal_shutdown();
    fclose(g_log);
    return g_failures == 0 ? 0 : 1;
}
