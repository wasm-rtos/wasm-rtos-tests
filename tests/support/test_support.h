#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include <stdint.h>
#include <stdio.h>
#include "os.h"

typedef struct TestBinary
{
    uint8_t* bytes;
    uint32_t size;
} TestBinary;

typedef struct TestContext
{
    const char* name;
    FILE* log;
    int failures;
} TestContext;

int test_begin(TestContext* context, const char* name, const char* log_path);
int test_finish(TestContext* context);
void test_log(TestContext* context, const char* format, ...);
int test_expect(TestContext* context, int condition, const char* message);
int test_load_binary(TestContext* context, const char* path, TestBinary* binary);
void test_free_binary(TestBinary* binary);
int test_schedule_until_dead(TestContext* context, OsTaskHandle task, uint32_t max_iterations);
int test_expect_clean_os(TestContext* context);

#endif
