typedef struct Pair
{
    int first;
    int second;
}
Pair;

typedef int (*Transform)(int value);

static const char library_name_data[] = "LVGL";
static const char * library_name_pointer = library_name_data;
static int counter;

int library_add(int value)
{
    return value + 37;
}

const char * library_name(void)
{
    return library_name_pointer;
}

void library_fill(Pair * pair, int base)
{
    pair->first = base;
    pair->second = base + 1;
}

int library_apply(Transform transform, int value)
{
    return transform(value) + 5;
}

int library_increment(void)
{
    return ++counter;
}
