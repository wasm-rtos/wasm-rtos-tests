#define PROC_IMPORT(name) __attribute__((import_module("wasm-rtos:proc"), import_name(name)))
PROC_IMPORT("spawn") int proc_spawn(const void* bytes, unsigned length, const char* const* argv, unsigned argc, unsigned priority);
PROC_IMPORT("wait") unsigned proc_wait(int pid, unsigned* exit_code, unsigned timeout_ms);
PROC_IMPORT("kill") unsigned proc_kill(int pid);

#define OS_STATUS_OK 0U
#define OS_STATUS_TASK_NOT_FOUND 6U
#define OS_STATUS_TIMEOUT 14U
#define OS_STATUS_ABORTED 22U
#define OS_WAIT_FOREVER 0xFFFFFFFFU

/* (module (func (export "app_main") (loop br 0))) - runs until killed */
static const unsigned char endless_child[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x04, 0x01, 0x60, 0x00, 0x00,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0c, 0x01, 0x08, 'a', 'p', 'p', '_', 'm', 'a', 'i', 'n', 0x00, 0x00,
    0x0a, 0x09, 0x01, 0x07, 0x00, 0x03, 0x40, 0x0c, 0x00, 0x0b, 0x0b
};

int app_main(void)
{
    int failures = 0;
    unsigned code = 99U;
    int victim = proc_spawn(endless_child, sizeof(endless_child), 0, 0U, 0U);

    if (victim <= 0)
        failures |= 1;
    /* Still running: a zero timeout reports TIMEOUT instead of blocking. */
    if (proc_wait(victim, &code, 0U) != OS_STATUS_TIMEOUT)
        failures |= 2;
    if (proc_kill(victim) != OS_STATUS_OK)
        failures |= 4;
    if (proc_wait(victim, &code, OS_WAIT_FOREVER) != OS_STATUS_ABORTED)
        failures |= 8;
    if (proc_kill(12345) != OS_STATUS_TASK_NOT_FOUND)
        failures |= 16;

    /* Left running when this task returns; it becomes an orphan. */
    if (proc_spawn(endless_child, sizeof(endless_child), 0, 0U, 0U) <= 0)
        failures |= 32;

    return failures;
}

