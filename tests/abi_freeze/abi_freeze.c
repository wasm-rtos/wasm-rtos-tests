#include <wasm-rtos.h>

/*
 * ABI 1.x frozen at 1.8 (the kernel's docs/decisions.md, 8). app_main
 * returns, in the low byte, what an unanswered WASI file call returned, and
 * above it a bit for every rule that did not hold.
 */

/* A call this kernel has, named by a newer minor version. */
__attribute__((import_module("wasm-rtos:kernel/proc@1.9.0"), import_name("self")))
int32_t self_1_9(void);
/* The same, named by the first minor version. */
__attribute__((import_module("wasm-rtos:kernel/proc@1.0.0"), import_name("self")))
int32_t self_1_0(void);
/* A call 1.8 does not have, in an interface it has. */
__attribute__((import_module("wasm-rtos:kernel/sync@1.9.0"), import_name("mutex-try-lock")))
int32_t mutex_try_lock_1_9(int32_t mutex);
/* A call 1.8 does not have, in a driver's interface. */
__attribute__((import_module("wasm-rtos:kernel/gpio@1.9.0"), import_name("toggle")))
int32_t gpio_toggle_1_9(int32_t pin);
/* An interface 1.8 does not have. */
__attribute__((import_module("wasm-rtos:kernel/camera@1.9.0"), import_name("open")))
int32_t camera_open_1_9(const char* name, uint32_t name_length, uint32_t flags);
/* A WASI call the kernel never answers. */
__attribute__((import_module("wasi_snapshot_preview1"), import_name("sock_accept")))
int32_t sock_accept(int32_t fd, int32_t flags, int32_t* out_fd);
/* A WASI file call: answered with files, ENOSYS without. */
__attribute__((import_module("wasi_snapshot_preview1"), import_name("path_open")))
int32_t path_open(int32_t fd, int32_t lookup_flags, const char* path, uint32_t path_length, int32_t open_flags,
                  int64_t rights, int64_t inheriting, int32_t fd_flags, int32_t* out_fd);

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
    int32_t failures = 0;
    int32_t fd = 0;
    int32_t self = wr_self();
    int32_t child = 0;

    if (self <= 0 || self_1_9() != self || self_1_0() != self)
        failures |= 1;
    if (mutex_try_lock_1_9(0) != -WR_EUNSUPPORTED)
        failures |= 2;
    if (gpio_toggle_1_9(0) != -WR_EUNSUPPORTED)
        failures |= 4;
    if (camera_open_1_9("cam", 3U, 0U) != -WR_EUNSUPPORTED)
        failures |= 8;
    if (sock_accept(0, 0, &fd) != 52)
        failures |= 16;
    /* Asked above the parent's priority: runs at the parent's (the host checks). */
    child = wr_spawn(endless_child, sizeof(endless_child), 0, 0U, 20U, 0U);
    if (child <= 0 && child != -WR_EUNSUPPORTED)
        failures |= 32;
    return (failures << 8) | (path_open(99, 0, "x", 1U, 0, 0, 0, 0, &fd) & 0xff);
}

/* Imports every call of ABI 1.8, so the program loads only if each links. Not run. */
int64_t app_main_all_calls(void)
{
    int64_t sum = 0;

    sum += (int64_t)wr_close(0);
    sum += (int64_t)wr_mutex_create(0, 0);
    sum += (int64_t)wr_mutex_lock(0, 0);
    sum += (int64_t)wr_mutex_unlock(0);
    sum += (int64_t)wr_semaphore_create(0, 0, 0, 0);
    sum += (int64_t)wr_semaphore_take(0, 0);
    sum += (int64_t)wr_semaphore_give(0);
    sum += (int64_t)wr_event_create(0, 0);
    sum += (int64_t)wr_event_set(0, 0);
    sum += (int64_t)wr_event_clear(0, 0);
    sum += (int64_t)wr_event_get(0);
    sum += (int64_t)wr_event_wait(0, 0, 0, 0);
    sum += (int64_t)wr_queue_create(0, 0, 0, 0);
    sum += (int64_t)wr_queue_send(0, 0, 0);
    sum += (int64_t)wr_queue_receive(0, 0, 0);
    sum += (int64_t)wr_timer_create(0, 0);
    sum += (int64_t)wr_timer_start(0);
    sum += (int64_t)wr_timer_stop(0);
    sum += (int64_t)wr_timer_set_period(0, 0);
    sum += (int64_t)wr_timer_read(0);
    sum += (int64_t)wr_wait_any(0, 0, 0);
    sum += (int64_t)wr_spawn(0, 0, 0, 0, 0, 0);
    sum += (int64_t)wr_spawn_path(0, 0, 0, 0, 0, 0);
    sum += (int64_t)wr_wait(0, 0);
    sum += (int64_t)wr_kill(0);
    sum += (int64_t)wr_self();
    sum += (int64_t)wr_parent();
    sum += (int64_t)wr_gpio_open(0, 0, 0);
    sum += (int64_t)wr_gpio_set(0, 0);
    sum += (int64_t)wr_gpio_get(0);
    sum += (int64_t)wr_gpio_set_mode(0, 0);
    sum += (int64_t)wr_gpio_watch(0, 0);
    sum += (int64_t)wr_gpio_events(0);
    sum += (int64_t)wr_display_open(0, 0, 0);
    sum += (int64_t)wr_display_get(0, 0);
    sum += (int64_t)wr_display_write(0, 0, 0, 0, 0, 0, 0, 0);
    sum += (int64_t)wr_display_fill(0, 0, 0, 0, 0, 0, 0);
    sum += (int64_t)wr_display_flush(0);
    sum += (int64_t)wr_display_set_brightness(0, 0);
    sum += (int64_t)wr_input_open(0, 0, 0);
    sum += (int64_t)wr_input_get(0, 0);
    sum += (int64_t)wr_input_read(0, 0, 0);
    sum += (int64_t)wr_i2c_open(0, 0, 0, 0);
    sum += (int64_t)wr_i2c_transaction(0, 0, 0);
    sum += (int64_t)wr_spi_open(0, 0, 0, 0, 0, 0);
    sum += (int64_t)wr_spi_transaction(0, 0, 0);
    sum += (int64_t)wr_adc_open(0, 0, 0);
    sum += (int64_t)wr_adc_read(0);
    sum += (int64_t)wr_adc_read_mv(0);
    sum += (int64_t)wr_adc_get(0, 0);
    sum += (int64_t)wr_pwm_open(0, 0, 0, 0);
    sum += (int64_t)wr_pwm_set_duty(0, 0);
    sum += (int64_t)wr_pwm_set_frequency(0, 0);
    sum += (int64_t)wr_pwm_get(0, 0);
    sum += (int64_t)wr_uart_open(0, 0, 0, 0);
    sum += (int64_t)wr_uart_read(0, 0, 0);
    sum += (int64_t)wr_uart_write(0, 0, 0);
    sum += (int64_t)wr_uart_wait_writable(0, 0);
    sum += (int64_t)wr_uart_get(0, 0);
    sum += (int64_t)wr_pulse_open(0, 0, 0);
    sum += (int64_t)wr_pulse_send(0, 0, 0, 0);
    sum += (int64_t)wr_pulse_send_bits(0, 0, 0, 0, 0, 0, 0);
    sum += (int64_t)wr_pulse_receive(0, 0, 0, 0, 0);
    sum += (int64_t)wr_pulse_get(0, 0);
    sum += (int64_t)wr_onewire_open(0, 0, 0, 0);
    sum += (int64_t)wr_onewire_transaction(0, 0, 0);
    sum += (int64_t)wr_onewire_search(0, 0, 0, 0);
    sum += (int64_t)wr_counter_open(0, 0, 0, 0);
    sum += (int64_t)wr_counter_read(0, 0);
    sum += (int64_t)wr_counter_set(0, 0);
    sum += (int64_t)wr_counter_watch(0, 0, 0);
    sum += (int64_t)wr_counter_get(0, 0);
    sum += (int64_t)wr_dac_open(0, 0, 0);
    sum += (int64_t)wr_dac_write(0, 0);
    sum += (int64_t)wr_dac_write_mv(0, 0);
    sum += (int64_t)wr_dac_get(0, 0);
    return sum;
}
