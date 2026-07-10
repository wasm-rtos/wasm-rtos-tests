#define TEST_NAME "simple_loop"
#define TEST_KIND TEST_FINITE
#define TEST_ENTRY "app_main"
#define TEST_EXPECTED_CODE 0U
#define TEST_EXPECTED_REASON OS_TASK_EXIT_RETURNED
#include "../support/test_runner.h"
int main(void) { return test_runner_main(); }
