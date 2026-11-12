#include <wasm-rtos.h>

/*
 * Programs that share the kernel's objects and wait in every way a program
 * can: each returns a hash of what it saw and when, so a run that was saved
 * and restored ends with the same numbers only if nothing was lost.
 */

/* The file reader reads through WASI without a C library. */
typedef struct
{
    uint8_t* buf;
    uint32_t buf_len;
} Iovec;

__attribute__((import_module("wasi_snapshot_preview1"), import_name("path_open")))
int32_t file_open(int32_t fd, int32_t dirflags, const char* path, uint32_t path_len, int32_t oflags,
                  uint64_t rights_base, uint64_t rights_inheriting, int32_t fdflags, int32_t* out_fd);
__attribute__((import_module("wasi_snapshot_preview1"), import_name("fd_read")))
int32_t file_read(int32_t fd, const Iovec* iovs, uint32_t iovs_len, uint32_t* out_read);

/* Memory the programs work over, so their memory holds more than zeros. */
static uint32_t table[2048];

static uint32_t mix(uint32_t hash, uint32_t value)
{
    return (hash ^ value) * 16777619u;
}

static uint32_t now(void)
{
    return (uint32_t)wr_time_ms();
}

static uint32_t work(uint32_t seed, uint32_t rounds)
{
    uint32_t hash = 2166136261u;
    uint32_t i;
    for (i = 0; i < rounds; ++i)
    {
        table[(i * 13u) % 2048u] += seed + i;
        hash = mix(hash, table[(i * 7u) % 2048u]);
    }
    return hash;
}

/* Sends 24 items through a queue of 2, gives the semaphore every 6th, then sets the event. */
uint32_t app_main_producer(void)
{
    int32_t queue = wr_queue_open("jobs", 4, 2);
    int32_t ready = wr_semaphore_open("ready", 3, 0);
    int32_t done = wr_event_open("done");
    uint32_t hash = 2166136261u;
    uint32_t i;
    for (i = 1; i <= 24; ++i)
    {
        uint32_t item = i * 7u + work(i, 200);
        if (wr_queue_send(queue, &item, WR_WAIT_FOREVER) < 0)
            return 1;
        hash = mix(mix(hash, item), now());
        if (i % 6u == 0)
        {
            wr_semaphore_give(ready);
            wr_sleep_ms(3);
        }
    }
    wr_event_set(done, 1);
    return hash;
}

uint32_t app_main_consumer(void)
{
    int32_t queue = wr_queue_open("jobs", 4, 2);
    int32_t ready = wr_semaphore_open("ready", 3, 0);
    int32_t done = wr_event_open("done");
    uint32_t hash = 2166136261u;
    uint32_t i;
    for (i = 0; i < 24; ++i)
    {
        uint32_t item = 0;
        if (wr_queue_receive(queue, &item, WR_WAIT_FOREVER) < 0)
            return 1;
        hash = mix(mix(hash, item), now());
        if (i % 5u == 0)
            wr_sleep_ms(2);
    }
    for (i = 0; i < 4; ++i)
        hash = mix(mix(hash, (uint32_t)wr_semaphore_take(ready, 40)), now());
    hash = mix(mix(hash, (uint32_t)wr_event_wait(done, 1, WR_EVENT_CLEAR, 500)), now());
    return hash;
}

/* Holds the mutex while it sleeps. */
uint32_t app_main_locker(void)
{
    int32_t lock = wr_mutex_open("lock");
    uint32_t hash = 2166136261u;
    uint32_t i;
    for (i = 0; i < 5; ++i)
    {
        hash = mix(mix(hash, (uint32_t)wr_mutex_lock(lock, WR_WAIT_FOREVER)), now());
        wr_sleep_ms(6);
        hash = mix(hash, work(i, 100));
        wr_mutex_unlock(lock);
        wr_sleep_ms(1);
    }
    return hash;
}

/* Waits for the mutex the locker holds. */
uint32_t app_main_contender(void)
{
    int32_t lock = wr_mutex_open("lock");
    uint32_t hash = 2166136261u;
    uint32_t i;
    wr_sleep_ms(1);
    for (i = 0; i < 5; ++i)
    {
        int32_t result = wr_mutex_lock(lock, 200);
        hash = mix(mix(hash, (uint32_t)result), now());
        wr_sleep_ms(2);
        if (result >= 0)
            wr_mutex_unlock(lock);
    }
    return hash;
}

/* Waits for a periodic timer and counts what it read. */
uint32_t app_main_ticker(void)
{
    int32_t timer = wr_timer_create(7, WR_TIMER_PERIODIC);
    uint32_t hash = 2166136261u;
    uint32_t i;
    wr_timer_start(timer);
    for (i = 0; i < 8; ++i)
    {
        hash = mix(hash, (uint32_t)wr_wait_any(&timer, 1, 100));
        hash = mix(mix(hash, (uint32_t)wr_timer_read(timer)), now());
    }
    return hash;
}

/* Starts this module from the file system as a child and waits for it. */
uint32_t app_main_parent(void)
{
    static const char path[] = "/bin/system_snapshot.wasm";
    int32_t child = wr_spawn_path(path, sizeof(path) - 1u, 0, 0, 0, 0);
    uint32_t hash = mix(2166136261u, (uint32_t)(child > 0));
    hash = mix(mix(hash, (uint32_t)wr_wait(child, WR_WAIT_FOREVER)), now());
    return hash;
}

/* What a child process runs. */
uint32_t app_main(void)
{
    uint32_t hash = work(5, 300);
    wr_sleep_ms(9);
    hash = mix(hash, work(6, 300));
    wr_sleep_ms(9);
    return mix(hash, now()) & 0x7FFFu;
}

/* Reads a file 5 bytes at a time, sleeping between. */
uint32_t app_main_reader(void)
{
    static const char path[] = "data.txt";
    uint8_t bytes[5];
    Iovec iov = {bytes, sizeof(bytes)};
    uint32_t hash = 2166136261u;
    int32_t fd = -1;
    uint32_t got = 0;
    uint32_t i;
    /* fd 3 is the root directory; rights: fd_read | fd_seek. */
    if (file_open(3, 0, path, sizeof(path) - 1u, 0, 6u, 0, 0, &fd) != 0)
        return 1;
    do
    {
        got = 0;
        if (file_read(fd, &iov, 1, &got) != 0)
            return 2;
        for (i = 0; i < got; ++i)
            hash = mix(hash, bytes[i]);
        hash = mix(hash, now());
        wr_sleep_ms(2);
    } while (got > 0);
    return hash;
}

/* Sleeps 100 ms and says how long that took. */
uint32_t app_main_sleeper(void)
{
    uint32_t start = now();
    wr_sleep_ms(100);
    return now() - start;
}
