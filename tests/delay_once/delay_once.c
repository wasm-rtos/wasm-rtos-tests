__attribute__((import_module("env"), import_name("os_delay_ms")))
void os_delay_ms(unsigned int delay_ms);

void app_main(void)
{
    volatile unsigned int before = 1U;
    os_delay_ms(10U);
    volatile unsigned int after = before + 1U;
    (void)after;
}
