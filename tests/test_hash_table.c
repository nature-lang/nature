#include "test.h"
#include <stdio.h>
#include <uv.h>
#include <string.h>
#include "utils/uthash.h"
#include "utils/table.h"
#include "utils/sc_map.h"

#define TEST_SIZE 1000000

// Structure for uthash
typedef struct {
    char key[32];
    int value;
    UT_hash_handle hh;
} hash_item;

static void test_uthash_performance() {
    hash_item *hash_table = NULL;
    uint64_t start_time, end_time;

    // Test uthash insertion
    start_time = uv_hrtime();
    for (int i = 0; i < TEST_SIZE; i++) {
        hash_item *item = (hash_item *) malloc(sizeof(hash_item));
        snprintf(item->key, sizeof(item->key), "key%d", i);
        item->value = i;
        HASH_ADD_STR(hash_table, key, item);
    }
    end_time = uv_hrtime();
    printf("uthash: insert %d items took %.2f ms\n",
           TEST_SIZE, (end_time - start_time) / 1000000.0);

    // Test uthash lookup
    start_time = uv_hrtime();
    for (int i = 0; i < TEST_SIZE; i++) {
        hash_item *item;
        char key[32];
        snprintf(key, sizeof(key), "key%d", i);
        HASH_FIND_STR(hash_table, key, item);
    }
    end_time = uv_hrtime();
    printf("uthash: lookup %d items took %.2f ms\n",
           TEST_SIZE, (end_time - start_time) / 1000000.0);

    // Cleanup
    hash_item *current, *tmp;
    HASH_ITER(hh, hash_table, current, tmp) {
        HASH_DEL(hash_table, current);
        free(current);
    }
}

static void test_table_performance() {
    uint64_t start_time, end_time;
    table_t *table = table_new();

    // Test table.h insertion
    start_time = uv_hrtime();
    for (int i = 0; i < TEST_SIZE; i++) {
        char *key = (char *) malloc(32);
        snprintf(key, 32, "key%d", i);
        int *value = (int *) malloc(sizeof(int));
        *value = i;
        table_set(table, key, value);
    }
    end_time = uv_hrtime();
    printf("table.h: insert %d items took %.2f ms\n",
           TEST_SIZE, (end_time - start_time) / 1000000.0);

    // Test table.h lookup
    start_time = uv_hrtime();
    for (int i = 0; i < TEST_SIZE; i++) {
        char key[32];
        snprintf(key, 32, "key%d", i);
        table_get(table, key);
    }
    end_time = uv_hrtime();
    printf("table.h: lookup %d items took %.2f ms\n",
           TEST_SIZE, (end_time - start_time) / 1000000.0);

    // Cleanup
    table_free(table);
}

static void test_scmap_performance() {
    uint64_t start_time, end_time;
    struct sc_map_str map;
    sc_map_init_str(&map, 0, 0);

    // Test sc_map insertion
    start_time = uv_hrtime();
    for (int i = 0; i < TEST_SIZE; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "%d", i);
        sc_map_put_str(&map, strdup(key), strdup(value));
    }
    end_time = uv_hrtime();
    printf("sc_map: insert %d items took %.2f ms\n",
           TEST_SIZE, (end_time - start_time) / 1000000.0);

    // Test sc_map lookup
    start_time = uv_hrtime();
    for (int i = 0; i < TEST_SIZE; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key%d", i);
        sc_map_get_str(&map, key);
    }
    end_time = uv_hrtime();
    printf("sc_map: lookup %d items took %.2f ms\n",
           TEST_SIZE, (end_time - start_time) / 1000000.0);

    // Cleanup
    const char *key, *value;
    sc_map_foreach(&map, key, value) {
        free((void*)key);
        free((void*)value);
    }
    sc_map_term_str(&map);
}

static void test_scmap_int64_performance() {
    uint64_t start_time, end_time;
    struct sc_map_64 map;
    sc_map_init_64(&map, 0, 0);

    // Test sc_map_64 insertion
    start_time = uv_hrtime();
    for (int i = 0; i < TEST_SIZE; i++) {
        uint64_t key = i;
        sc_map_put_64(&map, key, i);
    }
    end_time = uv_hrtime();
    printf("sc_map_64: insert %d items took %.2f ms\n",
           TEST_SIZE, (end_time - start_time) / 1000000.0);

    // Test sc_map_64 lookup
    start_time = uv_hrtime();
    for (int i = 0; i < TEST_SIZE; i++) {
        uint64_t key = i;
        sc_map_get_64(&map, key);
    }
    end_time = uv_hrtime();
    printf("sc_map_64: lookup %d items took %.2f ms\n",
           TEST_SIZE, (end_time - start_time) / 1000000.0);

    // Cleanup
    sc_map_term_64(&map);
}

int main(void) {
    printf("\n=== Testing uthash performance ===\n");
    test_uthash_performance();

    printf("\n=== Testing table.h performance ===\n");
    test_table_performance();

    printf("\n=== Testing sc_map performance ===\n");
    test_scmap_performance();

    printf("\n=== Testing sc_map_64 performance ===\n");
    test_scmap_int64_performance();

    return 0;
}