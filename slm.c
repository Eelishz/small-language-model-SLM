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
#define TOKEN_LIST_INITIAL_CAPACITY 10000
#define TOKEN_MAP_INITIAL_CAPACITY 4096*8
#define TOKEN_MAP_MAX_UTIL 0.80F
#define NGRAM_LEN 2

typedef uint16_t Token;

typedef struct {
    char *ptr;
    size_t len;
} StringSlice;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} Tokens;

typedef struct {
    size_t token_id;
    float p;
} MarkovChainItem;

typedef MarkovChainItem **MarkovChain;

typedef struct {
    StringSlice key;
    Token value;
} TokenMapItem;

typedef struct {
    TokenMapItem *items;
    size_t count;
    size_t capacity;
} TokenMap;

typedef struct {
    Token key[NGRAM_LEN];
    Token value;
    bool slot_used;
} NGramMapItem;

typedef struct {
    NGramMapItem *items;
    size_t count;
    size_t capacity;
} NGramMap;

typedef struct {
    StringSlice *items;
    size_t count;
    size_t capacity;
} IDToTokens;

typedef struct {
    uint8_t *ptr;
    size_t used;
    size_t capacity;
} Arena;

uint64_t int_hash(uint64_t state, Token data) {
    state ^= data;
    state *= 1099511628211ULL;
    return state;
}

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

NGramMapItem *ngram_map_get(NGramMap *m, Token key[NGRAM_LEN]) {
    uint64_t hash = int_hash(INITIAL_FNV1A_STATE, key[0]);
    for (size_t i = 0; i < NGRAM_LEN; ++i) {
        hash = int_hash(hash, key[i]);
    }
    size_t index = hash % m->capacity;

    if (!(m->items[index].slot_used)) return NULL;


    while (true) {
        if (!(m->items[index].slot_used)) return NULL;

        bool same_key = true;
        for (size_t i = 0; i < NGRAM_LEN; ++i) {
            same_key = same_key && (m->items[index].key[i] == key[i]);
        }

        if (same_key) {
            return &m->items[index];
        }
        index = (index + 1) % m->capacity;
    }
}

bool ngram_map_insert(NGramMap *m, NGramMapItem item) {
    assert(m);
    if (!m->items) {
        m->items = malloc(sizeof(TokenMapItem) * TOKEN_MAP_INITIAL_CAPACITY);
        assert(m->items);
        m->capacity = TOKEN_MAP_INITIAL_CAPACITY;
        memset(m->items, 0, m->capacity);
    }

    if ((float)m->count / (float)m->capacity > TOKEN_MAP_MAX_UTIL) {
        todo("reallocation");
    }

    uint64_t hash = int_hash(INITIAL_FNV1A_STATE, item.key[0]);
    for (size_t i = 0; i < NGRAM_LEN; ++i) {
        hash = int_hash(hash, item.key[i]);
    }
    size_t index = hash % m->capacity;

    if (!(m->items[index].slot_used)) {
        item.slot_used = true;
        m->items[index] = item;
        m->count++;
        return false;
    }

    bool same_key = true;
    for (size_t i = 0; i < NGRAM_LEN; ++i) {
        same_key = same_key && (m->items[index].key[i] == item.key[i]);
    }

    if (same_key) {
        // m->items[index] = item;
        return true;
    }

    while (m->items[index].slot_used) index = (index + 1) % m->capacity;

    item.slot_used = true;
    m->items[index] = item;
    m->count++;

    return false;
}

void ngram_map_print(NGramMap *m) {
    for (size_t i = 0; i < m->capacity; ++i) {
        NGramMapItem item = m->items[i];

        if(!item.slot_used) continue;

        printf("{ \"");
        for (size_t i = 0; i < NGRAM_LEN; ++i) {
            printf("%u", item.key[i]);
            if (i < NGRAM_LEN - 1) printf(", ");
        }
        printf("\": %u", item.value);
        printf(" }\n");
    }
}

TokenMapItem *token_map_get(TokenMap *m, StringSlice key) {
    uint64_t hash = fnv1a_hash(INITIAL_FNV1A_STATE,key.ptr, key.len);
    size_t index = hash % m->capacity;

    if (!m->items[index].key.ptr) return NULL;

    while (true) {
        if (!m->items[index].key.ptr) return NULL;

        if (slice_cmp(m->items[index].key, key)) {
            return &m->items[index];
        }
        index = (index + 1) % m->capacity;
    }
}

bool token_map_insert(TokenMap *m, TokenMapItem item) {
    assert(m);
    if (!m->items) {
        m->items = malloc(sizeof(TokenMapItem) * TOKEN_MAP_INITIAL_CAPACITY);
        assert(m->items);
        m->capacity = TOKEN_MAP_INITIAL_CAPACITY;
        memset(m->items, 0, m->capacity);
    }

    if ((float)m->count / (float)m->capacity > TOKEN_MAP_MAX_UTIL) {
        todo("reallocation");
    }

    uint64_t hash = fnv1a_hash(INITIAL_FNV1A_STATE, item.key.ptr, item.key.len);
    size_t index = hash % m->capacity;


    if (!m->items[index].key.ptr) {
        m->items[index] = item;
        m->count++;
        return false;
    }

    if (slice_cmp(m->items[index].key, item.key)) {
        // m->items[index] = item;
        return true;
    }

    while (m->items[index].key.ptr) index = (index + 1) % m->capacity;
    m->items[index] = item;
    m->count++;

    return false;
}

void token_map_print(TokenMap *m) {
    for (size_t i = 0; i < m->capacity; ++i) {
        TokenMapItem item = m->items[i];

        if(!item.key.ptr) continue;

        printf("{ \"");
        print_string_slice(item.key);
        printf("\": %u", item.value);
        printf(" }\n");
    }
}

void tokens_append(Tokens *tokens, Token item) {
    assert(tokens);
    if (!tokens->items) {
        tokens->items = malloc(sizeof(item) * TOKEN_LIST_INITIAL_CAPACITY);
        assert(tokens->items);
        tokens->capacity = TOKEN_LIST_INITIAL_CAPACITY;
    }

    if ((tokens->count + 1) >= tokens->capacity) {
        size_t new_capacity = tokens->capacity * 2;
        tokens->items = realloc(tokens->items, sizeof(item) * new_capacity);
        assert(tokens->items);
        tokens->capacity = new_capacity;
    }

    tokens->items[tokens->count++] = item;
}

void id_to_tokens_append(IDToTokens *tokens, StringSlice item) {
    assert(tokens);
    if (!tokens->items) {
        tokens->items = malloc(sizeof(item) * TOKEN_LIST_INITIAL_CAPACITY);
        assert(tokens->items);
        tokens->capacity = TOKEN_LIST_INITIAL_CAPACITY;
    }

    if ((tokens->count + 1) >= tokens->capacity) {
        size_t new_capacity = tokens->capacity * 2;
        tokens->items = realloc(tokens->items, sizeof(item) * new_capacity);
        assert(tokens->items);
        tokens->capacity = new_capacity;
    }

    tokens->items[tokens->count++] = item;
}

void arena_reserve(Arena *arena, size_t capacity) {
    arena->ptr = malloc(capacity);
    assert(arena->ptr);
    arena->capacity = capacity;
}

void *arena_allocate(Arena *arena, size_t size, size_t align) {
    uintptr_t current = (uintptr_t)(arena->ptr + arena->used);
    uintptr_t aligned = (current + (align - 1)) & ~(uintptr_t)(align - 1);
    size_t offset = aligned - (uintptr_t)arena->ptr;

    if (offset + size > arena->capacity) return NULL;

    arena->used = offset + size;
    return (void *)(arena->ptr + offset);
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

    Tokens tokens = {0};

    Token token_id = 0;
    Token ngram_id = 0;
    IDToTokens id_to_tokens = {0};
    TokenMap token_map = {0};
    NGramMap ngram_map = {0};

    int cursor = 0;
    int token_start = 0;

    while (cursor < sb.st_size) {
        while (is_space(data[cursor]) && cursor < sb.st_size) cursor++;
        token_start = cursor;
        while (!is_space(data[cursor]) && cursor < sb.st_size) cursor++;

        if (token_start < cursor) {
            StringSlice token = {
                .ptr = &data[token_start],
                .len = cursor - token_start
            };

            TokenMapItem map_item = {
                .key = token,
                .value = token_id
            };

            if(!token_map_insert(&token_map, map_item)) {
                id_to_tokens_append(&id_to_tokens, token);
                token_id++;
            }

            TokenMapItem *current_token = token_map_get(&token_map, token);
            assert(current_token);
            assert(slice_cmp(token, current_token->key));
            tokens_append(&tokens, current_token->value);

            if (tokens.count >= NGRAM_LEN) {
                NGramMapItem ngram_map_item = {0};
                ngram_map_item.value = ngram_id;

                for (size_t i = 0; i < NGRAM_LEN; ++i) {
                    ngram_map_item.key[i] = tokens.items[tokens.count - 1 - i];
                }

                if (!ngram_map_insert(&ngram_map, ngram_map_item)) {
                    ngram_id++;
                }
            }
        }
    }

    printf("Total tokens: %lu\n", tokens.count);
    printf("Unique tokens: %u\n", token_id + 1);
    printf("Unique N-grams: %u\n", ngram_id + 1);

    // printf("--- token map ---\n");
    // token_map_print(&token_map);
    // printf("--- ngram map ---\n");
    // ngram_map_print(&ngram_map);

    Arena arena = {0};
    arena_reserve(&arena, sizeof(MarkovChainItem) * (ngram_id + 1) * (token_id + 1));

    MarkovChain m = {0};

    for (size_t i = NGRAM_LEN + 1; i < tokens.count; ++i) {
        Token key[NGRAM_LEN] = {0};
        for (size_t ii = 0; ii < NGRAM_LEN; ++ii) {
            key[ii] = tokens.items[i - ii - NGRAM_LEN];
        }
        printf("%u, %u ", key[0], key[1]);
        fflush(stdout);

        NGramMapItem *ngram_item = ngram_map_get(&ngram_map, key);
        assert(ngram_item);
        printf("NGRAM_ID: %u\n", ngram_item->value);
    }

    printf("\n");

    if (munmap(data, sb.st_size) != 0) return 1;
}
