#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#define TEST_FINITE 1
#define TEST_YIELD 2
#define TEST_DELAY 3
#define TEST_BAD_IMPORT 4
#define TEST_LONG_RUNNING 5
#define TEST_GET_TIME 6
#define TEST_RETURN_VALUES 7
#define TEST_ENTRY_ARGS 8
#define TEST_SNAPSHOT_YIELD 9
#define TEST_SNAPSHOT_DELAY 10
#define TEST_CUSTOM_IMPORT 11
#define TEST_QUEUE_API 12

int test_runner_main(void);

#endif
