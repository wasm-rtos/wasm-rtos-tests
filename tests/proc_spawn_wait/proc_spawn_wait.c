#include <wasm-rtos.h>

/* (module (func (export "app_main") (result i32) i32.const 42)) */
static const unsigned char quick_child[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0c, 0x01, 0x08, 'a', 'p', 'p', '_', 'm', 'a', 'i', 'n', 0x00, 0x00,
    0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x2a, 0x0b
};

/* (module (import "wasi_snapshot_preview1" "sched_yield" (func (result i32)))
 *         (func (export "app_main") (result i32) (local i32)
 *           i32.const 200 local.set 0
 *           (loop call 0 drop local.get 0 i32.const 1 i32.sub local.tee 0 br_if 0)
 *           i32.const 7))
 * Yields 200 times, so it is still running when the parent waits. */
static const unsigned char slow_child[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,
    0x02, 0x26, 0x01,
    0x16, 'w', 'a', 's', 'i', '_', 's', 'n', 'a', 'p', 's', 'h', 'o', 't', '_', 'p', 'r', 'e', 'v', 'i', 'e', 'w', '1',
    0x0b, 's', 'c', 'h', 'e', 'd', '_', 'y', 'i', 'e', 'l', 'd', 0x00, 0x00,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0c, 0x01, 0x08, 'a', 'p', 'p', '_', 'm', 'a', 'i', 'n', 0x00, 0x01,
    0x0a, 0x1c, 0x01, 0x1a, 0x01, 0x01, 0x7f,
    0x41, 0xc8, 0x01, 0x21, 0x00,
    0x03, 0x40, 0x10, 0x00, 0x1a, 0x20, 0x00, 0x41, 0x01, 0x6b, 0x22, 0x00, 0x0d, 0x00, 0x0b,
    0x41, 0x07, 0x0b
};

/* Returns a bit mask of failed checks; 0 means every check passed. */
int app_main(void)
{
    int failures = 0;
    int32_t slow = 0;
    int32_t quick = 0;

    if (wr_self() <= 0)
        failures |= 1;
    if (wr_parent() != 0)
        failures |= 2;

    slow = wr_spawn(slow_child, sizeof(slow_child), 0, 0U, 0U, 0U);
    if (slow <= 0)
        failures |= 4;
    quick = wr_spawn(quick_child, sizeof(quick_child), 0, 0U, 0U, 0U);
    if (quick <= 0)
        failures |= 8;

    /* Blocks while the slow child runs; the quick child ends meanwhile. */
    if (wr_wait(slow, WR_WAIT_FOREVER) != 7)
        failures |= 16;

    /* Already ended: collected without blocking. */
    if (wr_wait(quick, WR_WAIT_FOREVER) != 42)
        failures |= 32;

    /* A collected child cannot be waited for again; the handle still closes. */
    if (wr_wait(quick, 0U) != -WR_EBADH || wr_close(quick) != 0 || wr_close(slow) != 0)
        failures |= 64;

    /* Bad modules and arguments are refused without a child. */
    if (wr_spawn("nope", 4U, 0, 0U, 0U, 0U) != -WR_EBADPROGRAM)
        failures |= 128;
    if (wr_spawn((const void*)0xFFFFFFF0U, 64U, 0, 0U, 0U, 0U) != -WR_EFAULT)
        failures |= 256;

    return failures;
}
