#include <wasm-rtos.h>

int32_t app_main_try(void)
{
    return wr_i2c_open_cstr("i2c0", 0x48u);
}

/* A temperature sensor: write the register address, read two bytes after a repeated start. */
int32_t app_main_sensor(void)
{
    uint8_t reg = 0x00;
    uint8_t value[2] = { 0, 0 };
    int32_t sensor = wr_i2c_open_cstr("i2c0", 0x48u);

    if (sensor <= 0)
        return -1;
    if (wr_i2c_write_read(sensor, &reg, 1u, value, 2u) != 0)
        return -2;
    /* Waits so the test can see a second program share the bus. */
    wr_sleep_ms(10u);
    return (value[0] << 8) | value[1];
}

/* A second program on the same bus, at another address. */
int32_t app_main_display(void)
{
    static const uint8_t command[2] = { 0x00, 0xAF };
    static const uint8_t data_head = 0x40;
    static const uint8_t pixels[4] = { 1, 2, 3, 4 };
    wr_i2c_op ops[2] = { { WR_I2C_WRITE, (void*)&data_head, 1u }, { WR_I2C_WRITE, (void*)pixels, 4u } };
    int32_t display = wr_i2c_open_cstr("i2c0", 0x3Cu);

    if (display <= 0)
        return 1;
    if (wr_i2c_open_cstr("i2c0", 0x48u) != -WR_EINUSE)
        return 2;
    if (wr_i2c_write(display, command, 2u) != 0)
        return 3;
    /* Two writes in a row are one write on the wire: a control byte, then data. */
    if (wr_i2c_transaction(display, ops, 2u) != 0)
        return 4;
    return 0;
}

/* Probe every address: only the address goes out. */
int32_t app_main_scan(void)
{
    int32_t found = 0;

    for (uint32_t address = 0x08u; address < 0x78u; ++address)
    {
        int32_t device = wr_i2c_open_cstr("i2c0", address);
        int32_t result;
        if (device <= 0)
            return -1000 - device;
        result = wr_i2c_transaction(device, 0, 0u);
        if (result == 0)
            found = found * 256 + (int32_t)address;
        else if (result != -WR_ENACK)
            return -2000 + result;
        wr_close(device);
    }
    return found;
}

int32_t app_main_errors(void)
{
    uint8_t byte = 0;
    wr_i2c_op ops[17];
    int32_t quiet = wr_i2c_open_cstr("i2c0", 0x50u);
    int32_t broken = wr_i2c_open_cstr("i2c0", 0x51u);
    int32_t slow = wr_i2c_open_cstr("i2c0", 0x52u);
    int32_t sensor = wr_i2c_open_cstr("i2c0", 0x48u);

    if (quiet <= 0 || broken <= 0 || slow <= 0 || sensor <= 0)
        return 1;
    if (wr_i2c_open_cstr("nope", 0x48u) != -WR_ENOTFOUND || wr_i2c_open_cstr("i2c0", 0x80u) != -WR_EINVAL ||
        wr_i2c_open("i2c0", 4u, 0x49u, 1u) != -WR_EINVAL)
        return 2;
    if (wr_i2c_read(quiet, &byte, 1u) != -WR_ENACK)
        return 3;
    if (wr_i2c_read(broken, &byte, 1u) != -WR_EBUS || wr_i2c_read(slow, &byte, 1u) != -WR_ETIMEOUT)
        return 4;
    if (wr_i2c_read(sensor, (void*)0xFFFFFFF0u, 4u) != -WR_EFAULT)
        return 5;
    for (int i = 0; i < 17; ++i)
    {
        ops[i].kind = WR_I2C_READ;
        ops[i].data = &byte;
        ops[i].length = 1u;
    }
    if (wr_i2c_transaction(sensor, ops, 17u) != -WR_ELIMIT)
        return 6;
    ops[0].kind = 7u;
    if (wr_i2c_transaction(sensor, ops, 1u) != -WR_EINVAL)
        return 7;
    if (wr_wait_any(&sensor, 1u, 0u) != -WR_EBADH)
        return 8;
    if (wr_close(sensor) != 0 || wr_i2c_read(sensor, &byte, 1u) != -WR_EBADH)
        return 9;
    return 0;
}
