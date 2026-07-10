__attribute__((import_module("env"), import_name("os_delay_ms"))) void os_delay_ms(unsigned int delay_ms);

int app_main(void)
{
    os_delay_ms(10);
    return 21;
}
