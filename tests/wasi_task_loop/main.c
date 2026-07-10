#define TEST_NAME "wasi_task_loop"
#define TEST_KIND TEST_LONG_RUNNING
#define TEST_ENTRY "_start"
#include "../support/test_runner.h"
int main(void) { return test_runner_main(); }
