__attribute__((import_module("math"), import_name("add"))) extern int
math_add(int left, int right);

__attribute__((import_module("math"), import_name("wide_add"))) extern long long
math_wide_add(long long left, long long right);

__attribute__((import_module("math"), import_name("float_add"))) extern float
math_float_add(float left, float right);

__attribute__((import_module("math"), import_name("double_add"))) extern double
math_double_add(double left, double right);

__attribute__((import_module("scale"), import_name("mul"))) extern int
scale_mul(int left, int right);

__attribute__((import_module("env"), import_name("host_phase"))) extern void
host_phase(unsigned int phase);

__attribute__((import_module("env"), import_name("os_yield"))) extern void
os_yield(void);

__attribute__((import_module("startup"), import_name("check"))) extern int
startup_check(void);

__attribute__((import_module("mutable"), import_name("add"))) extern int
mutable_add(int left, int right);

int app_main(void)
{
    if (math_add(20, 22) != 42)
    {
        return 1;
    }

    if (math_wide_add(5000000000LL, 7LL) != 5000000007LL)
    {
        return 2;
    }

    if (math_float_add(1.25f, 2.5f) != 3.75f)
    {
        return 3;
    }

    if (math_double_add(10.5, 0.25) != 10.75)
    {
        return 4;
    }

    host_phase(1U);
    os_yield();

    if (scale_mul(6, 7) != 42)
    {
        return 5;
    }

    host_phase(2U);
    os_yield();
    return 0;
}

int app_main_start(void)
{
    if (startup_check() != 1)
    {
        return 1;
    }

    host_phase(3U);
    os_yield();
    return 0;
}

int app_main_changed(void)
{
    return mutable_add(1, 2);
}
