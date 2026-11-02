#include <wasm-rtos.h>

int32_t app_main_try(void)
{
    return wr_uart_open_cstr("uart1", 9600u, 0u);
}

/* A GPS-like exchange: send a command that does not fit at once, then read the reply. */
int32_t app_main_talk(void)
{
    static const char command[] = "$PMTK220,1000*1F\r\n";
    char reply[64];
    int32_t port = wr_uart_open_cstr("uart1", 115200u, WR_UART_PARITY_EVEN);
    int32_t got = 0;

    if (port <= 0)
        return -1;
    if (wr_uart_get(port, WR_UART_BAUD) != 115200)
        return -2;
    if (wr_uart_read(port, reply, sizeof(reply)) != 0)
        return -3;
    /* The port takes 8 bytes at a time; wr_uart_write_all waits for room in between. */
    if (wr_uart_write_all(port, command, sizeof(command) - 1u, 1000u) != (int32_t)(sizeof(command) - 1u))
        return -4;
    /* Wait for the reply, then read what came. */
    while (got < 6)
    {
        int32_t n;
        if (wr_wait_any(&port, 1u, 1000u) != 0)
            return -5;
        n = wr_uart_read(port, reply + got, sizeof(reply) - (uint32_t)got);
        if (n <= 0)
            return -6;
        got += n;
    }
    if (reply[0] != '$' || reply[4] != 'K')
        return -7;
    return wr_uart_get(port, WR_UART_ERRORS);
}

int32_t app_main_errors(void)
{
    int32_t port = wr_uart_open_cstr("uart1", 9600u, 0u);
    uint8_t byte = 'x';

    if (port <= 0)
        return 1;
    if (wr_uart_open_cstr("uart1", 9600u, 0u) != -WR_EINUSE || wr_uart_open_cstr("nope", 9600u, 0u) != -WR_ENOTFOUND)
        return 2;
    if (wr_uart_open_cstr("uart2", 0u, 0u) != -WR_EINVAL ||
        wr_uart_open_cstr("uart2", 9600u, WR_UART_PARITY_EVEN | WR_UART_PARITY_ODD) != -WR_EINVAL ||
        wr_uart_open_cstr("uart2", 9600u, 0x40u) != -WR_EINVAL)
        return 3;
    if (wr_uart_open_cstr("uart2", 2000000u, 0u) != -WR_EUNSUPPORTED)
        return 4;
    if (wr_uart_read(port, (void*)0xFFFFFFF0u, 32u) != -WR_EFAULT || wr_uart_write(port, (void*)0xFFFFFFF0u, 32u) != -WR_EFAULT)
        return 5;
    /* Room for 8 bytes: a ninth does not fit, and a check without waiting says so. */
    if (wr_uart_write(port, "12345678", 8u) != 8 || wr_uart_write(port, &byte, 1u) != 0 ||
        wr_uart_wait_writable(port, 0u) != -WR_EFULL)
        return 6;
    if (wr_uart_wait_writable(port, 20u) != -WR_ETIMEOUT)
        return 7;
    if (wr_uart_get(port, 9u) != -WR_EUNSUPPORTED || wr_uart_read(99, &byte, 1u) != -WR_EBADH)
        return 8;
    return 0;
}

int32_t app_main_overflow(void)
{
    int32_t port = wr_uart_open_cstr("uart1", 9600u, 0u);
    int32_t available;
    if (port <= 0)
        return -1;
    wr_sleep_ms(10u);
    available = wr_uart_get(port, WR_UART_AVAILABLE);
    return wr_uart_get(port, WR_UART_ERRORS) * 1000 + available;
}
