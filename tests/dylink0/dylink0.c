typedef struct Pair
{
    int first;
    int second;
}
Pair;

extern int library_add(int value);
extern const char * library_name(void);
extern void library_fill(Pair * pair, int base);
extern int library_apply(int (*transform)(int), int value);
extern int library_increment(void);

static int double_value(int value)
{
    return value * 2;
}

int app_main(int value)
{
    return library_add(value);
}

int app_name_char(int index)
{
    return (unsigned char) library_name()[index];
}

int app_struct(void)
{
    Pair pair;
    library_fill(&pair, 20);
    return pair.first + pair.second;
}

int app_callback(void)
{
    return library_apply(double_value, 6);
}

int app_counter(void)
{
    volatile int work = 0;
    int index;

    /* Force several RTOS fuel slices before entering the shared library. */
    for (index = 0; index < 50000; ++index)
        work += index & 1;

    library_increment();
    return library_increment() + (work == -1);
}

int app_work_only(void)
{
    volatile int work = 0;
    int index;

    for (index = 0; index < 50000; ++index)
        work += index & 1;

    return work;
}
