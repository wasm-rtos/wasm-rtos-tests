/* The loop body is empty, so it compiles to a bare branch back to the loop
 * header. The scheduler must still preempt it when its fuel runs out. */
void app_main(void)
{
    for (;;)
    {
    }
}
