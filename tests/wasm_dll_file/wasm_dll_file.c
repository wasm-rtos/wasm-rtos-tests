__attribute__((import_module("math"), import_name("add"))) extern int
math_add(int left, int right);

__attribute__((import_module("env"), import_name("host_phase"))) extern void
host_phase(unsigned int phase);

__attribute__((import_module("env"), import_name("os_yield"))) extern void
os_yield(void);

int app_main(void)
{
    if (math_add(20, 22) != 42)
    {
        return 1;
    }

    host_phase(1U);
    os_yield();

    if (math_add(19, 23) != 42)
    {
        return 2;
    }

    host_phase(2U);
    os_yield();
    return 0;
}
