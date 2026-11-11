#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    FILE* x = fopen("bus_async.wasm", "rb");
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

/* Chip selects "cs-a" on pin 5 and "cs-b" on pin 6, pulled up. */
static uint32_t g_pin_flags[8];
static uint8_t g_pin_level[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };

static OsStatus gpio_configure(void* context, uint32_t pin, uint32_t flags)
{
    (void)context;
    g_pin_flags[pin] = flags;
    g_pin_level[pin] = (flags & OS_GPIO_OUTPUT) && !(flags & OS_GPIO_INIT_HIGH) ? 0U : 1U;
    return OS_STATUS_OK;
}

static OsStatus gpio_write(void* context, uint32_t pin, uint8_t level)
{
    (void)context;
    g_pin_level[pin] = level;
    return OS_STATUS_OK;
}

static OsStatus gpio_read(void* context, uint32_t pin, uint8_t* out_level)
{
    (void)context;
    *out_level = g_pin_level[pin];
    return OS_STATUS_OK;
}

/*
 * A port with DMA: transaction only starts the transfer. complete() is the
 * "interrupt" at its end, which fills what the flash sends and reports.
 */
static const OsSpiOp* g_spi_ops;
static uint32_t g_spi_count;
static uint32_t g_spi_started;
static uint32_t g_finishes;
static uint8_t g_cs_low_at_end;

static void spi_complete(OsStatus status)
{
    static const uint8_t jedec[3] = { 0xEF, 0x40, 0x18 };
    uint32_t index;
    g_cs_low_at_end = g_pin_level[5] == 0U;
    for (index = 0; index < g_spi_count; index++)
        if (g_spi_ops[index].kind == OS_SPI_READ)
            memcpy(g_spi_ops[index].read, jedec, 3);
    g_spi_ops = NULL;
    os_bus_complete(OS_BUS_SPI, 0U, status);
}

static OsStatus spi_transaction(void* context, uint32_t bus, uint32_t mode, uint32_t frequency_hz,
                                const OsSpiOp* ops, uint32_t count)
{
    (void)context;
    (void)bus;
    (void)mode;
    (void)frequency_hz;
    g_spi_ops = ops;
    g_spi_count = count;
    g_spi_started++;
    return OS_STATUS_PENDING;
}

/* Waits for the transfer: here it ends at once. */
static void spi_finish(void* context, uint32_t bus)
{
    (void)context;
    (void)bus;
    g_finishes++;
    if (g_spi_ops != NULL)
        spi_complete(OS_STATUS_OK);
}

static const OsI2cOp* g_i2c_ops;

static OsStatus i2c_transaction(void* context, uint32_t bus, uint32_t address, uint32_t frequency_hz,
                                const OsI2cOp* ops, uint32_t count)
{
    (void)context;
    (void)bus;
    (void)address;
    (void)frequency_hz;
    (void)count;
    g_i2c_ops = ops;
    return OS_STATUS_PENDING;
}

static void i2c_finish(void* context, uint32_t bus)
{
    (void)context;
    (void)bus;
}

static uint32_t g_traces;
static OsBusTrace g_last_trace;

static void trace(void* context, const OsBusTrace* record)
{
    (void)context;
    g_traces++;
    g_last_trace = *record;
}

/* A page store in host memory, so programs can be swapped out. */
static uint8_t g_slots[256][4096];

static OsStatus store_write(void* context, uint32_t slot, const void* frame)
{
    (void)context;
    if (slot >= 256U)
        return OS_STATUS_OUT_OF_MEMORY;
    memcpy(g_slots[slot], frame, 4096U);
    return OS_STATUS_OK;
}

static OsStatus store_read(void* context, uint32_t slot, void* frame)
{
    (void)context;
    if (slot >= 256U)
        return OS_STATUS_OUT_OF_MEMORY;
    memcpy(frame, g_slots[slot], 4096U);
    return OS_STATUS_OK;
}

static OsTaskHandle start(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    if (os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
    return t;
}

static void run_until_waiting(OsTaskHandle t)
{
    uint32_t i;
    for (i = 0; i < 1000U && os_task_get_state(t) != OS_TASK_WAITING && os_task_get_state(t) != OS_TASK_DEAD; i++)
        os_schedule();
}

static int32_t result_of(OsTaskHandle t)
{
    OsValue v;
    uint32_t i;
    for (i = 0; i < 1000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(1U);
    if (os_task_get_state(t) != OS_TASK_DEAD || os_task_get_return_value(t, &v) != OS_STATUS_OK)
        return -100000;
    return (int32_t)v.value.i32;
}

int main(void)
{
    Bin b = {0};
    OsGpioPort gpio = { NULL, gpio_configure, gpio_write, gpio_read };
    OsSpiPort spi = { NULL, spi_transaction, spi_finish };
    OsI2cPort i2c = { NULL, i2c_transaction, i2c_finish };
    OsPageStore store = { NULL, 256U, 64U, store_write, store_read, NULL, NULL, NULL };
    OsTaskHandle flash = NULL;
    OsTaskHandle spin = NULL;
    OsTaskHandle second = NULL;
    uint32_t runs;
    uint32_t i;

    l = fopen("bus_async.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load bus_async.wasm");
    if (!b.d)
        return 1;
    ok(os_gpio_port_set(&gpio) == OS_STATUS_OK && os_gpio_add_pin("cs-a", 5U, OS_GPIO_CAN_ALL, 0U) == OS_STATUS_OK &&
           os_gpio_add_pin("cs-b", 6U, OS_GPIO_CAN_ALL, 0U) == OS_STATUS_OK &&
           os_spi_port_set(&spi) == OS_STATUS_OK && os_spi_add_bus("spi0", 0U, 10000000U) == OS_STATUS_OK &&
           os_i2c_port_set(&i2c) == OS_STATUS_OK && os_i2c_add_bus("i2c0", 0U, 400000U) == OS_STATUS_OK &&
           os_page_store_set(&store) == OS_STATUS_OK,
       "a board whose SPI and I2C ports use DMA");
    os_bus_trace_set(trace, NULL);

    /* The transfer runs in the background: the program waits, the others run. */
    flash = start(&b, "app_main_flash");
    spin = start(&b, "app_main_spin");
    run_until_waiting(flash);
    ok(os_task_get_state(flash) == OS_TASK_WAITING && g_spi_started == 1U && os_bus_is_busy(OS_BUS_SPI, 0U),
       "the program waits while the port works");
    ok(g_pin_level[5] == 0U, "chip select stays low during the transfer");
    runs = os_task_get_run_count(spin);
    for (i = 0; i < 5U; i++)
        os_schedule();
    ok(os_task_get_run_count(spin) == runs + 5U, "other programs run meanwhile");
    ok(os_task_swap_out(flash) == OS_STATUS_BUSY, "a program whose memory the port uses is not swapped out");
    ok(g_traces == 0U, "nothing is traced before the transfer ends");

    spi_complete(OS_STATUS_OK);
    ok(g_cs_low_at_end && os_task_get_state(flash) == OS_TASK_WAITING, "the end is only recorded, as in an interrupt handler");
    os_schedule();
    ok(g_pin_level[5] == 1U && !os_bus_is_busy(OS_BUS_SPI, 0U), "the next schedule delivers it: chip select up");
    ok(g_traces == 1U && g_last_trace.task_id == os_task_get_id(flash) && g_last_trace.status == OS_STATUS_OK &&
           g_last_trace.count == 2U,
       "the trace comes at the end, with the program and the result");
    ok(result_of(flash) == 0xEF4018, "the bytes the port wrote into the program's memory arrive");
    os_task_delete(flash);

    /* An error at the end reaches the program. */
    flash = start(&b, "app_main_flash");
    run_until_waiting(flash);
    spi_complete(OS_STATUS_BUS_ERROR);
    ok(result_of(flash) == -24, "a failed transfer gives -WR_EBUS");
    os_task_delete(flash);

    /* Pausing waits for the transfer; the program gets its result when it resumes. */
    flash = start(&b, "app_main_flash");
    run_until_waiting(flash);
    g_finishes = 0U;
    ok(os_task_suspend(flash) == OS_STATUS_OK && g_finishes == 1U && !os_bus_is_busy(OS_BUS_SPI, 0U),
       "suspend makes the port finish first");
    ok(os_task_swap_out(flash) == OS_STATUS_OK, "after that it can be swapped out");
    ok(os_task_resume(flash) == OS_STATUS_OK && result_of(flash) == 0xEF4018, "on resume the program has its bytes");
    os_task_delete(flash);

    /* Aborting a wait cannot call bytes back: the transfer ends and its result counts. */
    flash = start(&b, "app_main_flash");
    run_until_waiting(flash);
    g_finishes = 0U;
    ok(os_task_abort_wait(flash) == OS_STATUS_OK && g_finishes == 1U && result_of(flash) == 0xEF4018,
       "abort-wait finishes the transfer");
    os_task_delete(flash);

    /* Deleting a program waits for the port before its memory goes. */
    flash = start(&b, "app_main_flash");
    run_until_waiting(flash);
    g_finishes = 0U;
    ok(os_task_delete(flash) == OS_STATUS_OK && g_finishes == 1U && g_pin_level[5] == 1U && g_spi_ops == NULL,
       "delete makes the port finish before the memory is freed");

    /* A second program on a busy bus waits out the first transfer. */
    flash = start(&b, "app_main_flash");
    second = start(&b, "app_main_flash_b");
    run_until_waiting(flash);
    g_finishes = 0U;
    g_spi_started = 0U;
    os_task_suspend(spin);
    run_until_waiting(second);
    ok(os_task_get_state(second) == OS_TASK_WAITING, "the second program got the bus too");
    ok(g_finishes == 1U && g_spi_started == 1U, "after the port finished the first transfer");
    spi_complete(OS_STATUS_OK);
    ok(result_of(flash) == 0xEF4018 && result_of(second) == 0xEF4018, "both programs have their bytes");
    os_task_delete(flash);
    os_task_delete(second);

    /* I2C works the same way. */
    flash = start(&b, "app_main_sensor");
    run_until_waiting(flash);
    ok(os_task_get_state(flash) == OS_TASK_WAITING && os_bus_is_busy(OS_BUS_I2C, 0U), "an I2C transfer in the background");
    g_i2c_ops[1].data[0] = 0x19;
    g_i2c_ops[1].data[1] = 0x80;
    os_bus_complete(OS_BUS_I2C, 0U, OS_STATUS_OK);
    ok(result_of(flash) == 0x1980, "its read arrives");
    os_task_delete(flash);

    os_task_delete(spin);
    os_bus_trace_set(NULL, NULL);
    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
