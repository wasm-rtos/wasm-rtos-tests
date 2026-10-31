#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Each failed check returns its own exit code; 0 means every check passed. */
int main(void)
{
    char buffer[64];
    unsigned char block[300];
    FILE* file = NULL;
    size_t count = 0;
    size_t total = 0;
    struct stat info;

    file = fopen("/hello.txt", "rb");
    if (!file)
        return 1;
    count = fread(buffer, 1, sizeof(buffer) - 1, file);
    buffer[count] = '\0';
    if (strcmp(buffer, "hello from tar\n") != 0)
        return 2;
    if (fseek(file, 6, SEEK_SET) != 0)
        return 3;
    count = fread(buffer, 1, 4, file);
    buffer[count] = '\0';
    if (strcmp(buffer, "from") != 0)
        return 4;
    if (ftell(file) != 10)
        return 5;
    if (fseek(file, -4, SEEK_END) != 0)
        return 6;
    count = fread(buffer, 1, sizeof(buffer) - 1, file);
    buffer[count] = '\0';
    if (strcmp(buffer, "tar\n") != 0)
        return 7;
    fclose(file);

    file = fopen("/docs/nested/../nested/./deep.txt", "r");
    if (!file)
        return 8;
    count = fread(buffer, 1, sizeof(buffer) - 1, file);
    buffer[count] = '\0';
    fclose(file);
    if (strcmp(buffer, "deep\n") != 0)
        return 9;

    /* A file on a second image mounted at /extra, read in chunks. */
    file = fopen("/extra/data.bin", "rb");
    if (!file)
        return 10;
    while ((count = fread(block, 1, sizeof(block), file)) > 0)
    {
        for (size_t i = 0; i < count; ++i)
            if (block[i] != (unsigned char)((total + i) % 251))
                return 11;
        total += count;
    }
    fclose(file);
    if (total != 1000)
        return 12;

    if (stat("/hello.txt", &info) != 0 || info.st_size != 15 || !S_ISREG(info.st_mode))
        return 13;
    if (stat("/docs", &info) != 0 || !S_ISDIR(info.st_mode))
        return 14;
    if (stat("/extra", &info) != 0 || !S_ISDIR(info.st_mode))
        return 15;

    errno = 0;
    if (fopen("/missing.txt", "r") != NULL || errno != ENOENT)
        return 16;
    errno = 0;
    if (fopen("/hello.txt", "w") != NULL || errno != EROFS)
        return 17;
    errno = 0;
    if (fopen("/new.txt", "w") != NULL || errno != EROFS)
        return 18;

    /* Relative paths resolve from the root, and ".." cannot leave it. */
    file = fopen("hello.txt", "r");
    if (!file)
        return 19;
    fclose(file);
    file = fopen("/../../hello.txt", "r");
    if (!file)
        return 20;
    fclose(file);

    printf("file system checks passed\n");
    return 0;
}
