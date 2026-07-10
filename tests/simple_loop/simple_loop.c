void app_main(void)
{
    volatile unsigned int accumulator = 0U;

    for (unsigned int i = 0U; i < 1024U; ++i)
    {
        accumulator += i;
    }
}
