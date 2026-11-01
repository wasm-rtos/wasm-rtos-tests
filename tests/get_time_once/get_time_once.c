#include <wasm-rtos.h>

int app_main(void)
{
    return wr_time_ms() == 1234U ? 0 : 1;
}
