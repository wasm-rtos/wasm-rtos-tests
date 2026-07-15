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
    FILE* file = fopen("queue_blocking.wasm", "rb");
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
    uint32_t queue_id,
    uint32_t value,
    uint32_t arg_count,
    uint32_t priority
)
{
    OsValue args[2];

    args[0].type = OS_VALUE_TYPE_I32;
    args[0].value.i32 = queue_id;
    args[1].type = OS_VALUE_TYPE_I32;
    args[1].value.i32 = value;
    return os_task_create_with_args(
        out_task,
        wasm->bytes,
        wasm->size,
        entry,
        args,
        arg_count,
        name,
        64U * 1024U,
        priority
    );
}

static void delete_task(OsTaskHandle* task)
{
    if (*task != NULL)
    {
        expect(os_task_delete(*task) == OS_STATUS_OK, "delete queue task");
        *task = NULL;
    }
}

int main(void)
{
    WasmBinary wasm = {0};
    OsQueueHandle queue = NULL;
    OsTaskHandle first_task = NULL;
    OsTaskHandle second_task = NULL;
    uint32_t first = 0U;
    uint32_t second = 0U;
    uint32_t received = 0U;
    uint32_t queue_id = 0U;

    g_log = fopen("queue_blocking.log", "w");
    if (g_log == NULL)
    {
        return 1;
    }

    hal_init();
    expect(os_init() == OS_STATUS_OK, "initialize OS");
    expect(load_wasm(&wasm), "load blocking queue test module");

    expect(os_queue_create(&queue, sizeof(uint32_t), 1U) == OS_STATUS_OK,
           "create direct-handoff queue");
    queue_id = os_queue_get_id(queue);
    expect(create_task(&first_task, &wasm, "app_main_receive", "high_receiver",
                       queue_id, 0U, 1U, OS_TASK_PRIORITY_HIGH) == OS_STATUS_OK,
           "create high-priority receiver");
    expect(os_schedule() == OS_STATUS_OK, "empty queue blocks receiver");
    expect(os_task_get_state(first_task) == OS_TASK_WAITING,
           "receiver enters WAITING");
    expect(create_task(&second_task, &wasm, "app_main_send", "low_sender",
                       queue_id, 42U, 2U, OS_TASK_PRIORITY_LOW) == OS_STATUS_OK,
           "create low-priority sender");
    expect(os_schedule() == OS_STATUS_OK, "sender directly hands off item");
    expect(os_task_get_state(first_task) == OS_TASK_READY,
           "handoff wakes receiver");
    expect(os_task_get_state(second_task) == OS_TASK_READY,
           "higher-priority wake preempts sender");
    expect(os_queue_get_count(queue) == 0U,
           "direct handoff does not consume queue storage");
    expect(os_schedule() == OS_STATUS_OK, "high-priority receiver resumes first");
    expect(os_task_get_state(first_task) == OS_TASK_DEAD &&
               os_task_get_exit_code(first_task) == 42U,
           "blocked receive returns handed-off value");
    expect(os_schedule() == OS_STATUS_OK, "preempted sender finishes");
    expect(os_task_get_state(second_task) == OS_TASK_DEAD &&
               os_task_get_exit_code(second_task) == OS_STATUS_OK,
           "blocked-send import returns success");
    delete_task(&first_task);
    delete_task(&second_task);
    os_queue_delete(queue);
    queue = NULL;

    first = 10U;
    second = 20U;
    expect(os_queue_create(&queue, sizeof(uint32_t), 1U) == OS_STATUS_OK,
           "create full-queue scenario");
    queue_id = os_queue_get_id(queue);
    expect(os_queue_send(queue, &first) == OS_STATUS_OK,
           "fill queue before blocking sender");
    expect(create_task(&first_task, &wasm, "app_main_send", "high_sender",
                       queue_id, second, 2U, OS_TASK_PRIORITY_HIGH) == OS_STATUS_OK,
           "create high-priority blocked sender");
    expect(os_schedule() == OS_STATUS_OK, "full queue blocks sender");
    expect(os_task_get_state(first_task) == OS_TASK_WAITING,
           "sender enters WAITING");
    expect(create_task(&second_task, &wasm, "app_main_receive", "low_receiver",
                       queue_id, 0U, 1U, OS_TASK_PRIORITY_LOW) == OS_STATUS_OK,
           "create low-priority receiver");
    expect(os_schedule() == OS_STATUS_OK, "receiver frees queue slot");
    expect(os_task_get_state(first_task) == OS_TASK_READY,
           "freed slot wakes blocked sender");
    expect(os_task_get_state(second_task) == OS_TASK_READY,
           "woken sender preempts lower-priority receiver");
    expect(os_queue_get_count(queue) == 1U,
           "blocked sender fills newly freed slot");
    expect(os_schedule() == OS_STATUS_OK, "high-priority sender resumes first");
    expect(os_task_get_exit_code(first_task) == OS_STATUS_OK,
           "blocked sender returns success");
    expect(os_schedule() == OS_STATUS_OK, "preempted receiver finishes");
    expect(os_task_get_exit_code(second_task) == first,
           "receiver gets oldest queued item");
    expect(os_queue_receive(queue, &received) == OS_STATUS_OK && received == second,
           "sender item remains next in FIFO order");
    delete_task(&first_task);
    delete_task(&second_task);
    os_queue_delete(queue);
    queue = NULL;

    expect(os_queue_create(&queue, sizeof(uint32_t), 1U) == OS_STATUS_OK,
           "create priority-order queue");
    queue_id = os_queue_get_id(queue);
    expect(create_task(&first_task, &wasm, "app_main_receive", "low_waiter",
                       queue_id, 0U, 1U, OS_TASK_PRIORITY_LOW) == OS_STATUS_OK,
           "create low-priority waiter");
    expect(os_schedule() == OS_STATUS_OK, "low-priority waiter blocks");
    expect(create_task(&second_task, &wasm, "app_main_receive", "high_waiter",
                       queue_id, 0U, 1U, OS_TASK_PRIORITY_HIGH) == OS_STATUS_OK,
           "create high-priority waiter");
    expect(os_schedule() == OS_STATUS_OK, "high-priority waiter blocks");
    first = 101U;
    second = 202U;
    expect(os_queue_send(queue, &first) == OS_STATUS_OK,
           "send one item to competing waiters");
    expect(os_task_get_state(second_task) == OS_TASK_READY &&
               os_task_get_state(first_task) == OS_TASK_WAITING,
           "highest-priority receiver wakes first");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_exit_code(second_task) == first,
           "high-priority receiver gets first item");
    expect(os_queue_send(queue, &second) == OS_STATUS_OK,
           "send item to remaining waiter");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_exit_code(first_task) == second,
           "low-priority receiver gets second item");
    delete_task(&first_task);
    delete_task(&second_task);
    os_queue_delete(queue);
    queue = NULL;

    expect(os_queue_create(&queue, sizeof(uint32_t), 1U) == OS_STATUS_OK,
           "create equal-priority FIFO queue");
    queue_id = os_queue_get_id(queue);
    expect(create_task(&first_task, &wasm, "app_main_receive", "first_waiter",
                       queue_id, 0U, 1U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create first equal-priority waiter");
    expect(os_schedule() == OS_STATUS_OK, "first equal-priority waiter blocks");
    expect(create_task(&second_task, &wasm, "app_main_receive", "second_waiter",
                       queue_id, 0U, 1U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create second equal-priority waiter");
    expect(os_schedule() == OS_STATUS_OK, "second equal-priority waiter blocks");
    first = 501U;
    second = 502U;
    expect(os_queue_send(queue, &first) == OS_STATUS_OK &&
               os_queue_send(queue, &second) == OS_STATUS_OK,
           "send two direct-handoff values");
    expect(os_schedule() == OS_STATUS_OK && os_schedule() == OS_STATUS_OK,
           "resume both equal-priority waiters");
    expect(os_task_get_exit_code(first_task) == first &&
               os_task_get_exit_code(second_task) == second,
           "equal-priority waiters wake in FIFO wait order");
    delete_task(&first_task);
    delete_task(&second_task);
    os_queue_delete(queue);
    queue = NULL;

    expect(os_queue_create(&queue, sizeof(uint32_t), 1U) == OS_STATUS_OK,
           "create timeout queue");
    queue_id = os_queue_get_id(queue);
    expect(create_task(&first_task, &wasm, "app_main_receive_timeout",
                       "receive_timeout", queue_id, 0U, 1U,
                       OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create receive-timeout task");
    expect(os_schedule() == OS_STATUS_OK, "receive-timeout task blocks");
    os_tick(4U);
    expect(os_task_get_state(first_task) == OS_TASK_WAITING,
           "receiver remains blocked before deadline");
    os_tick(1U);
    expect(os_task_get_state(first_task) == OS_TASK_READY,
           "receiver wakes exactly at deadline");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_exit_code(first_task) == OS_STATUS_TIMEOUT,
           "receive wait returns timeout");
    delete_task(&first_task);
    first = 303U;
    expect(os_queue_send(queue, &first) == OS_STATUS_OK,
           "fill timeout queue");
    expect(create_task(&first_task, &wasm, "app_main_send_timeout",
                       "send_timeout", queue_id, 404U, 2U,
                       OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create send-timeout task");
    expect(os_schedule() == OS_STATUS_OK, "send-timeout task blocks");
    os_tick(4U);
    expect(os_task_get_state(first_task) == OS_TASK_WAITING,
           "sender remains blocked before deadline");
    os_tick(1U);
    expect(os_task_get_state(first_task) == OS_TASK_READY,
           "sender wakes exactly at deadline");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_exit_code(first_task) == OS_STATUS_TIMEOUT,
           "send wait returns timeout");
    expect(os_queue_receive(queue, &received) == OS_STATUS_OK && received == first,
           "timed-out sender does not modify queue");
    delete_task(&first_task);
    os_queue_delete(queue);
    queue = NULL;

    expect(os_queue_create(&queue, sizeof(uint32_t), 1U) == OS_STATUS_OK,
           "create deletion queue");
    queue_id = os_queue_get_id(queue);
    expect(create_task(&first_task, &wasm, "app_main_receive", "deleted_waiter",
                       queue_id, 0U, 1U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create waiter for deleted queue");
    expect(os_schedule() == OS_STATUS_OK, "deleted-queue waiter blocks");
    os_queue_delete(queue);
    queue = NULL;
    expect(os_task_get_state(first_task) == OS_TASK_READY,
           "queue deletion wakes receiver");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_exit_code(first_task) ==
                   (0x80000000U | (uint32_t)OS_STATUS_QUEUE_NOT_FOUND),
           "deleted queue returns not-found status");
    delete_task(&first_task);

    expect(os_queue_create(&queue, sizeof(uint32_t), 1U) == OS_STATUS_OK,
           "create sender-deletion queue");
    queue_id = os_queue_get_id(queue);
    first = 601U;
    expect(os_queue_send(queue, &first) == OS_STATUS_OK,
           "fill sender-deletion queue");
    expect(create_task(&first_task, &wasm, "app_main_send", "deleted_sender",
                       queue_id, 602U, 2U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK,
           "create sender for deleted queue");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_state(first_task) == OS_TASK_WAITING,
           "deleted-queue sender blocks");
    os_queue_delete(queue);
    queue = NULL;
    expect(os_task_get_state(first_task) == OS_TASK_READY,
           "queue deletion wakes sender");
    expect(os_schedule() == OS_STATUS_OK &&
               os_task_get_exit_code(first_task) == OS_STATUS_QUEUE_NOT_FOUND,
           "deleted queue returns not-found to sender");
    delete_task(&first_task);

    free(wasm.bytes);
    expect(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    hal_shutdown();
    fclose(g_log);
    return g_failures == 0 ? 0 : 1;
}
