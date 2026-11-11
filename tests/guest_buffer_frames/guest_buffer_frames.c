#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wasi/api.h>
#include <wasm-rtos.h>

/*
 * Every buffer the program hands the OS lies across a 4 KiB boundary, where
 * a paged memory splits it between two frames. The host makes the memory
 * paged; the program checks that each call still reads and writes the
 * right bytes, and returns how many checks failed.
 */
#define FRAME 4096U

static uint8_t area[10U * FRAME] __attribute__((aligned(4096)));
static int failures;

static void check(int ok, const char* what)
{
    printf("%s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok)
        failures++;
}

int main(void)
{
    static const char text[] = "written across frames\n";
    int32_t queue = wr_queue_open("frames", 16U, 1U);
    uint8_t* sent = area + 1U * FRAME - 8U;
    uint8_t* received = area + 2U * FRAME - 8U;
    __wasi_ciovec_t iov;
    uint8_t* iov_at = area + 3U * FRAME - 4U;
    uint8_t* data = area + 4U * FRAME - 10U;
    uint8_t* written_at = area + 5U * FRAME - 2U;
    uint8_t* time_at = area + 6U * FRAME - 4U;
    uint32_t written = 0U;
    uint64_t time = 0U;

    check(queue >= 0, "open the host's queue");

    /* A queue item sent from and received into split buffers. */
    memcpy(sent, "sixteen bytes!!", 16U);
    memset(received, 0, 16U);
    check(wr_queue_send(queue, sent, 0U) == 0 && wr_queue_receive(queue, received, 0U) == 0 &&
              memcmp(received, "sixteen bytes!!", 16U) == 0,
          "a queue item goes across frames both ways");

    /* fd_write with the iovec, the bytes and the count each split. */
    memcpy(data, text, sizeof(text) - 1U);
    iov.buf = data;
    iov.buf_len = sizeof(text) - 1U;
    memcpy(iov_at, &iov, sizeof(iov));
    fflush(stdout);
    check(__wasi_fd_write(1, (const __wasi_ciovec_t*)(void*)iov_at, 1, (__wasi_size_t*)(void*)written_at) == 0,
          "fd_write takes split buffers");
    memcpy(&written, written_at, sizeof(written));
    check(written == sizeof(text) - 1U, "fd_write counts the bytes into a split word");

    /* A 64-bit result written across frames. */
    memset(time_at, 0xFF, 8U);
    check(__wasi_clock_time_get(__WASI_CLOCKID_MONOTONIC, 1U, (__wasi_timestamp_t*)(void*)time_at) == 0,
          "clock_time_get writes a split timestamp");
    memcpy(&time, time_at, sizeof(time));
    check(time != UINT64_MAX, "the timestamp arrived");

    /* The host reads this while the program waits, and writes its own bytes. */
    memcpy(area + 7U * FRAME - 6U, "ACROSS-FRAME", 12U);
    printf("area %u\n", (unsigned)(uintptr_t)area);
    fflush(stdout);
    memset(area + 9U * FRAME - 8U, 0, 16U);
    check(wr_queue_receive(queue, area + 9U * FRAME - 8U, WR_WAIT_FOREVER) == 0 &&
              memcmp(area + 9U * FRAME - 8U, "from the host!!", 16U) == 0,
          "an item that arrives later lands across frames");
    check(memcmp(area + 8U * FRAME - 3U, "HOSTPUT", 7U) == 0, "the host wrote across frames");
    return failures;
}
