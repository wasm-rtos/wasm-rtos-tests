#include "wasm3/source/m3_env.h"
#include "wasm3/source/m3_m3c.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    M3C_BASE_OFFSET = 37,
    M3C_HEADER_FUNCTION_COUNT_OFFSET = 28,
    M3C_HEADER_TABLE_OFFSET = 40,
    M3C_FUNCTION_DESC_SIZE = 64,
    M3C_FUNCTION_FLAGS_OFFSET = 0,
    M3C_FUNCTION_CODE_OFFSET = 32
};

typedef struct WasmBinary
{
    uint8_t * bytes;
    uint32_t size;
} WasmBinary;

typedef struct MemoryStorage
{
    uint8_t * bytes;
    size_t size;
    size_t capacity;
    uint32_t readCalls;
    uint32_t writeCalls;
} MemoryStorage;

static FILE * g_log;
static int g_failures;

static void log_message(const char * format, ...)
{
    va_list args;

    va_start(args, format);
    vprintf(format, args);
    printf("\n");
    va_end(args);

    va_start(args, format);
    vfprintf(g_log, format, args);
    fprintf(g_log, "\n");
    fflush(g_log);
    va_end(args);
}

static void expect(int condition, const char * message)
{
    log_message("%s %s", condition ? "PASS" : "FAIL", message);
    if (!condition)
        ++g_failures;
}

static int load_wasm(const char * path, WasmBinary * binary)
{
    FILE * file = fopen(path, "rb");
    long size;

    binary->bytes = NULL;
    binary->size = 0;
    if (!file || fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0
        || (unsigned long) size > UINT32_MAX || fseek(file, 0, SEEK_SET) != 0)
    {
        if (file)
            fclose(file);
        return 0;
    }

    binary->bytes = (uint8_t *) malloc((size_t) size);
    if (!binary->bytes
        || fread(binary->bytes, 1, (size_t) size, file) != (size_t) size)
    {
        fclose(file);
        free(binary->bytes);
        binary->bytes = NULL;
        return 0;
    }

    fclose(file);
    binary->size = (uint32_t) size;
    return 1;
}

static M3Result storage_read_at(void * context, uint64_t offset,
                                void * data, uint32_t size)
{
    MemoryStorage * storage = (MemoryStorage *) context;
    ++storage->readCalls;
    if (offset > storage->size || size > storage->size - (size_t) offset)
        return "test storage read overflow";
    memcpy(data, storage->bytes + (size_t) offset, size);
    return m3Err_none;
}

static M3Result storage_write_at(void * context, uint64_t offset,
                                 const void * data, uint32_t size)
{
    MemoryStorage * storage = (MemoryStorage *) context;
    size_t end;
    ++storage->writeCalls;

    if (offset > SIZE_MAX || size > SIZE_MAX - (size_t) offset)
        return "test storage write overflow";
    end = (size_t) offset + size;
    if (end > storage->capacity)
    {
        size_t capacity = storage->capacity ? storage->capacity : 256;
        uint8_t * bytes;
        while (capacity < end)
            capacity *= 2;
        bytes = (uint8_t *) realloc(storage->bytes, capacity);
        if (!bytes)
            return m3Err_mallocFailed;
        storage->bytes = bytes;
        storage->capacity = capacity;
    }

    memcpy(storage->bytes + (size_t) offset, data, size);
    if (end > storage->size)
        storage->size = end;
    return m3Err_none;
}

static uint64_t read_u64_le(const uint8_t * bytes)
{
    uint64_t value = 0;
    uint32_t i;
    for (i = 0; i < 8; ++i)
        value |= (uint64_t) bytes[i] << (i * 8U);
    return value;
}

static uint32_t read_u32_le(const uint8_t * bytes)
{
    return (uint32_t) bytes[0]
           | ((uint32_t) bytes[1] << 8)
           | ((uint32_t) bytes[2] << 16)
           | ((uint32_t) bytes[3] << 24);
}

static m3ApiRawFunction(host_test_value)
{
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, input);
    (void) runtime;
    (void) _ctx;
    (void) _mem;
    m3ApiReturn(input + 42U);
}

static M3Result link_host_import(IM3Module module)
{
    return m3_LinkRawFunction(module, "env", "host_test_value", "i(i)",
                              host_test_value);
}

static void run_cached_module(IM3Environment environment,
                              M3CStorage * storage,
                              uint64_t image_size)
{
    IM3Module module = NULL;
    IM3Runtime runtime = NULL;
    IM3Function function = NULL;
    M3Result result;
    uint32_t readsBeforeFunction;
    uint32_t snapshotSize = 0;
    uint8_t * snapshot = NULL;
    uint32_t returnValue = 1;
    uint32_t attempts;

    result = m3_ParseM3C(environment, &module, storage, M3C_BASE_OFFSET);
    expect(result == m3Err_none && module != NULL, "parse standalone .m3c image");
    if (result || !module)
        return;

    runtime = m3_NewRuntime(environment, 64U * 1024U, NULL);
    expect(runtime != NULL, "create runtime for cached module");
    if (!runtime)
    {
        m3_FreeModule(module);
        return;
    }

    result = m3_LoadModule(runtime, module);
    expect(result == m3Err_none, "load cached module and release source Wasm copy");
    if (result)
    {
        m3_FreeModule(module);
        m3_FreeRuntime(runtime);
        return;
    }

    result = link_host_import(module);
    expect(result == m3Err_none, "link host import without persisting callback pointers");

    readsBeforeFunction = ((MemoryStorage *) storage->context)->readCalls;
    result = m3_FindFunction(&function, runtime, "app_main");
    expect(result == m3Err_none && function != NULL, "load cached function on first lookup");
    expect(((MemoryStorage *) storage->context)->readCalls > readsBeforeFunction,
           "function metacode is read lazily from storage");
    if (result || !function)
    {
        m3_FreeRuntime(runtime);
        return;
    }

    m3_SetFuel(runtime, 1);
    result = m3_CallV(function);
    expect(result == m3Err_fuelExhausted && m3_IsSuspended(runtime),
           "cached function suspends when fuel is exhausted");

    result = m3_GetRuntimeSnapshotSize(runtime, &snapshotSize);
    expect(result == m3Err_none && snapshotSize != 0,
           "query snapshot size for cached function");
    snapshot = (uint8_t *) malloc(snapshotSize);
    expect(snapshot != NULL, "allocate snapshot buffer");
    if (snapshot)
    {
        result = m3_SaveRuntimeSnapshot(runtime, snapshot, snapshotSize,
                                        &snapshotSize);
        expect(result == m3Err_none, "save cached function snapshot");
        if (!result)
        {
            result = m3_LoadRuntimeSnapshot(runtime, snapshot, snapshotSize);
            expect(result == m3Err_none, "restore cached function snapshot");
        }
    }

    result = m3Err_fuelExhausted;
    for (attempts = 0; attempts < 1000 && result == m3Err_fuelExhausted; ++attempts)
    {
        m3_AddFuel(runtime, 1);
        result = m3_Resume(runtime);
    }
    expect(result == m3Err_none, "resume cached function to completion");
    if (!result)
    {
        result = m3_GetResultsV(function, &returnValue);
        expect(result == m3Err_none && returnValue == 0,
               "cached function calls host import and returns expected value");
    }

    expect(image_size > 0, "writer reports non-empty image size");
    free(snapshot);
    m3_FreeRuntime(runtime);
}

static void reject_corrupted_function(MemoryStorage * memory,
                                      M3CStorage * storage)
{
    uint64_t codeOffset;
    IM3Environment environment;
    IM3Module module = NULL;
    IM3Runtime runtime = NULL;
    IM3Function function = NULL;
    M3Result result;
    uint64_t tableOffset;
    uint32_t functionCount;
    uint32_t functionIndex;

    functionCount = read_u32_le(memory->bytes + M3C_BASE_OFFSET
                                + M3C_HEADER_FUNCTION_COUNT_OFFSET);
    tableOffset = read_u64_le(memory->bytes + M3C_BASE_OFFSET
                              + M3C_HEADER_TABLE_OFFSET);
    for (functionIndex = 0; functionIndex < functionCount; ++functionIndex)
    {
        const uint8_t * desc = memory->bytes + M3C_BASE_OFFSET
                             + (size_t) tableOffset
                             + (size_t) functionIndex * M3C_FUNCTION_DESC_SIZE;
        if (read_u32_le(desc + M3C_FUNCTION_FLAGS_OFFSET) & 1U)
        {
            codeOffset = read_u64_le(desc + M3C_FUNCTION_CODE_OFFSET);
            break;
        }
    }
    if (functionIndex == functionCount)
    {
        expect(0, "locate cached function for corruption test");
        return;
    }
    if (codeOffset >= memory->size - M3C_BASE_OFFSET)
    {
        expect(0, "locate cached function for corruption test");
        return;
    }
    memory->bytes[M3C_BASE_OFFSET + (size_t) codeOffset] ^= 0x01U;

    environment = m3_NewEnvironment();
    result = m3_ParseM3C(environment, &module, storage, M3C_BASE_OFFSET);
    expect(result == m3Err_none && module != NULL,
           "parse image before lazy function integrity check");
    if (!result && module)
    {
        runtime = m3_NewRuntime(environment, 64U * 1024U, NULL);
        result = runtime ? m3_LoadModule(runtime, module) : m3Err_mallocFailed;
        if (!result)
            result = link_host_import(module);
        if (!result)
            result = m3_FindFunction(&function, runtime, "app_main");
        expect(result == m3Err_m3cInvalid && function == NULL,
               "reject corrupted function before execution");
    }

    if (runtime)
        m3_FreeRuntime(runtime);
    else if (module)
        m3_FreeModule(module);
    m3_FreeEnvironment(environment);
}

int main(int argc, char ** argv)
{
    const char * wasmPath = argc > 1 ? argv[1] : "m3c_roundtrip.wasm";
    WasmBinary wasm = {0};
    MemoryStorage memory = {0};
    M3CStorage storage;
    IM3Environment environment;
    IM3Module sourceModule = NULL;
    M3Result result;
    uint64_t imageSize = 0;
    uint32_t i;

    g_log = fopen("m3c_roundtrip.log", "w");
    if (!g_log)
        return 1;

    expect(load_wasm(wasmPath, &wasm), "load source Wasm module");
    environment = m3_NewEnvironment();
    expect(environment != NULL, "create compiler environment");

    memory.capacity = 256;
    memory.bytes = (uint8_t *) malloc(memory.capacity);
    expect(memory.bytes != NULL, "allocate storage buffer");
    if (memory.bytes)
    {
        memset(memory.bytes, 0xA5, M3C_BASE_OFFSET);
        memory.size = M3C_BASE_OFFSET;
    }

    storage.context = &memory;
    storage.readAt = storage_read_at;
    storage.writeAt = storage_write_at;
    storage.sync = NULL;

    if (wasm.bytes && environment && memory.bytes)
    {
        result = m3_ParseModule(environment, &sourceModule, wasm.bytes, wasm.size);
        expect(result == m3Err_none && sourceModule != NULL, "parse source Wasm module");
        if (!result && sourceModule)
        {
            result = m3_WriteM3C(sourceModule, &storage, M3C_BASE_OFFSET,
                                 &imageSize);
            expect(result == m3Err_none, "compile and write relocatable .m3c image");
            expect(memory.writeCalls > 0, "writer uses generic storage callbacks");
            for (i = 0; i < M3C_BASE_OFFSET; ++i)
                if (memory.bytes[i] != 0xA5)
                    break;
            expect(i == M3C_BASE_OFFSET, "writer respects non-zero storage offset");
            m3_FreeModule(sourceModule);
            sourceModule = NULL;
        }

        free(wasm.bytes);
        wasm.bytes = NULL;

        if (!result)
        {
            run_cached_module(environment, &storage, imageSize);
            reject_corrupted_function(&memory, &storage);
        }
    }

    if (sourceModule)
        m3_FreeModule(sourceModule);
    if (environment)
        m3_FreeEnvironment(environment);
    free(wasm.bytes);
    free(memory.bytes);

    log_message("%s m3c_roundtrip", g_failures == 0 ? "PASS" : "FAIL");
    fclose(g_log);
    return g_failures == 0 ? 0 : 1;
}
