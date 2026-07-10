__attribute__((import_module("env"), import_name("missing_required_import")))
void missing_required_import(void);

void app_main(void)
{
    missing_required_import();
}
