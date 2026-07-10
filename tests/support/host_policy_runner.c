#include "test_support.h"
#include "hal.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef TEST_NAME
#error TEST_NAME is required
#endif
#ifndef HOST_TEST_KIND
#error HOST_TEST_KIND is required
#endif

#define HOST_TASK_ID 1
#define HOST_TASK_ID_LIST 2
#define HOST_TASK_FIND 3
#define HOST_QUEUE 4
#define HOST_PRIORITY 5
#define HOST_ROUND_ROBIN 6
#define HOST_SUSPEND_RESUME 7
#define HOST_DELETION 8
#define HOST_WAKE_ORDER 9
#define HOST_TICK_WRAP 10
#define HOST_SNAPSHOT_NEGATIVE 11
#define HOST_EXIT_METADATA 12
#define HOST_INVALID_ARGS 13
#define HOST_EMPTY_SCHEDULER 14
#define HOST_HAL_TIME 15

#define TEST_WASM TEST_NAME ".wasm"
#define TEST_LOG TEST_NAME ".log"
#define STACK_SIZE (64U * 1024U)

static int create_task(TestContext* test, TestBinary* binary, OsTaskHandle* task, const char* name, uint32_t priority)
{
    OsStatus status = os_task_create(task, binary->bytes, binary->size, "app_main", name, STACK_SIZE, priority);
    return test_expect(test, status == OS_STATUS_OK && *task != NULL, "create task");
}

static void cleanup_task(OsTaskHandle task)
{
    if (task != NULL) (void)os_task_delete(task);
}

static void test_task_id(TestContext* test, TestBinary* binary)
{
    OsTaskHandle a = NULL, b = NULL;
    uint32_t a_id, b_id;
    if (!create_task(test, binary, &a, "id_a", OS_TASK_PRIORITY_NORMAL) ||
        !create_task(test, binary, &b, "id_b", OS_TASK_PRIORITY_NORMAL)) goto done;
    a_id = os_task_get_id(a); b_id = os_task_get_id(b);
    test_expect(test, os_task_get_id(NULL) == 0U, "NULL task id is zero");
    test_expect(test, a_id != 0U && b_id != 0U && a_id != b_id, "task ids are unique");
    test_expect(test, os_task_get_id(a) == a_id && os_task_get_id(b) == b_id, "task ids are stable");
done: cleanup_task(a); cleanup_task(b);
}

static void test_task_id_list(TestContext* test, TestBinary* binary)
{
    OsTaskHandle a = NULL, b = NULL;
    uint32_t ids[2] = {0U, 0U};
    uint32_t count;
    if (!create_task(test, binary, &a, "list_a", 5U) || !create_task(test, binary, &b, "list_b", 5U)) goto done;
    count = os_task_get_id_list(ids, 2U);
    test_expect(test, count == 2U, "two task ids returned");
    test_expect(test, ids[0] == os_task_get_id(a) && ids[1] == os_task_get_id(b), "task id order");
    test_expect(test, os_task_get_id_list(ids, 1U) == 1U, "task id list truncation");
    test_expect(test, os_task_get_id_list(NULL, 2U) == 0U, "NULL task id list");
done: cleanup_task(a); cleanup_task(b);
}

static void test_task_find(TestContext* test, TestBinary* binary)
{
    OsTaskHandle task = NULL;
    uint32_t id;
    if (!create_task(test, binary, &task, "find", 5U)) return;
    id = os_task_get_id(task);
    test_expect(test, os_task_find_by_id(id) == task, "find task by id");
    test_expect(test, os_task_find_by_id(0U) == NULL, "zero task id not found");
    cleanup_task(task);
    test_expect(test, os_task_find_by_id(id) == NULL, "deleted task id not found");
}

static void test_queue(TestContext* test)
{
    OsQueueHandle queue = NULL;
    uint32_t first[2] = {1U, 2U}, second[2] = {3U, 4U}, received[2] = {0U, 0U};
    uint32_t id;
    test_expect(test, os_queue_create(NULL, 8U, 2U) == OS_STATUS_INVALID_ARGUMENT, "queue create NULL output");
    test_expect(test, os_queue_create(&queue, 0U, 2U) == OS_STATUS_INVALID_ARGUMENT, "queue rejects zero item size");
    test_expect(test, os_queue_create(&queue, 8U, 0U) == OS_STATUS_INVALID_ARGUMENT, "queue rejects zero capacity");
    if (!test_expect(test, os_queue_create(&queue, 8U, 2U) == OS_STATUS_OK && queue != NULL, "queue create")) return;
    id = os_queue_get_id(queue);
    test_expect(test, id != 0U && os_queue_find_by_id(id) == queue, "queue id lookup");
    test_expect(test, os_queue_get_count(queue) == 0U && os_queue_get_space(queue) == 2U, "empty queue counters");
    test_expect(test, os_queue_send(queue, first) == OS_STATUS_OK, "send first");
    first[0] = 99U;
    test_expect(test, os_queue_send(queue, second) == OS_STATUS_OK, "send second");
    test_expect(test, os_queue_send(queue, second) == OS_STATUS_QUEUE_FULL, "exact queue full status");
    test_expect(test, os_queue_get_count(queue) == 2U && os_queue_get_space(queue) == 0U, "full queue counters");
    test_expect(test, os_queue_receive(queue, received) == OS_STATUS_OK && received[0] == 1U && received[1] == 2U, "queue copies values");
    test_expect(test, os_queue_receive(queue, received) == OS_STATUS_OK && received[0] == 3U && received[1] == 4U, "queue FIFO");
    test_expect(test, os_queue_receive(queue, received) == OS_STATUS_QUEUE_EMPTY, "exact queue empty status");
    os_queue_delete(queue);
    test_expect(test, os_queue_find_by_id(id) == NULL, "deleted queue id not found");
}

static void test_priority(TestContext* test, TestBinary* binary)
{
    OsTaskHandle low = NULL, high = NULL;
    if (!create_task(test, binary, &low, "low", OS_TASK_PRIORITY_LOW) ||
        !create_task(test, binary, &high, "high", OS_TASK_PRIORITY_HIGH)) goto done;
    test_expect(test, os_schedule() == OS_STATUS_OK, "schedule high priority");
    test_expect(test, os_task_get_run_count(high) == 1U && os_task_get_run_count(low) == 0U, "higher priority runs first");
    test_expect(test, os_task_set_priority(low, OS_TASK_PRIORITY_REALTIME) == OS_STATUS_OK, "raise priority");
    test_expect(test, os_task_get_priority(low) == OS_TASK_PRIORITY_REALTIME, "priority getter");
    test_expect(test, os_schedule() == OS_STATUS_OK, "schedule raised task");
    test_expect(test, os_task_get_run_count(low) == 1U, "raised task runs next");
done: cleanup_task(low); cleanup_task(high);
}

static void test_round_robin(TestContext* test, TestBinary* binary)
{
    OsTaskHandle a = NULL, b = NULL;
    if (!create_task(test, binary, &a, "rr_a", 5U) || !create_task(test, binary, &b, "rr_b", 5U)) goto done;
    test_expect(test, os_schedule() == OS_STATUS_OK && os_schedule() == OS_STATUS_OK, "two equal-priority slices");
    test_expect(test, os_task_get_run_count(a) == 1U && os_task_get_run_count(b) == 1U, "round-robin fairness");
done: cleanup_task(a); cleanup_task(b);
}

static void test_suspend_resume(TestContext* test, TestBinary* binary)
{
    OsTaskHandle task = NULL;
    if (!create_task(test, binary, &task, "suspend", 5U)) return;
    test_expect(test, os_task_suspend(task) == OS_STATUS_OK, "suspend task");
    test_expect(test, os_task_get_state(task) == OS_TASK_SUSPENDED, "suspended state");
    test_expect(test, os_schedule() == OS_STATUS_NO_READY_TASKS, "suspended task not scheduled");
    test_expect(test, os_task_resume(task) == OS_STATUS_OK, "resume task");
    test_expect(test, os_task_get_state(task) == OS_TASK_READY, "resumed state");
    test_expect(test, os_schedule() == OS_STATUS_OK && os_task_get_run_count(task) == 1U, "resumed task runs");
    cleanup_task(task);
}

static void test_deletion(TestContext* test, TestBinary* binary)
{
    OsTaskHandle task = NULL;
    test_expect(test, os_task_delete(NULL) == OS_STATUS_INVALID_ARGUMENT, "delete NULL task");
    if (!create_task(test, binary, &task, "delete_ready", 5U)) return;
    test_expect(test, os_task_delete(task) == OS_STATUS_OK, "delete ready task");
    test_expect(test, os_get_task_count() == 0U, "ready task removed");
}

static void test_wake_order(TestContext* test, TestBinary* binary)
{
    OsTaskHandle early = NULL, late = NULL;
    OsValue early_arg, late_arg;
    early_arg.type = OS_VALUE_TYPE_I32; early_arg.value.i32 = 5U;
    late_arg.type = OS_VALUE_TYPE_I32; late_arg.value.i32 = 10U;
    test_expect(test, os_task_create_with_args(&early, binary->bytes, binary->size, "app_main", &early_arg, 1U, "early", STACK_SIZE, 5U) == OS_STATUS_OK, "create early waiter");
    test_expect(test, os_task_create_with_args(&late, binary->bytes, binary->size, "app_main", &late_arg, 1U, "late", STACK_SIZE, 5U) == OS_STATUS_OK, "create late waiter");
    if (early == NULL || late == NULL) goto done;
    test_expect(test, os_schedule() == OS_STATUS_OK && os_schedule() == OS_STATUS_OK, "start both waiters");
    test_expect(test, os_task_get_state(early) == OS_TASK_WAITING && os_task_get_state(late) == OS_TASK_WAITING, "both waiting");
    os_tick(5U);
    test_expect(test, os_task_get_state(early) == OS_TASK_READY && os_task_get_state(late) == OS_TASK_WAITING, "earliest deadline wakes first");
    os_tick(5U);
    test_expect(test, os_task_get_state(late) == OS_TASK_READY, "later deadline wakes later");
done: cleanup_task(early); cleanup_task(late);
}

static void test_tick_wrap(TestContext* test, TestBinary* binary)
{
    OsTaskHandle task = NULL;
    os_tick(0xFFFFFFF8U);
    if (!create_task(test, binary, &task, "wrap", 5U)) return;
    test_expect(test, os_schedule() == OS_STATUS_OK && os_task_get_state(task) == OS_TASK_WAITING, "wait across tick wrap");
    os_tick(9U);
    test_expect(test, os_task_get_state(task) == OS_TASK_WAITING, "not ready before wrapped deadline");
    os_tick(1U);
    test_expect(test, os_task_get_state(task) == OS_TASK_READY, "ready at wrapped deadline");
    cleanup_task(task);
}

static void test_snapshot_negative(TestContext* test, TestBinary* binary)
{
    OsTaskHandle task = NULL;
    uint32_t size = 0U, written = 0U;
    uint8_t* buffer;
    test_expect(test, os_task_get_snapshot_size(NULL, &size) == OS_STATUS_INVALID_ARGUMENT, "snapshot size rejects NULL task");
    if (!create_task(test, binary, &task, "snapshot_negative", 5U)) return;
    test_expect(test, os_schedule() == OS_STATUS_OK, "reach resumable state");
    if (!test_expect(test, os_task_get_snapshot_size(task, &size) == OS_STATUS_OK && size > 0U, "snapshot size")) goto done;
    buffer = (uint8_t*)malloc(size);
    if (!test_expect(test, buffer != NULL, "allocate snapshot buffer")) goto done;
    test_expect(test, os_task_save_snapshot(task, NULL, size, &written) == OS_STATUS_INVALID_ARGUMENT, "snapshot save rejects NULL buffer");
    if (size > 1U) test_expect(test, os_task_save_snapshot(task, buffer, size - 1U, &written) == OS_STATUS_BUFFER_TOO_SMALL, "snapshot exact small-buffer status");
    test_expect(test, os_task_load_snapshot(task, NULL, size) == OS_STATUS_INVALID_ARGUMENT, "snapshot load rejects NULL buffer");
    free(buffer);
done: cleanup_task(task);
}

static void test_exit_metadata(TestContext* test, TestBinary* binary)
{
    OsTaskHandle task = NULL;
    if (!create_task(test, binary, &task, "exit_metadata", 5U)) return;
    test_expect(test, test_schedule_until_dead(test, task, 10U), "task exits");
    test_expect(test, os_task_get_state(task) == OS_TASK_DEAD, "dead state");
    test_expect(test, os_task_get_exit_reason(task) == OS_TASK_EXIT_RETURNED, "returned exit reason");
    test_expect(test, os_task_get_exit_code(task) == 42U, "exit code metadata");
    cleanup_task(task);
}

static void test_invalid_args(TestContext* test, TestBinary* binary)
{
    OsTaskHandle task = NULL;
    OsValue arg;
    arg.type = OS_VALUE_TYPE_I32; arg.value.i32 = 1U;
    test_expect(test, os_task_create_with_args(&task, binary->bytes, binary->size, "app_main", NULL, 1U, "bad_args", STACK_SIZE, 5U) == OS_STATUS_INVALID_ARGUMENT, "NULL entry args rejected");
    test_expect(test, task == NULL, "failed create returns NULL task");
    arg.type = (OsValueType)99;
    test_expect(test, os_task_create_with_args(&task, binary->bytes, binary->size, "app_main", &arg, 1U, "bad_type", STACK_SIZE, 5U) == OS_STATUS_INVALID_ARGUMENT, "invalid value type rejected");
}

static void test_empty_scheduler(TestContext* test)
{
    test_expect(test, os_schedule() == OS_STATUS_NO_READY_TASKS, "empty scheduler status");
    test_expect(test, os_get_task_count() == 0U && os_get_ready_task_count() == 0U && os_get_waiting_task_count() == 0U, "empty scheduler counters");
    test_expect(test, os_task_get_current() == NULL, "no current task");
}

static void test_hal_time(TestContext* test)
{
    uint32_t start = hal_get_time_ms();
    struct timespec delay = {0, 5L * 1000L * 1000L};
    (void)nanosleep(&delay, NULL);
    test_expect(test, hal_get_time_ms() >= start, "HAL time does not move backward");
}

int main(void)
{
    TestContext test;
    TestBinary binary = {NULL, 0U};
    int needs_binary = HOST_TEST_KIND != HOST_QUEUE && HOST_TEST_KIND != HOST_EMPTY_SCHEDULER && HOST_TEST_KIND != HOST_HAL_TIME;
    if (!test_begin(&test, TEST_NAME, TEST_LOG)) return 1;
    if (!needs_binary || test_load_binary(&test, TEST_WASM, &binary))
    {
#if HOST_TEST_KIND == HOST_TASK_ID
        test_task_id(&test, &binary);
#elif HOST_TEST_KIND == HOST_TASK_ID_LIST
        test_task_id_list(&test, &binary);
#elif HOST_TEST_KIND == HOST_TASK_FIND
        test_task_find(&test, &binary);
#elif HOST_TEST_KIND == HOST_QUEUE
        test_queue(&test);
#elif HOST_TEST_KIND == HOST_PRIORITY
        test_priority(&test, &binary);
#elif HOST_TEST_KIND == HOST_ROUND_ROBIN
        test_round_robin(&test, &binary);
#elif HOST_TEST_KIND == HOST_SUSPEND_RESUME
        test_suspend_resume(&test, &binary);
#elif HOST_TEST_KIND == HOST_DELETION
        test_deletion(&test, &binary);
#elif HOST_TEST_KIND == HOST_WAKE_ORDER
        test_wake_order(&test, &binary);
#elif HOST_TEST_KIND == HOST_TICK_WRAP
        test_tick_wrap(&test, &binary);
#elif HOST_TEST_KIND == HOST_SNAPSHOT_NEGATIVE
        test_snapshot_negative(&test, &binary);
#elif HOST_TEST_KIND == HOST_EXIT_METADATA
        test_exit_metadata(&test, &binary);
#elif HOST_TEST_KIND == HOST_INVALID_ARGS
        test_invalid_args(&test, &binary);
#elif HOST_TEST_KIND == HOST_EMPTY_SCHEDULER
        test_empty_scheduler(&test);
#elif HOST_TEST_KIND == HOST_HAL_TIME
        test_hal_time(&test);
#else
#error Unsupported HOST_TEST_KIND
#endif
    }
    test_free_binary(&binary);
    test_expect_clean_os(&test);
    return test_finish(&test);
}
