#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Returns how many entries dir has, or -1; sets *found to the type of name. */
static int list(const char* dir, const char* name, int* found)
{
    DIR* handle = opendir(dir);
    struct dirent* entry = NULL;
    int count = 0;

    *found = 0;
    if (!handle)
        return -1;
    while ((entry = readdir(handle)) != NULL)
    {
        printf("%s: %s (%s)\n", dir, entry->d_name, entry->d_type == DT_DIR ? "dir" : "file");
        ++count;
        if (strcmp(entry->d_name, name) == 0)
            *found = entry->d_type == DT_DIR ? 2 : 1;
        if (strncmp(entry->d_name, "._", 2) == 0)
            *found = -100;
    }
    closedir(handle);
    return count;
}

int main(void)
{
    int found = 0;
    DIR* handle = NULL;

    /* hello.txt, docs (listed twice in the archive) and the /extra mount point */
    if (list("/", "hello.txt", &found) != 3 || found != 1)
        return 1;
    if (list("/", "docs", &found) != 3 || found != 2)
        return 2;
    if (list("/", "extra", &found) != 3 || found != 2)
        return 3;
    /* docs only holds an implied directory */
    if (list("/docs", "nested", &found) != 1 || found != 2)
        return 4;
    if (list("/docs/nested", "deep.txt", &found) != 1 || found != 1)
        return 5;
    if (list("/extra", "data.bin", &found) != 1 || found != 1)
        return 6;

    errno = 0;
    handle = opendir("/hello.txt");
    if (handle != NULL || errno != ENOTDIR)
        return 7;
    errno = 0;
    handle = opendir("/nowhere");
    if (handle != NULL || errno != ENOENT)
        return 8;

    printf("directory checks passed\n");
    return 0;
}
