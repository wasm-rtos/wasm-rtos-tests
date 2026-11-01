#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    uint8_t* d;
    uint32_t n;
} Bin;

static FILE* l;
static int f;

static void ok(int c, const char* s)
{
    printf("%s %s\n", c ? "PASS" : "FAIL", s);
    if (l)
    {
        fprintf(l, "%s %s\n", c ? "PASS" : "FAIL", s);
        fflush(l);
    }
    if (!c)
        f++;
}

static int load(Bin* b)
{
    FILE* x = fopen("gpio_api.wasm", "rb");
    long n;
    if (!x)
        return 0;
    fseek(x, 0, SEEK_END);
    n = ftell(x);
    rewind(x);
    if (n <= 0)
        return fclose(x), 0;
    b->d = malloc((size_t)n);
    b->n = (uint32_t)n;
    if (!b->d || fread(b->d, 1, (size_t)n, x) != (size_t)n)
    {
        fclose(x);
        free(b->d);
        b->d = NULL;
        return 0;
    }
    fclose(x);
    return 1;
}

/* A board in memory: what the kernel configured and drove on each pin. */
typedef struct FakePin
{
    uint32_t flags;
    uint8_t level;      /* driven by an output */
    uint8_t input;      /* what the outside world puts on the pin */
    uint32_t configures;
} FakePin;

static FakePin g_pins[8];

static OsStatus fake_configure(void* context, uint32_t pin, uint32_t flags)
{
    (void)context;
    if (pin >= 8U)
        return OS_STATUS_INVALID_ARGUMENT;
    g_pins[pin].flags = flags;
    g_pins[pin].configures++;
    if (flags & OS_GPIO_OUTPUT)
        g_pins[pin].level = (flags & OS_GPIO_INIT_HIGH) ? 1U : 0U;
    return OS_STATUS_OK;
}

static OsStatus fake_write(void* context, uint32_t pin, uint8_t level)
{
    (void)context;
    if (pin >= 8U)
        return OS_STATUS_INVALID_ARGUMENT;
    g_pins[pin].level = level;
    return OS_STATUS_OK;
}

static OsStatus fake_read(void* context, uint32_t pin, uint8_t* out_level)
{
    (void)context;
    if (pin >= 8U)
        return OS_STATUS_INVALID_ARGUMENT;
    *out_level = (g_pins[pin].flags & OS_GPIO_OUTPUT) ? g_pins[pin].level : g_pins[pin].input;
    return OS_STATUS_OK;
}

static const OsGpioPort g_port = { NULL, fake_configure, fake_write, fake_read };

static OsTaskHandle create(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    if (os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
    return t;
}

static int32_t run_to_result(OsTaskHandle t)
{
    OsValue v;
    uint32_t i;
    for (i = 0; t && i < 1000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
    {
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(10U);
    }
    if (!t || os_task_get_state(t) != OS_TASK_DEAD || os_task_get_return_value(t, &v) != OS_STATUS_OK)
        return -100000;
    return (int32_t)v.value.i32;
}

static void run_until_waiting(OsTaskHandle t)
{
    uint32_t i;
    for (i = 0; t && i < 1000U && os_task_get_state(t) == OS_TASK_READY; i++)
        os_schedule();
}

static void report(const char* entry, int32_t result)
{
    if (result != 0)
        printf("     %s returned %d\n", entry, (int)result);
}

static void setup_board(void)
{
    ok(os_gpio_port_set(&g_port) == OS_STATUS_OK, "install the GPIO port");
    ok(os_gpio_add_pin("led", 2U, OS_GPIO_CAN_INPUT | OS_GPIO_CAN_OUTPUT, OS_GPIO_ACTIVE_LOW) == OS_STATUS_OK &&
           os_gpio_add_pin("gpio2", 2U, OS_GPIO_CAN_INPUT | OS_GPIO_CAN_OUTPUT, OS_GPIO_ACTIVE_LOW) == OS_STATUS_OK,
       "register the active-low LED under two names");
    ok(os_gpio_add_pin("button-a", 0U, OS_GPIO_CAN_INPUT | OS_GPIO_CAN_PULL_UP | OS_GPIO_CAN_EDGES, 0U) == OS_STATUS_OK,
       "register a button that reports edges");
    ok(os_gpio_add_pin("sensor", 4U,
                       OS_GPIO_CAN_INPUT | OS_GPIO_CAN_OUTPUT | OS_GPIO_CAN_OPEN_DRAIN | OS_GPIO_CAN_PULL_UP,
                       0U) == OS_STATUS_OK,
       "register an open-drain sensor pin");
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;
    OsTaskHandle holder = NULL;
    int32_t result;

    l = fopen("gpio_api.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load gpio_api.wasm");
    if (!b.d)
        return 1;

    /* No port: programs learn that this device has no pins for them. */
    t = create(&b, "app_main_try");
    ok(run_to_result(t) == -13, "without a port gpio-open returns -WR_EUNSUPPORTED");
    os_task_delete(t);

    setup_board();
    ok(os_gpio_add_pin("led", 3U, OS_GPIO_CAN_OUTPUT, 0U) == OS_STATUS_BUSY, "a name is registered once");
    ok(os_gpio_add_pin("d2", 2U, OS_GPIO_CAN_OUTPUT, 0U) == OS_STATUS_INVALID_ARGUMENT,
       "another name for a pin must describe it the same way");
    ok(os_gpio_add_pin("a-name-that-is-longer-than-32-bytes", 5U, OS_GPIO_CAN_ALL, 0U) == OS_STATUS_INVALID_ARGUMENT,
       "names longer than 32 bytes are refused");

    /* Logical levels over an inverted pin, and release at the end. */
    t = create(&b, "app_main_led");
    run_until_waiting(t);
    ok(g_pins[2].flags == OS_GPIO_OUTPUT && g_pins[2].level == 0U,
       "\"on\" drives the active-low LED low from the start");
    os_tick(10U);
    run_until_waiting(t);
    ok(g_pins[2].level == 1U, "writing 0 drives it high");
    os_tick(10U);
    result = run_to_result(t);
    report("app_main_led", result);
    ok(result == 0, "levels, aliases and bad levels behave");
    ok(g_pins[2].flags == 0U, "the kernel released the pin as an input without pulls");
    os_task_delete(t);

    t = create(&b, "app_main_errors");
    result = run_to_result(t);
    report("app_main_errors", result);
    ok(result == 0, "unknown names, bad and unsupported modes, bad handles; a 1.0 import still links");
    ok(g_pins[4].flags == 0U, "a closed pin is released too");
    os_task_delete(t);

    /* Edges, counted from an "interrupt handler", end a wait. */
    t = create(&b, "app_main_button");
    g_pins[0].input = 1U;
    run_until_waiting(t);
    ok(os_task_get_state(t) == OS_TASK_WAITING, "the program waits for the button or its timer");
    ok(g_pins[0].flags == (OS_GPIO_PULL_UP | OS_GPIO_EDGE_FALLING),
       "a logical press of an active-low button asks the port for falling edges");
    g_pins[0].input = 0U;
    os_gpio_edge(0U, OS_GPIO_EDGE_FALLING);
    g_pins[0].input = 1U;
    os_gpio_edge(0U, OS_GPIO_EDGE_RISING);
    g_pins[0].input = 0U;
    os_gpio_edge(0U, OS_GPIO_EDGE_FALLING);
    ok(os_task_get_state(t) == OS_TASK_WAITING, "edges wait for the scheduler");
    result = run_to_result(t);
    ok(result == 21, "two presses counted, releases ignored, the button reads pressed");
    os_task_delete(t);

    /* One owner at a time; a stopped or crashed owner gives the pin back. */
    holder = create(&b, "app_main_hold");
    run_until_waiting(holder);
    ok(os_gpio_get_owner(2U) == os_task_get_id(holder) && os_gpio_get_owner(4U) == 0U,
       "the host sees who owns a pin");
    t = create(&b, "app_main_try");
    ok(run_to_result(t) == -22, "a second program gets -WR_EINUSE");
    os_task_delete(t);
    os_task_delete(holder);
    ok(g_pins[2].flags == 0U, "deleting the owner releases the pin");
    t = create(&b, "app_main_try");
    ok(run_to_result(t) > 0, "then another program can open it");
    os_task_delete(t);

    t = create(&b, "app_main_crash");
    run_to_result(t);
    ok(os_task_get_exit_reason(t) == OS_TASK_EXIT_WASM_ERROR && g_pins[2].flags == 0U,
       "a program that traps leaves its pins safe");
    os_task_delete(t);

    free(b.d);
    ok(os_get_task_count() == 0U, "OS is clean");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
