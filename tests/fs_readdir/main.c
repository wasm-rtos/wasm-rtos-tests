#include "os.h"
#include "fs.h"
#include "hal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    FILE* x = fopen("fs_readdir.wasm", "rb");
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

/* Minimal ustar writer: appends one entry and returns the next offset. */
static uint32_t tar_add(uint8_t* image, uint32_t offset, const char* name, char type, const void* data, uint32_t size)
{
    uint8_t* h = image + offset;
    unsigned sum = 0U;
    uint32_t i;

    memset(h, 0, 512);
    strncpy((char*)h, name, 100);
    memcpy(h + 100, "0000644", 7);
    memcpy(h + 108, "0000000", 7);
    memcpy(h + 116, "0000000", 7);
    snprintf((char*)h + 124, 12, "%011o", size);
    snprintf((char*)h + 136, 12, "%011o", 0U);
    h[156] = (uint8_t)type;
    memcpy(h + 257, "ustar", 6);
    memcpy(h + 263, "00", 2);
    memset(h + 148, ' ', 8);
    for (i = 0; i < 512; i++)
        sum += h[i];
    snprintf((char*)h + 148, 8, "%06o", sum);
    h[155] = ' ';
    if (size)
        memcpy(image + offset + 512, data, size);
    return offset + 512 + (size + 511U) / 512U * 512U;
}

/* hello.txt, ._hello.txt (macOS metadata), docs/ listed explicitly and docs/nested only implied. */
static uint32_t build_main_image(uint8_t* image)
{
    uint32_t o = 0;
    o = tar_add(image, o, "./hello.txt", '0', "hello from tar\n", 15);
    o = tar_add(image, o, "./._hello.txt", '0', "junk", 4);
    o = tar_add(image, o, "docs/", '5', NULL, 0);
    o = tar_add(image, o, "docs/nested/deep.txt", '0', "deep\n", 5);
    return o + 1024;
}

static uint32_t build_extra_image(uint8_t* image)
{
    uint8_t data[1000];
    uint32_t i;
    for (i = 0; i < sizeof(data); i++)
        data[i] = (uint8_t)(i % 251);
    return tar_add(image, 0, "data.bin", '0', data, sizeof(data)) + 1024;
}

static uint32_t run_program(Bin* b, const char* name)
{
    OsTaskHandle t = NULL;
    uint32_t i;
    uint32_t code = 999U;
    ok(os_task_create(&t, b->d, b->n, NULL, name, 65536U, OS_TASK_PRIORITY_NORMAL) == OS_STATUS_OK && t,
       "create WASI program");
    for (i = 0; t && i < 200U && os_task_get_state(t) != OS_TASK_DEAD; i++)
        os_schedule();
    if (t)
    {
        ok(os_task_get_state(t) == OS_TASK_DEAD, "program finishes");
        code = os_task_get_exit_code(t);
        printf("exit code: %u\n", code);
        os_task_delete(t);
    }
    return code;
}

static uint8_t main_image[8192];
static uint8_t extra_image[4096];

int main(void)
{
    Bin b = {0};
    OsFsTar main_tar;
    OsFsTar extra_tar;
    l = fopen("fs_readdir.log", "a");
    hal_init();
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(os_fs_tar_init(&main_tar, main_image, build_main_image(main_image)) == OS_STATUS_OK, "main image is valid");
    ok(os_fs_tar_init(&extra_tar, extra_image, build_extra_image(extra_image)) == OS_STATUS_OK, "extra image is valid");
    ok(os_fs_mount("/", &os_fs_tar_driver, &main_tar) == OS_STATUS_OK, "mount main image at /");
    ok(os_fs_mount("/extra", &os_fs_tar_driver, &extra_tar) == OS_STATUS_OK, "mount extra image at /extra");
    ok(load(&b), "load fs_readdir.wasm");
    if (b.d)
    {
        ok(run_program(&b, "fs_readdir") == 0U, "program listed files, implied directories and mount points");
        free(b.d);
    }
    os_shutdown();
    hal_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
