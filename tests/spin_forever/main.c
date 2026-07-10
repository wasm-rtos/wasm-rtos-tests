#define TEST_NAME "spin_forever"
#define TEST_KIND TEST_LONG_RUNNING
#define TEST_ENTRY "app_main"
#include "../support/test_runner.h"
int main(void) { return test_runner_main(); }
