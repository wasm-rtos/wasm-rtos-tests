#include <wasm-rtos.h>

/* (module (func (export "app_main") (loop br 0))) */
static const unsigned char endless_child[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x04, 0x01, 0x60, 0x00, 0x00,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0c, 0x01, 0x08, 'a', 'p', 'p', '_', 'm', 'a', 'i', 'n', 0x00, 0x00,
    0x0a, 0x09, 0x01, 0x07, 0x00, 0x03, 0x40, 0x0c, 0x00, 0x0b, 0x0b
};

/* Spawns until the kernel refuses; returns how many spawns succeeded, or
 * -1 if the refusal was not -WR_ELIMIT. */
int app_main(void)
{
    int started = 0;

    for (;;)
    {
        int32_t child = wr_spawn(endless_child, sizeof(endless_child), 0, 0U, 0U, 0U);
        if (child > 0)
        {
            ++started;
            continue;
        }
        return child == -WR_ELIMIT ? started : -1;
    }
}
