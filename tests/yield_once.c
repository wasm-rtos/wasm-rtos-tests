__attribute__((import_module("env"), import_name("os_yield")))
void os_yield(void);

void app_main(void)
{
    volatile unsigned int before = 1U;
    os_yield();
    volatile unsigned int after = before + 1U;
    (void)after;
}
