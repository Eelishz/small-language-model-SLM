#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

typedef struct {
    char *ptr;
    size_t len;
} StringSlice;

#define write_string_slice(s, f) write(f, s.ptr, s.len);

bool is_space(char c) {
    return c == ' ' || c == '\n';
}

int main() {
    struct stat sb;

    int fd = open("data.txt", O_RDONLY);
    fstat(fd, &sb);
    printf("Size: %lu\n", (uint64_t)sb.st_size);

    char *data = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        return 1;
    }
    printf("mapped, booyah!\n");

    int cursor = 0;
    int token_start = 0;
    while (cursor < sb.st_size) {
        while (is_space(data[cursor])) cursor++;
        if (cursor >= sb.st_size) break;
        token_start = cursor;
        while (!is_space(data[cursor])) cursor++;

        if (token_start < cursor) {
            StringSlice slice = {
                .ptr = &data[token_start],
                .len = cursor - token_start
            };
            write_string_slice(slice, 1);
            printf("\n");
        }
    }

    if (munmap(data, sb.st_size) != 0) return 1;
}
