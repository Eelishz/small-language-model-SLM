#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define todo(msg) do {printf("TODO: %s\n", msg); abort();} while (false);

#define print_string_slice(s) printf("%.*s", (int)s.len, s.ptr)
#define INITIAL_FNV1A_STATE 14695981039346656037ULL
#define TOKEN_MAP_INITIAL_CAPACITY 4096*8
#define TOKEN_MAP_MAX_UTIL 0.80F
#define NGRAM_LEN 1

typedef struct {
    char *ptr;
    size_t len;
} StringSlice;

typedef StringSlice NGram[NGRAM_LEN];

typedef struct {
    size_t token_id;
    float p;
} MarkovChainItem;

typedef MarkovChainItem MarkovChain[];

typedef struct {
    StringSlice key;
    uint64_t value;
} OutputTokenMapItem;

typedef struct {
    OutputTokenMapItem *items;
    size_t count;
    size_t capacity;
} OutputTokenMap;

typedef struct {
    uint64_t value;
    NGram key;
} InputTokenMapItem;

typedef struct {
    InputTokenMapItem *items;
    size_t count;
    size_t capacity;
} InputTokenMap;

uint64_t fnv1a_hash(uint64_t state, const void *data, size_t len) {
    const uint8_t *bytes = data;
    for (size_t i = 0; i < len; i++) {
        state ^= bytes[i];
        state *= 1099511628211ULL;
    }
    return state;
}

bool slice_cmp(StringSlice s1, StringSlice s2) {
    return s1.len == s2.len && memcmp(s1.ptr, s2.ptr, s1.len) == 0;
}

bool input_token_map_insert(InputTokenMap *m, InputTokenMapItem item) {
    assert(m);
    if (!m->items) {
        m->items = malloc(sizeof(InputTokenMapItem) * TOKEN_MAP_INITIAL_CAPACITY);
        assert(m->items);
        m->capacity = TOKEN_MAP_INITIAL_CAPACITY;
        memset(m->items, 0, m->capacity);
    }

    if ((float)m->count / (float)m->capacity > TOKEN_MAP_MAX_UTIL) {
        todo("reallocation");
    }

    uint64_t hash = INITIAL_FNV1A_STATE;
    for (int i = 0; i < NGRAM_LEN; ++i) {
        StringSlice s = item.key[i];
        hash = fnv1a_hash(hash, s.ptr, s.len);
    }

    size_t index = hash % m->capacity;


    if (!m->items[index].key[0].ptr) {
        m->items[index] = item;
        m->count++;
        return false;
    }

    bool same_key = true;
    for (int i = 0; i < NGRAM_LEN; ++i) {
        same_key = same_key && slice_cmp(m->items[index].key[i], item.key[i]);
    }

    if (same_key) {
        m->items[index] = item;
        return true;
    }

    while (m->items[index].key[0].ptr) index = (index + 1) % m->capacity;
    m->items[index] = item;
    m->count++;

    return false;
}

void input_token_map_print(InputTokenMap *m) {
    for (size_t i = 0; i < m->capacity; ++i) {
        InputTokenMapItem item = m->items[i];

        if(!item.key[0].ptr) continue;

        printf("{ (");
        for (size_t i = 0; i < NGRAM_LEN; ++i) {
            printf("\"");
            print_string_slice(item.key[i]);
            printf("\"");
            if (i < NGRAM_LEN - 1) printf(", ");
        }
        printf("): %lu", item.value);
        printf(" }\n");
    }
}

bool is_space(char c) {
    return c == ' ';
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

    // MarkovChain m = {0};
    
    InputTokenMap input_map = {0};
    // InputTokenMapItem output_map = {0};

    StringSlice prev_tokens[NGRAM_LEN] = {0};
    size_t token_buffer_head = 0;
    size_t n_tokens = 0;
    size_t input_token_id = 0;
    size_t output_token_id = 0;

    int cursor = 0;
    int token_start = 0;

    while (cursor < sb.st_size) {
        while (is_space(data[cursor]) && cursor < sb.st_size) cursor++;
        token_start = cursor;
        while (!is_space(data[cursor]) && cursor < sb.st_size) cursor++;

        if (token_start < cursor) {
            n_tokens++;
            NGram input_token;
            size_t read_head = token_buffer_head;
            for (int i = 0; i < NGRAM_LEN; ++i) {
                input_token[i] = prev_tokens[read_head];
                read_head = (read_head + 1) % NGRAM_LEN;
            }

            // make sure to populate the ring buffer first!
            if (n_tokens < NGRAM_LEN) continue;

            StringSlice output_token = {
                .ptr = &data[token_start],
                .len = cursor - token_start
            };

            InputTokenMapItem input_item = {0};
            input_item.value = input_token_id;
            for (int i = 0; i < NGRAM_LEN; ++i) {
                input_item.key[i] = input_token[i];
            }

            if(!input_token_map_insert(&input_map, input_item)) input_token_id++;

            prev_tokens[token_buffer_head] = output_token;
            token_buffer_head = (token_buffer_head + 1) % NGRAM_LEN;
        }
    }

    input_token_map_print(&input_map);

    if (munmap(data, sb.st_size) != 0) return 1;
}
