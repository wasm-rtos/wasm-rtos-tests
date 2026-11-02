#include <wasm-rtos.h>

int32_t app_main_try(void)
{
    return wr_spi_open_cstr("spi0", "cs-a", WR_SPI_MODE_0, 0u);
}

/* A flash chip on chip select "cs-a": the JEDEC id command, then three bytes back. */
int32_t app_main_flash(void)
{
    static const uint8_t command = 0x9F;
    uint8_t id[3] = { 0, 0, 0 };
    wr_spi_op ops[2] = { { WR_SPI_WRITE, &command, 0, 1u }, { WR_SPI_READ, 0, id, 3u } };
    int32_t flash = wr_spi_open_cstr("spi0", "cs-a", WR_SPI_MODE_3, 1000000u);

    if (flash <= 0)
        return -1;
    if (wr_spi_transaction(flash, ops, 2u) != 0)
        return -2;
    wr_sleep_ms(10u);
    return (id[0] << 16) | (id[1] << 8) | id[2];
}

/* Another device on "cs-b" of the same bus: a full-duplex transfer in place, with a delay. */
int32_t app_main_echo(void)
{
    uint8_t buffer[4] = { 0x01, 0x02, 0x03, 0x04 };
    wr_spi_op ops[2] = { { WR_SPI_DELAY_US, 0, 0, 5u }, { WR_SPI_TRANSFER, buffer, buffer, 4u } };
    int32_t echo = wr_spi_open_cstr("spi0", "cs-b", WR_SPI_MODE_0, 0u);

    if (echo <= 0)
        return 1;
    /* A program driving chip select itself would need the whole bus. */
    if (wr_spi_open_cstr("spi0", "", WR_SPI_MODE_0, 0u) != -WR_EINUSE)
        return 2;
    if (wr_spi_transaction(echo, ops, 2u) != 0)
        return 3;
    if (buffer[0] != 0xFE || buffer[3] != 0xFB)
        return 4;
    return 0;
}

/* The whole bus, chip select through gpio, like an Arduino library. */
int32_t app_main_whole_bus(void)
{
    static const uint8_t command = 0x9F;
    uint8_t id[3] = { 0, 0, 0 };
    int32_t bus = wr_spi_open_cstr("spi0", "", WR_SPI_MODE_0, 100000000u);
    int32_t cs = wr_gpio_open_cstr("cs-a", WR_GPIO_OUTPUT | WR_GPIO_INIT_HIGH);
    wr_spi_op ops[2] = { { WR_SPI_WRITE, &command, 0, 1u }, { WR_SPI_READ, 0, id, 3u } };

    if (bus <= 0 || cs <= 0)
        return -1;
    if (wr_spi_open_cstr("spi0", "cs-b", WR_SPI_MODE_0, 0u) != -WR_EINUSE)
        return -2;
    wr_gpio_set(cs, 0u);
    if (wr_spi_transaction(bus, ops, 2u) != 0)
        return -3;
    wr_gpio_set(cs, 1u);
    return (id[0] << 16) | (id[1] << 8) | id[2];
}

int32_t app_main_errors(void)
{
    uint8_t byte = 0;
    wr_spi_op ops[17];
    int32_t pin = wr_gpio_open_cstr("cs-b", WR_GPIO_OUTPUT);
    int32_t device;

    if (pin <= 0)
        return 1;
    if (wr_spi_open_cstr("nope", "cs-a", 0u, 0u) != -WR_ENOTFOUND || wr_spi_open_cstr("spi0", "nope", 0u, 0u) != -WR_ENOTFOUND)
        return 2;
    if (wr_spi_open_cstr("spi0", "cs-a", 8u, 0u) != -WR_EINVAL)
        return 3;
    /* The program itself holds cs-b as a pin. */
    if (wr_spi_open_cstr("spi0", "cs-b", 0u, 0u) != -WR_EINUSE)
        return 4;
    device = wr_spi_open_cstr("spi0", "cs-a", 0u, 0u);
    if (device <= 0)
        return 5;
    for (int i = 0; i < 17; ++i)
    {
        ops[i].kind = WR_SPI_READ;
        ops[i].write = 0;
        ops[i].read = &byte;
        ops[i].length = 1u;
    }
    if (wr_spi_transaction(device, ops, 0u) != -WR_EINVAL || wr_spi_transaction(device, ops, 17u) != -WR_ELIMIT)
        return 6;
    ops[0].kind = 9u;
    if (wr_spi_transaction(device, ops, 1u) != -WR_EINVAL)
        return 7;
    ops[0].kind = WR_SPI_DELAY_US;
    ops[0].length = 70000u;
    if (wr_spi_transaction(device, ops, 1u) != -WR_EINVAL)
        return 8;
    ops[0].kind = WR_SPI_WRITE;
    ops[0].write = (const void*)0xFFFFFFF0u;
    ops[0].length = 32u;
    if (wr_spi_transaction(device, ops, 1u) != -WR_EFAULT)
        return 9;
    if (wr_wait_any(&device, 1u, 0u) != -WR_EBADH)
        return 10;
    return 0;
}
