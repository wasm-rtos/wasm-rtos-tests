__attribute__((import_module("env"), import_name("os_get_time_ms"))) extern unsigned int os_get_time_ms(void);

int app_main(void)
{
    return os_get_time_ms() == 1234U ? 0 : 1;
}
