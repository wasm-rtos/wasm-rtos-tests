#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Code stores (wasm-rtos decision 11): a program's code image is written
 * when no task is ready, mapped, found again when the program loads anew,
 * written again when the stored one is refused, and goes to the store
 * with the highest priority. The stores are RAM made to look like flash
 * mapped as data: sectors of 4 KiB, erased to 0xFF.
 */
static FILE* g_log;
static int g_failures;

static void expect(int condition, const char* message)
{
    fprintf(stdout, "%s %s\n", condition ? "PASS" : "FAIL", message);
    fprintf(g_log, "%s %s\n", condition ? "PASS" : "FAIL", message);
    fflush(g_log);
    if (!condition)
        ++g_failures;
}

typedef struct Flash
{
    uint32_t* words;
    uint32_t size;
} Flash;

static OsStatus flash_erase(void* context, uint32_t offset, uint32_t size)
{
    Flash* flash = (Flash*)context;
    if (offset % 4096U || size % 4096U || offset + size > flash->size)
        return OS_STATUS_INVALID_ARGUMENT;
    memset((uint8_t*)flash->words + offset, 0xFF, size);
    return OS_STATUS_OK;
}

/* Flash clears bits only: a write over bytes not erased is caught. */
static OsStatus flash_write(void* context, uint32_t offset, const void* data, uint32_t size)
{
    Flash* flash = (Flash*)context;
    uint8_t* to = (uint8_t*)flash->words + offset;
    const uint8_t* from = (const uint8_t*)data;
    if (offset + size > flash->size)
        return OS_STATUS_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < size; ++i)
    {
        if ((to[i] & from[i]) != from[i])
            return OS_STATUS_ERROR;
        to[i] &= from[i];
    }
    return OS_STATUS_OK;
}

static void flash_make(Flash* flash, OsCodeStore* store, uint32_t size)
{
    flash->words = (uint32_t*)malloc(size);
    flash->size = size;
    memset(flash->words, 0xFF, size);
    memset(store, 0, sizeof(*store));
    store->context = flash;
    store->base = (const uint8_t*)flash->words;
    store->size = size;
    store->erase_size = 4096U;
    store->erase = flash_erase;
    store->write = flash_write;
}

static uint8_t* read_file(const char* path, uint32_t* size)
{
    FILE* f = fopen(path, "rb");
    long n;
    uint8_t* bytes;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    bytes = (uint8_t*)malloc((size_t)n);
    if (fread(bytes, 1, (size_t)n, f) != (size_t)n)
    {
        free(bytes);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (uint32_t)n;
    return bytes;
}

/* A task of the program, run until it sleeps and the scheduler finds nothing ready (it writes an image then). */
static OsTaskHandle start(const uint8_t* bytes, uint32_t size)
{
    OsTaskHandle task = NULL;
    if (os_task_create(&task, bytes, size, "app_main", "code_store", 16384U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
    for (int i = 0; i < 200 && os_schedule() == OS_STATUS_OK; ++i)
        ;
    return task;
}

static void stop(OsTaskHandle task)
{
    if (task)
        os_task_delete(task);
    for (int i = 0; i < 4; ++i)
        (void)os_schedule();
}

int main(void)
{
    Flash flash, fast;
    OsCodeStore store, high;
    OsCodeStoreStats stats, high_stats;
    uint32_t size = 0;
    uint8_t* bytes;
    OsTaskHandle task;

    g_log = fopen("code_store.log", "w");
    if (!g_log)
        return 1;
    bytes = read_file("code_store.wasm", &size);
    expect(bytes != NULL, "read code_store.wasm");
    if (!bytes)
        return 1;
    flash_make(&flash, &store, 64U * 1024U);

    expect(os_init() == OS_STATUS_OK, "initialize OS");
    expect(os_code_store_add(&store, 1U) == OS_STATUS_OK, "add a code store");
    task = start(bytes, size);
    expect(task != NULL, "the program runs and sleeps");
    expect(os_code_store_get_stats(&store, &stats) == OS_STATUS_OK && stats.images == 1U && stats.mapped == 1U &&
               stats.used > 0U && stats.used % 4096U == 0U,
           "with nothing ready, its image is written and mapped");
    stop(task);
    task = start(bytes, size);
    expect(os_code_store_get_stats(&store, &stats) == OS_STATUS_OK && stats.images == 1U && stats.mapped == 2U,
           "loaded anew, the program maps the image it has");
    stop(task);
    os_shutdown();

    /* Restarted with the same flash: the image is still there. Then one byte of it changed. */
    expect(os_init() == OS_STATUS_OK, "initialize OS again");
    expect(os_code_store_add(&store, 1U) == OS_STATUS_OK, "the store comes back");
    expect(os_code_store_get_stats(&store, &stats) == OS_STATUS_OK && stats.images == 1U,
           "the image is found in it after a restart");
    task = start(bytes, size);
    expect(os_code_store_get_stats(&store, &stats) == OS_STATUS_OK && stats.mapped == 1U && stats.images == 1U,
           "and mapped: nothing predecoded to start");
    stop(task);
    ((uint8_t*)flash.words)[64] ^= 0x01U;
    task = start(bytes, size);
    expect(os_code_store_get_stats(&store, &stats) == OS_STATUS_OK && stats.images == 2U && stats.mapped == 2U,
           "an image changed is refused, and a new one written and mapped");
    stop(task);
    os_shutdown();

    /* Two empty stores: the image goes to the higher priority. */
    free(flash.words);
    flash_make(&flash, &store, 64U * 1024U);
    flash_make(&fast, &high, 64U * 1024U);
    expect(os_init() == OS_STATUS_OK, "initialize OS with two stores");
    expect(os_code_store_add(&store, 1U) == OS_STATUS_OK && os_code_store_add(&high, 5U) == OS_STATUS_OK,
           "add a store of priority 1 and one of priority 5");
    expect(os_code_store_add(&high, 2U) == OS_STATUS_INVALID_ARGUMENT, "a store is added once");
    task = start(bytes, size);
    expect(os_code_store_get_stats(&store, &stats) == OS_STATUS_OK && os_code_store_get_stats(&high, &high_stats) == OS_STATUS_OK &&
               high_stats.images == 1U && high_stats.mapped == 1U && stats.images == 0U,
           "the image goes to the store of priority 5");
    stop(task);
    os_shutdown();

    /* A store with no room left: erased when it is added, nothing being mapped then. */
    free(flash.words);
    flash_make(&flash, &store, 4096U);
    expect(os_init() == OS_STATUS_OK, "initialize OS with a store of one sector");
    expect(os_code_store_add(&store, 1U) == OS_STATUS_OK, "add it");
    task = start(bytes, size);
    expect(os_code_store_get_stats(&store, &stats) == OS_STATUS_OK && stats.images == 1U && stats.used == 4096U,
           "the image fills it");
    stop(task);
    os_shutdown();
    expect(os_init() == OS_STATUS_OK && os_code_store_add(&store, 1U) == OS_STATUS_OK &&
               os_code_store_get_stats(&store, &stats) == OS_STATUS_OK && stats.images == 0U && stats.used == 0U,
           "added again full, it is erased");
    os_shutdown();

    free(flash.words);
    free(fast.words);
    free(bytes);
    fprintf(g_log, "%s\n", g_failures ? "FAILED" : "ok");
    fclose(g_log);
    printf("%s\n", g_failures ? "FAILED" : "ok");
    return g_failures != 0;
}
