#include <wasm-rtos.h>

static wr_input_event g_events[48];

/* Wait for the device and collect events until there are count of them. */
static int32_t collect(int32_t device, uint32_t count)
{
    uint32_t have = 0;

    while (have < count)
    {
        int32_t got;
        if (wr_wait_any(&device, 1u, 5000u) != 0)
            return -1;
        got = wr_input_read(device, g_events + have, count - have);
        if (got <= 0)
            return -2;
        have += (uint32_t)got;
    }
    return 0;
}

static int is_key(const wr_input_event* event, uint16_t code, int32_t value)
{
    return event->type == WR_EVENT_KEY && event->code == code && event->value == value;
}

int32_t app_main_try(void)
{
    return wr_input_open_cstr("buttons");
}

/* A press held past the long-press time, then released. */
int32_t app_main_buttons(void)
{
    int32_t buttons = wr_input_open_cstr("buttons");

    if (buttons <= 0)
        return 1;
    if (wr_input_get(buttons, WR_INPUT_KIND) != (int32_t)WR_INPUT_BUTTONS || wr_input_get(buttons, 99u) != -WR_EUNSUPPORTED)
        return 2;
    if (wr_input_read(buttons, g_events, 4u) != 0)
        return 3;
    if (collect(buttons, 3u) != 0)
        return 4;
    if (!is_key(&g_events[0], WR_KEY_SELECT, WR_KEY_PRESSED) || !is_key(&g_events[1], WR_KEY_SELECT, WR_KEY_LONG_PRESS) ||
        !is_key(&g_events[2], WR_KEY_SELECT, WR_KEY_RELEASED))
        return 5;
    if (g_events[1].time_ms - g_events[0].time_ms != 600u || g_events[2].time_ms - g_events[0].time_ms != 700u)
        return 6;
    return 0;
}

/* Key repeat and the text the key typed. */
int32_t app_main_keyboard(void)
{
    int32_t keyboard = wr_input_open_cstr("keyboard");

    if (keyboard <= 0)
        return 1;
    if (collect(keyboard, 5u) != 0)
        return 2;
    if (!is_key(&g_events[0], 30u, WR_KEY_PRESSED) || g_events[1].type != WR_EVENT_TEXT || g_events[1].value != 'a' ||
        !is_key(&g_events[2], 30u, WR_KEY_REPEATED) || !is_key(&g_events[3], 30u, WR_KEY_REPEATED) ||
        !is_key(&g_events[4], 30u, WR_KEY_RELEASED))
        return 3;
    if (g_events[3].time_ms - g_events[2].time_ms != 100u)
        return 4;
    return 0;
}

/* Unread turns add up; the next turn after a read is a new event. */
int32_t app_main_bezel(void)
{
    int32_t bezel = wr_input_open_cstr("bezel");

    if (bezel <= 0)
        return 1;
    if (wr_input_get(bezel, WR_INPUT_KIND) != (int32_t)WR_INPUT_ROTARY || wr_input_get(bezel, WR_INPUT_STEPS_PER_TURN) != 24)
        return 2;
    if (collect(bezel, 1u) != 0)
        return 3;
    if (g_events[0].type != WR_EVENT_ROTATE || g_events[0].code != WR_REL_DIAL || g_events[0].value != 2)
        return 4;
    if (collect(bezel, 1u) != 0 || g_events[0].value != -5)
        return 5;
    return 0;
}

/* Only the last position of unread moves stays. */
int32_t app_main_touch(void)
{
    int32_t touch = wr_input_open_cstr("touch");

    if (touch <= 0)
        return 1;
    if (wr_input_get(touch, WR_INPUT_WIDTH) != 240 || wr_input_get(touch, WR_INPUT_HEIGHT) != 240)
        return 2;
    if (collect(touch, 3u) != 0)
        return 3;
    if (g_events[0].type != WR_EVENT_TOUCH || g_events[0].value != WR_TOUCH_DOWN || g_events[0].x != 10 || g_events[0].y != 11)
        return 4;
    if (g_events[1].value != WR_TOUCH_MOVE || g_events[1].x != 30 || g_events[1].y != 31)
        return 5;
    if (g_events[2].value != WR_TOUCH_UP || g_events[2].code != 0)
        return 6;
    return 0;
}

/* More events than the queue holds: the oldest go, and the loss comes first. */
int32_t app_main_overflow(void)
{
    int32_t buttons = wr_input_open_cstr("buttons");
    int32_t got;

    if (buttons <= 0)
        return 1;
    if (wr_wait_any(&buttons, 1u, 5000u) != 0)
        return 2;
    got = wr_input_read(buttons, g_events, 48u);
    if (got != 33)
        return 100 + got;
    if (g_events[0].type != WR_EVENT_DROPPED || g_events[0].value != 8)
        return 3;
    /* 40 events: presses and releases of BTN_0 + n / 2; the first 8 were lost. */
    if (!is_key(&g_events[1], WR_BTN_0 + 4u, WR_KEY_PRESSED) || !is_key(&g_events[32], WR_BTN_0 + 19u, WR_KEY_RELEASED))
        return 4;
    return 0;
}

int32_t app_main_errors(void)
{
    int32_t buttons = wr_input_open_cstr("buttons");

    if (buttons <= 0)
        return 1;
    if (wr_input_open_cstr("buttons") != -WR_EINUSE || wr_input_open_cstr("nope") != -WR_ENOTFOUND ||
        wr_input_open("touch", 5u, 1u) != -WR_EINVAL)
        return 2;
    if (wr_input_read(buttons, (wr_input_event*)0xFFFFFFF0u, 1u) != -WR_EFAULT)
        return 3;
    if (wr_input_read(buttons + 1, g_events, 1u) != -WR_EBADH || wr_input_get(77, WR_INPUT_KIND) != -WR_EBADH)
        return 4;
    /* Nothing queued: wait.any times out instead of returning at once. */
    if (wr_wait_any(&buttons, 1u, 0u) != -WR_ETIMEOUT)
        return 5;
    return 0;
}
