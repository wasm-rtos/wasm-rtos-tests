__attribute__((import_module("env"), import_name("os_yield")))
void os_yield(void);

static int value = 0;

int app_main(void)
{
    value = 7;
    os_yield();
    value += 5;
    return value;
}
