#define PROC_IMPORT(name) __attribute__((import_module("wasm-rtos:proc"), import_name(name)))
PROC_IMPORT("spawn") int proc_spawn(const void* bytes, unsigned length, const char* const* argv, unsigned argc, unsigned priority);
PROC_IMPORT("wait") unsigned proc_wait(int pid, unsigned* exit_code, unsigned timeout_ms);
PROC_IMPORT("self") unsigned proc_self(void);
PROC_IMPORT("parent") unsigned proc_parent(void);

#define OS_STATUS_OK 0U
#define OS_STATUS_TASK_NOT_FOUND 6U
#define OS_WAIT_FOREVER 0xFFFFFFFFU

/* (module (func (export "app_main") (result i32) i32.const 42)) */
static const unsigned char quick_child[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0c, 0x01, 0x08, 'a', 'p', 'p', '_', 'm', 'a', 'i', 'n', 0x00, 0x00,
    0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x2a, 0x0b
};

/* (module (import "env" "os_delay_ms" (func (param i32)))
 *         (func (export "app_main") (result i32) i32.const 100 call 0 i32.const 7)) */
static const unsigned char slow_child[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x09, 0x02, 0x60, 0x01, 0x7f, 0x00, 0x60, 0x00, 0x01, 0x7f,
    0x02, 0x13, 0x01, 0x03, 'e', 'n', 'v', 0x0b, 'o', 's', '_', 'd', 'e', 'l', 'a', 'y', '_', 'm', 's', 0x00, 0x00,
    0x03, 0x02, 0x01, 0x01,
    0x07, 0x0c, 0x01, 0x08, 'a', 'p', 'p', '_', 'm', 'a', 'i', 'n', 0x00, 0x01,
    0x0a, 0x0b, 0x01, 0x09, 0x00, 0x41, 0xe4, 0x00, 0x10, 0x00, 0x41, 0x07, 0x0b
};

/* Returns a bit mask of failed checks; 0 means every check passed. */
int app_main(void)
{
    int failures = 0;
    unsigned code = 0U;
    int slow = 0;
    int quick = 0;

    if (proc_self() == 0U)
        failures |= 1;
    if (proc_parent() != 0U)
        failures |= 2;

    slow = proc_spawn(slow_child, sizeof(slow_child), 0, 0U, 0U);
    if (slow <= 0)
        failures |= 4;
    quick = proc_spawn(quick_child, sizeof(quick_child), 0, 0U, 0U);
    if (quick <= 0)
        failures |= 8;

    /* Blocks while the slow child sleeps; the quick child dies meanwhile. */
    if (proc_wait(slow, &code, OS_WAIT_FOREVER) != OS_STATUS_OK || code != 7U)
        failures |= 16;

    /* Already dead: collected without blocking. */
    code = 0U;
    if (proc_wait(quick, &code, OS_WAIT_FOREVER) != OS_STATUS_OK || code != 42U)
        failures |= 32;

    /* A collected child cannot be waited for again. */
    if (proc_wait(quick, &code, 0U) != OS_STATUS_TASK_NOT_FOUND)
        failures |= 64;

    return failures;
}
