#define TEST_NAME "queue_api"
#define TEST_KIND TEST_QUEUE_API
#define TEST_ENTRY "app_main"
#define TEST_EXPECTED_CODE 0U
#define TEST_EXPECTED_REASON OS_TASK_EXIT_RETURNED
#include "../support/test_runner.h"
int main(void) { return test_runner_main(); }
