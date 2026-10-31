__attribute__((import_module("env"), import_name("first_missing_import"))) void first_missing_import(void);
__attribute__((import_module("env"), import_name("second_missing_import"))) void second_missing_import(void);

static volatile int never = 0;

/* Kept out of app_main so the imports are only reached through a nested
 * call that is compiled lazily; the loader must still reject them. */
__attribute__((noinline)) static void use_missing_imports(void)
{
    first_missing_import();
    second_missing_import();
}

void app_main(void)
{
    if (never)
    {
        use_missing_imports();
    }
}
