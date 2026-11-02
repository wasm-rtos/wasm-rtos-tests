#include "os.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
    FILE* x = fopen("input_api.wasm", "rb");
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

/* Linux input event codes the board reports. */
#define KEY_A 30U
#define KEY_SELECT 0x161U
#define BTN_0 0x100U
#define REL_DIAL 7U

/* Device numbers of the board. */
enum { BUTTONS, KEYBOARD, BEZEL, TOUCH };

static OsTaskHandle start(Bin* b, const char* entry)
{
    OsTaskHandle t = NULL;
    if (os_task_create(&t, b->d, b->n, entry, entry, 0U, OS_TASK_PRIORITY_NORMAL) != OS_STATUS_OK)
        return NULL;
    return t;
}

/* Run until every task waits; the board then reports what a person does. */
static void settle(void)
{
    uint32_t i;
    for (i = 0; i < 1000U && os_schedule() == OS_STATUS_OK; i++)
    {
    }
}

static int32_t finish(OsTaskHandle t, const char* entry)
{
    OsValue v;
    uint32_t i;
    int32_t result = -100000;
    for (i = 0; t && i < 1000U && os_task_get_state(t) != OS_TASK_DEAD; i++)
    {
        if (os_schedule() == OS_STATUS_NO_READY_TASKS)
            os_tick(10U);
    }
    if (t && os_task_get_state(t) == OS_TASK_DEAD && os_task_get_return_value(t, &v) == OS_STATUS_OK)
        result = (int32_t)v.value.i32;
    if (result != 0)
        printf("     %s returned %d\n", entry, (int)result);
    if (t)
        os_task_delete(t);
    return result;
}

int main(void)
{
    Bin b = {0};
    OsTaskHandle t = NULL;
    OsInputInfo buttons = { OS_INPUT_BUTTONS, 0U, 0U, 0U, 600U, 0U, 0U };
    OsInputInfo keyboard = { OS_INPUT_KEYBOARD, 0U, 0U, 0U, 0U, 300U, 100U };
    OsInputInfo bezel = { OS_INPUT_ROTARY, 0U, 0U, 24U, 0U, 0U, 0U };
    OsInputInfo touch = { OS_INPUT_TOUCH, 240U, 240U, 0U, 0U, 0U, 0U };
    OsInputInfo bad = { OS_INPUT_KEYBOARD, 0U, 0U, 0U, 0U, 300U, 0U };
    uint32_t i;

    l = fopen("input_api.log", "w");
    ok(os_init() == OS_STATUS_OK, "initialize OS");
    ok(load(&b), "load input_api.wasm");
    if (!b.d)
        return 1;

    ok(finish(start(&b, "app_main_try"), "app_main_try") == -13, "without devices input-open returns -WR_EUNSUPPORTED");

    ok(os_input_add("buttons", BUTTONS, &buttons) == OS_STATUS_OK && os_input_add("keyboard", KEYBOARD, &keyboard) == OS_STATUS_OK &&
           os_input_add("bezel", BEZEL, &bezel) == OS_STATUS_OK && os_input_add("touch", TOUCH, &touch) == OS_STATUS_OK,
       "register buttons, a keyboard, a bezel and a touch screen");
    ok(os_input_add("bezel", 9U, &bezel) == OS_STATUS_BUSY && os_input_add("knob", BEZEL, &bezel) == OS_STATUS_BUSY,
       "names and numbers are registered once");
    ok(os_input_add("bad", 9U, &bad) == OS_STATUS_INVALID_ARGUMENT, "repeat needs an interval");
    ok(os_input_rotate(BUTTONS, REL_DIAL, 1) == OS_STATUS_INVALID_ARGUMENT &&
           os_input_touch(TOUCH, 0U, OS_INPUT_TOUCH_DOWN, 240U, 0U) == OS_STATUS_INVALID_ARGUMENT &&
           os_input_text(BUTTONS, 'a') == OS_STATUS_INVALID_ARGUMENT && os_input_key(TOUCH, KEY_A, 1U) == OS_STATUS_INVALID_ARGUMENT &&
           os_input_text(KEYBOARD, 0xD800U) == OS_STATUS_INVALID_ARGUMENT && os_input_key(42U, KEY_A, 1U) == OS_STATUS_INVALID_ARGUMENT,
       "reports that do not fit the device are refused");

    /* Before anyone owns the device, what happens is not kept. */
    ok(os_input_key(BUTTONS, KEY_SELECT, 1U) == OS_STATUS_OK && os_input_key(BUTTONS, KEY_SELECT, 0U) == OS_STATUS_OK,
       "an unowned device takes reports");
    t = start(&b, "app_main_buttons");
    settle();
    ok(os_input_get_owner(BUTTONS) == os_task_get_id(t) && os_task_get_state(t) == OS_TASK_WAITING,
       "the program owns the buttons and waits for them");
    os_input_key(BUTTONS, KEY_SELECT, 1U);
    os_input_key(BUTTONS, KEY_SELECT, 1U);
    settle();
    ok(os_get_next_wakeup_ms() == 600U, "a held button asks to wake the kernel at its long-press time");
    os_tick(600U);
    settle();
    os_tick(100U);
    os_input_key(BUTTONS, KEY_SELECT, 0U);
    ok(finish(t, "app_main_buttons") == 0, "press, long press once at 600 ms, release; nothing from before open");
    ok(os_input_get_owner(BUTTONS) == 0U, "the device is free again when the program ends");

    t = start(&b, "app_main_keyboard");
    settle();
    os_input_key(KEYBOARD, KEY_A, 1U);
    os_input_text(KEYBOARD, 'a');
    settle();
    os_tick(300U);
    settle();
    os_tick(100U);
    settle();
    os_input_key(KEYBOARD, KEY_A, 0U);
    ok(finish(t, "app_main_keyboard") == 0, "press, text, repeats after 300 ms every 100 ms, release");

    t = start(&b, "app_main_bezel");
    settle();
    os_input_rotate(BEZEL, REL_DIAL, 1);
    os_input_rotate(BEZEL, REL_DIAL, 1);
    os_input_rotate(BEZEL, REL_DIAL, 1);
    os_input_rotate(BEZEL, REL_DIAL, -1);
    settle();
    os_input_rotate(BEZEL, REL_DIAL, -5);
    ok(finish(t, "app_main_bezel") == 0, "unread turns add up into one event");

    t = start(&b, "app_main_touch");
    settle();
    os_input_touch(TOUCH, 0U, OS_INPUT_TOUCH_DOWN, 10U, 11U);
    os_input_touch(TOUCH, 0U, OS_INPUT_TOUCH_MOVE, 20U, 21U);
    os_input_touch(TOUCH, 0U, OS_INPUT_TOUCH_MOVE, 30U, 31U);
    os_input_touch(TOUCH, 0U, OS_INPUT_TOUCH_UP, 30U, 31U);
    ok(finish(t, "app_main_touch") == 0, "down, the last move, up");

    t = start(&b, "app_main_overflow");
    settle();
    for (i = 0; i < 40U; i++)
        os_input_key(BUTTONS, BTN_0 + i / 2U, (uint8_t)(i % 2U == 0U));
    ok(finish(t, "app_main_overflow") == 0, "a full queue drops the oldest events and says how many");

    ok(finish(start(&b, "app_main_errors"), "app_main_errors") == 0, "busy, unknown, flags, bad pointers and handles, nothing queued");

    free(b.d);
    ok(os_get_task_count() == 0U, "clean OS state");
    os_shutdown();
    if (l)
        fclose(l);
    return f ? 1 : 0;
}
