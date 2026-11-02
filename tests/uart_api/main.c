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

#define WASM_FILE "uart_api.wasm"

static int load(Bin* b)
{
    FILE* x = fopen(WASM_FILE, "rb");
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

/*
 * Two serial ports. The port's send buffer holds 8 bytes; drain() is the
 * transmit interrupt emptying it. The peer answers a command line with
 * "$PMTK001,220,3*30" through the receive interrupt.
 */
static uint32_t g_baud;
static uint32_t g_format;
static uint8_t g_fifo[8];
static uint32_t g_fifo_used;
static char g_sent[64];
static uint32_t g_sent_length;
static uint32_t g_closed;

static OsStatus uart_configure(void* context, uint32_t uart, uint32_t baud, uint32_t format, uint32_t* out_baud)
{
    (void)context;
    (void)uart;
    g_baud = baud;
    g_format = format;
    *out_baud = baud;
    return OS_STATUS_OK;
}

static OsStatus uart_write(void* context, uint32_t uart, const uint8_t* data, uint32_t size, uint32_t* out_written)
{
    uint32_t room = sizeof(g_fifo) - g_fifo_used;
    uint32_t count = size < room ? size : room;
    (void)context;
    (void)uart;
    memcpy(g_fifo + g_fifo_used, data, count);
    g_fifo_used += count;
    *out_written = count;
    return OS_STATUS_OK;
}

static void uart_close(void* context, uint32_t uart)
{
    (void)context;
    (void)uart;
    g_closed++;
    g_fifo_used = 0;
}

/* The transmit interrupt: the bytes are on the wire, there is room again. */
static void drain(void)
{
    if (g_fifo_used == 0)
        return;
    memcpy(g_sent + g_sent_length, g_fifo, g_fifo_used);
    g_sent_length += g_fifo_used;
    g_fifo_used = 0;
    os_uart_writable(1U);
}

static OsTaskHandle start(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL);
    return t;
}

static int32_t finish(OsTaskHandle t, const char* entry)
{
    OsValue v;
    int32_t result = -100000;
    for (uint32_t i = 0; t && i < 1000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(10U);
    if (t && os_task_get_state(t) == OS_TASK_DEAD && os_task_get_return_value(t, &v) == OS_STATUS_OK)
        result = (int32_t)v.value.i32;
    printf("     %s returned %d\n", entry, (int)result);
    if (t)
        os_task_delete(t);
    return result;
}

int main(void)
{
    static const uint8_t reply[] = "$PMTK001,220,3*30\r\n";
    Bin b = {0};
    OsUartPort port = { NULL, uart_configure, uart_write, uart_close };
    OsUartPort broken = { NULL, NULL, uart_write, NULL };
    OsTaskHandle t = NULL;
    uint32_t i;
    uint8_t noise[300];

    l = fopen("uart_api.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load uart_api.wasm");
    if (!b.d)
        return 1;
    ok(finish(start(&b, "app_main_try"), "app_main_try") == -13, "without a port uart-open returns -WR_EUNSUPPORTED");
    ok(os_uart_port_set(&broken) == OS_STATUS_INVALID_ARGUMENT && os_uart_port_set(&port) == OS_STATUS_OK,
       "a port needs configure and write");
    ok(os_uart_add("uart1", 1U, 0U) == OS_STATUS_OK && os_uart_add("uart2", 2U, 921600U) == OS_STATUS_OK &&
           os_uart_add("uart1", 3U, 0U) == OS_STATUS_BUSY,
       "register two serial ports, the second up to 921600 baud");

    /* Bytes before the program opens the port are not kept. */
    os_uart_receive(1U, reply, 4U);
    t = start(&b, "app_main_talk");
    for (i = 0; i < 1000U && g_sent_length + g_fifo_used < 18U; i++)
    {
        if (os_task_get_state(t) == OS_TASK_WAITING)
            drain();
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(1U);
    }
    drain();
    ok(g_baud == 115200U && g_format == OS_UART_PARITY_EVEN, "the port runs at 115200 with even parity");
    ok(g_sent_length == 18U && memcmp(g_sent, "$PMTK220,1000*1F\r\n", 18U) == 0,
       "the whole command went out, 8 bytes at a time, the program waiting for room");
    ok(os_uart_get_owner(1U) == os_task_get_id(t), "the program owns uart1");
    /* The reply comes in pieces from the receive interrupt, and a framing error with it. */
    os_uart_receive(1U, reply, 3U);
    os_uart_error(1U);
    os_uart_receive(1U, reply + 3, sizeof(reply) - 4U);
    ok(finish(t, "app_main_talk") == 1, "the program woke for the reply, read it whole and saw one error");
    ok(g_closed == 1U && os_uart_get_owner(1U) == 0U, "the port is closed and free when the program ends");

    t = start(&b, "app_main_errors");
    ok(finish(t, "app_main_errors") == 0, "busy, unknown, bad speed and format, bad pointers, full, timeout");

    /* A full receive buffer keeps 256 bytes, drops the rest and counts them. */
    t = start(&b, "app_main_overflow");
    for (i = 0; i < 1000U && os_task_get_state(t) != OS_TASK_WAITING; i++)
        os_schedule();
    memset(noise, 'n', sizeof(noise));
    os_uart_receive(1U, noise, sizeof(noise));
    ok(finish(t, "app_main_overflow") == 44 * 1000 + 256, "256 bytes kept, 44 dropped and counted");

    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
