#include <wasm-rtos.h>

/* (module (func (export "app_main") (loop br 0))) - runs until killed */
static const unsigned char endless_child[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x04, 0x01, 0x60, 0x00, 0x00,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0c, 0x01, 0x08, 'a', 'p', 'p', '_', 'm', 'a', 'i', 'n', 0x00, 0x00,
    0x0a, 0x09, 0x01, 0x07, 0x00, 0x03, 0x40, 0x0c, 0x00, 0x0b, 0x0b
};

/* (module (func (export "app_main") unreachable)) - traps at once */
static const unsigned char trapping_child[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x04, 0x01, 0x60, 0x00, 0x00,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0c, 0x01, 0x08, 'a', 'p', 'p', '_', 'm', 'a', 'i', 'n', 0x00, 0x00,
    0x0a, 0x05, 0x01, 0x03, 0x00, 0x00, 0x0b
};

int app_main(void)
{
    int failures = 0;
    int32_t victim = wr_spawn(endless_child, sizeof(endless_child), 0, 0U, 0U, 0U);
    int32_t trapper = wr_spawn(trapping_child, sizeof(trapping_child), 0, 0U, 0U, 0U);

    if (victim <= 0 || trapper <= 0)
        failures |= 1;
    /* Still running: a zero timeout reports a timeout instead of blocking. */
    if (wr_wait(victim, 0U) != -WR_ETIMEOUT)
        failures |= 2;
    if (wr_kill(victim) != 0)
        failures |= 4;
    if (wr_wait(victim, WR_WAIT_FOREVER) != -WR_EKILLED)
        failures |= 8;
    if (wr_kill(victim) != -WR_EBADH || wr_kill(12345) != -WR_EBADH)
        failures |= 16;
    if (wr_wait(trapper, WR_WAIT_FOREVER) != -WR_ETRAPPED)
        failures |= 32;

    /* Left running when this task returns; it becomes an orphan. */
    if (wr_spawn(endless_child, sizeof(endless_child), 0, 0U, 0U, 0U) <= 0)
        failures |= 64;

    return failures;
}
