#include <wasm-rtos.h>

/* The JEDEC id of the flash on chip select cs: a command, then three bytes back. */
static int32_t read_id(const char* cs)
{
    static const uint8_t command = 0x9F;
    uint8_t id[3] = { 0, 0, 0 };
    wr_spi_op ops[2] = { { WR_SPI_WRITE, &command, 0, 1u }, { WR_SPI_READ, 0, id, 3u } };
    int32_t flash = wr_spi_open_cstr("spi0", cs, WR_SPI_MODE_0, 0u);
    int32_t result;

    if (flash <= 0)
        return -1000 + flash;
    result = wr_spi_transaction(flash, ops, 2u);
    if (result != 0)
        return result;
    return (id[0] << 16) | (id[1] << 8) | id[2];
}

/* Over a port that works in the background. */
int32_t app_main_flash(void)
{
    return read_id("cs-a");
}

/* A second flash on the same bus. */
int32_t app_main_flash_b(void)
{
    return read_id("cs-b");
}

/* Works while another program waits for the bus. */
int32_t app_main_spin(void)
{
    volatile uint32_t count = 0;
    for (;;)
        ++count;
}

int32_t app_main_sensor(void)
{
    uint8_t reg = 0;
    uint8_t value[2] = { 0, 0 };
    int32_t sensor = wr_i2c_open_cstr("i2c0", 0x48u);
    int32_t result;

    if (sensor <= 0)
        return -1000 + sensor;
    result = wr_i2c_write_read(sensor, &reg, 1u, value, 2u);
    return result != 0 ? result : (value[0] << 8) | value[1];
}
