#include "os.h"
#include "hal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { uint8_t* data; uint32_t size; } Binary;
static FILE* log_file;
static int failures;
static void expect(int condition, const char* text) { printf("%s %s\n", condition ? "PASS" : "FAIL", text); if (log_file) { fprintf(log_file, "%s %s\n", condition ? "PASS" : "FAIL", text); fflush(log_file); } if (!condition) ++failures; }
static int load(Binary* binary) { FILE* file = fopen("task_id_api.wasm", "rb"); long size; if (!file) return 0; fseek(file, 0, SEEK_END); size = ftell(file); rewind(file); if (size <= 0 || (unsigned long)size > 0xFFFFFFFFUL) { fclose(file); return 0; } binary->data = malloc((size_t)size); binary->size = (uint32_t)size; if (!binary->data || fread(binary->data, 1, (size_t)size, file) != (size_t)size) { fclose(file); free(binary->data); return 0; } fclose(file); return 1; }
int main(void) { Binary binary = {0}; OsTaskHandle a = NULL, b = NULL; uint32_t a_id = 0, b_id = 0; log_file = fopen("task_id_api.log", "a"); hal_init(); expect(os_init() == OS_STATUS_OK, "initialize OS"); expect(load(&binary), "load task_id_api.wasm"); expect(os_task_get_id(NULL) == 0U, "NULL task id is zero"); if (binary.data) { expect(os_task_create(&a, binary.data, binary.size, "app_main", "task_id_a", 64U * 1024U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && a != NULL, "create first task"); expect(os_task_create(&b, binary.data, binary.size, "app_main", "task_id_b", 64U * 1024U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && b != NULL, "create second task"); if (a && b) { a_id = os_task_get_id(a); b_id = os_task_get_id(b); expect(a_id != 0U && b_id != 0U && a_id != b_id, "task ids are unique"); expect(os_task_get_id(a) == a_id && os_task_get_id(b) == b_id, "task ids are stable"); expect(os_task_find_by_id(a_id) == a && os_task_find_by_id(b_id) == b, "find tasks by id"); } if (a) expect(os_task_delete(a) == OS_STATUS_OK, "delete first task"); if (b) expect(os_task_delete(b) == OS_STATUS_OK, "delete second task"); if (a_id) expect(os_task_find_by_id(a_id) == NULL, "deleted first task id not found"); if (b_id) expect(os_task_find_by_id(b_id) == NULL, "deleted second task id not found"); free(binary.data); } expect(os_get_task_count() == 0U, "clean OS state"); os_shutdown(); if (log_file) fclose(log_file); return failures ? 1 : 0; }
