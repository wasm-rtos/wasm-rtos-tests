#include "os.h"
#include "fs.h"
#include "os_kernel.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Processes of one program share it (docs/decisions.md 9 of the kernel):
 * one program for processes of the same contents, by path or by bytes; a
 * changed file is a new program while the old one runs on; the module
 * goes while no process has an instance and comes back; a restored
 * system shares again; the program goes with its last process.
 */

typedef struct
{
    uint8_t* d;
    uint32_t n;
} Bin;

static FILE* l;
static int f;

static void ok(int c, const char* s)
{
    printf("%s %s\n", c ? "PASS" : "FAIL", s);
    if (l)
    {
        fprintf(l, "%s %s\n", c ? "PASS" : "FAIL", s);
        fflush(l);
    }
    if (!c)
        f++;
}

static int load(Bin* b)
{
    FILE* x = fopen("program_share.wasm", "rb");
    long n;
    if (!x)
        return 0;
    fseek(x, 0, SEEK_END);
    n = ftell(x);
    rewind(x);
    if (n <= 0)
        return fclose(x), 0;
    b->d = malloc((size_t)n);
    b->n = (uint32_t)n;
    if (!b->d || fread(b->d, 1, (size_t)n, x) != (size_t)n)
    {
        fclose(x);
        free(b->d);
        b->d = NULL;
        return 0;
    }
    fclose(x);
    return 1;
}

/* ---- A file system of a few files in host memory that can change; it copies, never maps. */

typedef struct
{
    const char* name;
    const uint8_t* bytes;
    uint32_t size;
} File;

static File g_files[3];
static uint32_t g_reads; /* bytes the kernel read */

static File* file_named(const char* path)
{
    for (int i = 0; i < 3; i++)
        if (g_files[i].name && strcmp(g_files[i].name, path) == 0)
            return &g_files[i];
    return NULL;
}

static OsStatus ram_stat(void* context, const char* path, OsFsStat* out)
{
    File* file = file_named(path);
    (void)context;
    if (path[0] == '\0')
    {
        out->type = OS_FS_TYPE_DIRECTORY;
        out->size = 0;
        return OS_STATUS_OK;
    }
    if (!file)
        return OS_STATUS_FILE_NOT_FOUND;
    out->type = OS_FS_TYPE_FILE;
    out->size = file->size;
    return OS_STATUS_OK;
}

static OsStatus ram_open(void* context, const char* path, void** out)
{
    (void)context;
    *out = file_named(path);
    return *out ? OS_STATUS_OK : OS_STATUS_FILE_NOT_FOUND;
}

static OsStatus ram_read(void* context, void* handle, uint64_t offset, void* buffer, uint32_t length, uint32_t* out_read)
{
    File* file = (File*)handle;
    (void)context;
    if (offset >= file->size)
    {
        *out_read = 0;
        return OS_STATUS_OK;
    }
    if (length > file->size - offset)
        length = file->size - (uint32_t)offset;
    memcpy(buffer, file->bytes + offset, length);
    *out_read = length;
    g_reads += length;
    return OS_STATUS_OK;
}

static void ram_close(void* context, void* handle)
{
    (void)context;
    (void)handle;
}

static OsStatus ram_read_dir(void* context, const char* path, uint32_t index, OsFsDirEntry* out)
{
    (void)context;
    (void)path;
    (void)index;
    (void)out;
    return OS_STATUS_END_OF_DIRECTORY;
}

static const OsFsDriver g_ram_driver = { ram_stat, ram_open, ram_read, ram_close, ram_read_dir, NULL };

/* ---- Paging in host memory, for swapping out. */

#define SLOTS 256U
static uint8_t g_slots[SLOTS][4096];

static OsStatus store_write(void* context, uint32_t slot, const void* frame)
{
    (void)context;
    memcpy(g_slots[slot], frame, 4096U);
    return OS_STATUS_OK;
}

static OsStatus store_read(void* context, uint32_t slot, void* frame)
{
    (void)context;
    memcpy(frame, g_slots[slot], 4096U);
    return OS_STATUS_OK;
}

static const OsPageStore g_store = { NULL, SLOTS, 0U, store_write, store_read, NULL, NULL, NULL, NULL };

/* ---- A saved system in host memory. */

typedef struct
{
    uint8_t* d;
    uint32_t n;
    uint32_t cap;
    uint32_t at;
} Stream;

static OsStatus stream_write(void* context, const void* data, uint32_t size)
{
    Stream* s = (Stream*)context;
    if (s->n + size > s->cap)
    {
        uint32_t cap = (s->n + size) * 2U;
        uint8_t* d = realloc(s->d, cap);
        if (!d)
            return OS_STATUS_OUT_OF_MEMORY;
        s->d = d;
        s->cap = cap;
    }
    memcpy(s->d + s->n, data, size);
    s->n += size;
    return OS_STATUS_OK;
}

static OsStatus stream_read(void* context, void* data, uint32_t size)
{
    Stream* s = (Stream*)context;
    if (size > s->n - s->at)
        return OS_STATUS_INVALID_ARGUMENT;
    memcpy(data, s->d + s->at, size);
    s->at += size;
    return OS_STATUS_OK;
}

static void boot(void)
{
    os_init();
    os_page_store_set(&g_store);
    os_fs_mount("/p", &g_ram_driver, NULL);
}

/* ---- Running */

static OsTaskHandle spawn(const char* path)
{
    OsTaskHandle t = NULL;
    const char* argv[] = { path };
    return os_task_spawn_path(&t, path, argv, 1U, OS_TASK_PRIORITY_NORMAL, 0U) == OS_STATUS_OK ? t : NULL;
}

static void run(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++)
    {
        for (int s = 0; s < 8 && os_schedule() == OS_STATUS_OK; s++)
        {
        }
        os_tick(1U);
    }
}

static int all_dead(OsTaskHandle* tasks, int count)
{
    for (int i = 0; i < count; i++)
        if (tasks[i] && os_task_get_state(tasks[i]) != OS_TASK_DEAD)
            return 0;
    return 1;
}

static int32_t result_of(OsTaskHandle t)
{
    OsValue v;
    return os_task_get_return_value(t, &v) == OS_STATUS_OK ? v.value.i32 : -1;
}

/* What a whole run of the program returns, worked out here. */
static int32_t expected_result(void)
{
    static uint32_t words[4096];
    uint32_t sum = 0;
    memset(words, 0, sizeof(words));
    for (uint32_t tick = 0; tick < 20u; tick++)
        for (uint32_t i = 0; i < 4096u; i++)
            words[i] += i ^ tick;
    for (uint32_t i = 0; i < 4096u; i++)
        sum += words[i];
    return (int32_t)(sum & 0x7fffffff);
}

int main(void)
{
    Bin loaded = {0};
    Bin a = {0};
    uint8_t* changed = NULL;
    l = fopen("program_share.log", "w");
    ok(load(&loaded), "load program_share.wasm");
    if (!loaded.d)
        return 1;
    /*
     * Two programs of the same size and behaviour that differ in one byte:
     * the module with a custom section named "a" or "b" after it.
     */
    a.n = loaded.n + 4U;
    a.d = malloc(a.n);
    changed = malloc(a.n);
    memcpy(a.d, loaded.d, loaded.n);
    memcpy(a.d + loaded.n, "\x00\x02\x01" "a", 4);
    memcpy(changed, a.d, a.n);
    changed[a.n - 1U] = 'b';
    free(loaded.d);
    g_files[0] = (File){ "a.wasm", a.d, a.n };
    g_files[1] = (File){ "copy.wasm", a.d, a.n };
    g_files[2] = (File){ "other.wasm", changed, a.n };
    const int32_t want = expected_result();

    boot();
    {
        OsTaskHandle t[6] = {0};
        uint32_t reads;

        t[0] = spawn("/p/a.wasm");
        reads = g_reads;
        t[1] = spawn("/p/a.wasm");
        t[2] = spawn("/p/copy.wasm");
        ok(t[0] && t[1] && t[2], "three processes start");
        ok(os_get_program_count() == 1U, "the same file twice and a copy under another name: one program");
        ok(t[0] && t[1] && t[2] && t[0]->program == t[1]->program && t[1]->program == t[2]->program &&
               t[0]->wasm_module == t[2]->wasm_module,
           "they hold the same program and module");
        ok(g_reads - reads == 2U * a.n && t[0]->program->file.copy != NULL,
           "the later files were only read to compare them: one copy for the three");

        /* The file changes under the running processes. */
        g_files[0] = (File){ "a.wasm", changed, a.n };
        t[3] = spawn("/p/a.wasm");
        ok(t[3] && os_get_program_count() == 2U && t[3]->program != t[0]->program,
           "a file changed in one byte, the same size, is a new program");
        ok(t[3] && t[3]->wasm_bytes != t[0]->wasm_bytes, "the old processes keep the old bytes");

        /* From bytes, as a program starting a child from memory. */
        ok(os_task_spawn(&t[4], a.d, a.n, NULL, 0U, OS_TASK_PRIORITY_NORMAL, 0U) == OS_STATUS_OK && t[4] &&
               t[4]->program == t[0]->program && os_get_program_count() == 2U,
           "spawn from equal bytes shares the loaded program, no copy");

        run(5);
        /* Every process of the first program swapped out: its module goes; it comes back when one runs. */
        ok(os_task_swap_out(t[0]) == OS_STATUS_OK && os_task_swap_out(t[1]) == OS_STATUS_OK &&
               os_task_swap_out(t[2]) == OS_STATUS_OK && os_task_swap_out(t[4]) == OS_STATUS_OK,
           "swap out every process of the first program");
        ok(t[0]->program->module == NULL && t[3]->program->module != NULL,
           "its module is freed; the other program keeps its own");
        for (int i = 0; i < 400 && !all_dead(t, 5); i++)
            run(1);
        ok(all_dead(t, 5), "every process runs to its end");
        {
            int same = 1;
            for (int i = 0; i < 5; i++)
                same &= result_of(t[i]) == want;
            ok(same, "each computed its own result, as if alone");
        }
        for (int i = 0; i < 5; i++)
            if (t[i])
                os_task_delete(t[i]);
        ok(os_get_program_count() == 0U, "the programs went with their last processes");
    }

    /* Saved mid-way and restored: the processes share their program again. */
    g_files[0] = (File){ "a.wasm", a.d, a.n };
    {
        OsTaskHandle t[2];
        Stream s = {0};
        uint32_t ids[2];
        t[0] = spawn("/p/a.wasm");
        t[1] = spawn("/p/copy.wasm");
        ids[0] = t[0] ? os_task_get_id(t[0]) : 0;
        ids[1] = t[1] ? os_task_get_id(t[1]) : 0;
        run(5);
        ok(os_system_save(stream_write, &s) == OS_STATUS_OK, "save two processes of one program");
        os_shutdown();
        boot();
        ok(os_system_restore(stream_read, &s, NULL, NULL, 0U) == OS_STATUS_OK, "restore them");
        t[0] = os_task_find_by_id(ids[0]);
        t[1] = os_task_find_by_id(ids[1]);
        ok(t[0] && t[1] && t[0]->program == t[1]->program && os_get_program_count() == 1U,
           "restored, they share one program");
        for (int i = 0; i < 400 && !all_dead(t, 2); i++)
            run(1);
        ok(all_dead(t, 2) && result_of(t[0]) == want && result_of(t[1]) == want, "and run to the same results");
        os_task_delete(t[0]);
        os_task_delete(t[1]);
        free(s.d);
    }

    ok(os_get_program_count() == 0U && os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    free(changed);
    free(a.d);
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
