__attribute__((import_module("env"), import_name("host_test_value")))
extern unsigned int host_test_value(unsigned int input);

int app_main(void)
{
    return host_test_value(37U) == 79U ? 0 : 1;
}
