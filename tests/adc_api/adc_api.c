#include <wasm-rtos.h>

int32_t app_main_try(void)
{
    return wr_adc_open_cstr("a0");
}

int32_t app_main_read(void)
{
    int32_t pot = wr_adc_open_cstr("a0");
    int32_t battery = wr_adc_open_cstr("battery");
    int32_t again = wr_adc_open_cstr("a0");

    if (pot <= 0 || battery <= 0)
        return 1;
    /* Reading changes nothing: a channel is shared. */
    if (again <= 0 || again == pot)
        return 2;
    if (wr_adc_get(pot, WR_ADC_RESOLUTION) != 12 || wr_adc_get(pot, WR_ADC_MAX) != 4095 ||
        wr_adc_get(pot, WR_ADC_FULL_SCALE_MV) != 3300 || wr_adc_get(pot, 9u) != -WR_EUNSUPPORTED)
        return 3;
    if (wr_adc_read(pot) != 2048 || wr_adc_read(again) != 2048)
        return 4;
    /* No calibration in the port for a0: the kernel scales, rounded. */
    if (wr_adc_read_mv(pot) != 1650)
        return 5;
    /* Too large a raw value from the port is held at the channel's maximum. */
    if (wr_adc_read(battery) != 1023)
        return 6;
    if (wr_adc_open_cstr("nope") != -WR_ENOTFOUND || wr_adc_open("a0", 2u, 1u) != -WR_EINVAL)
        return 7;
    if (wr_wait_any(&pot, 1u, 0u) != -WR_EBADH || wr_adc_read(99) != -WR_EBADH)
        return 8;
    return 0;
}

int32_t app_main_calibrated(void)
{
    int32_t battery = wr_adc_open_cstr("battery");
    return battery <= 0 ? -1 : wr_adc_read_mv(battery);
}
