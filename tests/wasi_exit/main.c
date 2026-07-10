#define TEST_NAME "wasi_exit"
#define TEST_KIND TEST_FINITE
#define TEST_ENTRY "_start"
#define TEST_EXPECTED_CODE 7U
#define TEST_EXPECTED_REASON OS_TASK_EXIT_EXPLICIT
#include "../support/test_runner.h"
int main(void) { return test_runner_main(); }
