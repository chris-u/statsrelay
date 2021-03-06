#include <stdlib.h>
#include <string.h>
#include "hashmap.h"
#include "hashlib.h"

#define MAX_CAPACITY 0.75
#define DEFAULT_CAPACITY 128

// Basic hash entry.
typedef struct hashmap_entry {
    size_t key_len;
    char *key;
    void *value;
    void *metadata;
    struct hashmap_entry *next; // Support linking.
} hashmap_entry;

struct hashmap {
    int count;      // Number of entries
    int table_size; // Size of table in nodes
    int max_size;   // Max size before we resize
    hashmap_entry *table; // Pointer to an array of hashmap_entry objects
};

/**
 * Creates a new hashmap and allocates space for it.
 * @arg initial_size The minimim initial size. 0 for default (64).
 * @arg map Output. Set to the address of the map
 * @return 0 on success.
 */
int hashmap_init(int initial_size, hashmap **map) {
    // Default to 64 if no size
    if (initial_size <= 0) {
        initial_size = DEFAULT_CAPACITY;

        // Round up to power of 2
    } else {
        int most_sig_bit = 0;
        for (int idx = 0; idx < sizeof(initial_size) * 8; idx++) {
            if ((initial_size >> idx) & 0x1)
                most_sig_bit = idx;
        }

        // Round up if not a power of 2
        if ((1 << most_sig_bit) != initial_size) {
            most_sig_bit += 1;
        }
        initial_size = 1 << most_sig_bit;
    }

    // Allocate the map
    hashmap *m = calloc(1, sizeof(hashmap));
    m->table_size = initial_size;
    m->max_size = MAX_CAPACITY * initial_size;

    // Allocate the table
    m->table = (hashmap_entry *) calloc(initial_size, sizeof(hashmap_entry));

    // Return the table
    *map = m;
    return 0;
}

/**
 * Destroys a map and cleans up all associated memory
 * @arg map The hashmap to destroy. Frees memory.
 */
int hashmap_destroy(hashmap *map) {
    // Free each entry
    hashmap_entry *entry, *old;
    int in_table;
    for (int i = 0; i < map->table_size; i++) {
        entry = map->table + i;
        in_table = 1;
        while (entry && entry->key) {
            // Walk the next links
            old = entry;
            entry = entry->next;

            // Clear the objects
            free(old->key);

            // The initial entry is in the table
            // and we should not free that one.
            if (!in_table) {
                free(old);
            }
            in_table = 0;
        }
    }

    // Free the table and hash map
    free(map->table);
    free(map);
    return 0;
}

/**
 * Returns the size of the hashmap in items
 */
int hashmap_size(hashmap *map) {
    return map->count;
}

/**
 * Returns the hash entry for key.
 */
static hashmap_entry *hashmap_index(hashmap_entry *table, int table_size,
                                    const char *key, size_t key_len) {
    // Mod the lower 64bits of the hash function with the table
    // size to get the index
    return table + (uint32_t)(murmur3_32(key, key_len, 0) % table_size);
}

/**
 * Returns if key equals the hashmap entry key.
 */
static inline int hashmap_keys_equal(const hashmap_entry *entry, const char *key,
                                     size_t key_len) {
    return entry->key_len == key_len && memcmp(entry->key, key, key_len) == 0;
}

/**
 * Gets a value.
 * @arg key The key to look for
 * @arg key_len The key length
 * @arg value Output. Set to the value of th key.
 * 0 on success. -1 if not found.
 */
int hashmap_get(hashmap *map, const char *key, void **value) {
    // Compute the hash value of the key
    const size_t key_len = strlen(key);
    hashmap_entry *entry = hashmap_index(map->table, map->table_size, key, key_len);

    // Scan the keys
    while (entry && entry->key) {
        // Found it
        if (hashmap_keys_equal(entry, key, key_len)) {
            *value = entry->value;
            return 0;
        }

        // Walk the chain
        entry = entry->next;
    }

    // Not found
    return -1;
}

/**
 * Internal method to insert into a hash table
 * @arg table The table to insert into
 * @arg table_size The size of the table
 * @arg key The key to insert
 * @arg key_len The length of the key
 * @arg value The value to associate
 * @arg metadata
 * @arg should_cmp Should keys be compared to existing ones.
 * @return 1 if the key is new, 0 if updated.
 */
static int hashmap_insert_table(hashmap_entry *table,
        int table_size,
        const char *key,
        int key_len,
        void *value,
        void *metadata,
        int should_cmp) {
    // Look for an entry
    hashmap_entry *entry = hashmap_index(table, table_size, key, key_len);
    // last_entry governs if we saw any nodes with keys
    hashmap_entry *last_entry = NULL;

    // Scan the keys
    while (entry && entry->key) {
        // Found it, update the value
        if (should_cmp && hashmap_keys_equal(entry, key, key_len)) {
            entry->value = value;
            return 0;
        }

        // Walk the chain
        last_entry = entry;
        entry = entry->next;
    }

    if (entry == NULL && last_entry == NULL) {
        return -1;
    }

    // We already know the key length - no point using strdup
    char *insert_key = malloc(key_len + 1);
    if (insert_key == NULL) {
        return -1;
    }
    memcpy(insert_key, key, key_len + 1);

    // If last entry is NULL, we can just insert directly into the
    // table slot since it is empty
    if (last_entry == NULL) {
        entry->key_len = key_len;
        entry->key = insert_key;
        entry->value = value;
        entry->metadata = metadata;
        entry->next = NULL;

    // We have a last value, need to link against it with our new
    // value.
    } else {
        entry = calloc(1, sizeof(hashmap_entry));
        if (entry == NULL) {
            free(insert_key);
            return -1;
        }
        entry->key_len = key_len;
        entry->key = insert_key;
        entry->value = value;
        entry->metadata = metadata;
        last_entry->next = entry;
    }
    return 1;
}

/**
 * Internal method to double the size of a hashmap
 */
static void hashmap_double_size(hashmap *map) {
    // Calculate the new sizes
    int new_size = map->table_size * 2;
    int new_max_size = map->max_size * 2;

    // Allocate the table
    hashmap_entry *new_table = (hashmap_entry *) calloc(new_size, sizeof(hashmap_entry));

    // Move each entry
    hashmap_entry *entry, *old;
    int in_table;
    for (int i = 0; i < map->table_size; i++) {
        entry = map->table + i;
        in_table = 1;
        while (entry && entry->key) {
            // Walk the next links
            old = entry;
            entry = entry->next;

            // Insert the value in the new map
            // Do not compare keys or duplicate since we are just doubling our
            // size, and we have unique keys and duplicates already.
            hashmap_insert_table(new_table, new_size, old->key, strlen(old->key),
                                 old->value, old->metadata, 0);

            // The initial entry is in the table
            // and we should not free that one.
            if (!in_table) {
                free(old);
            }
            in_table = 0;
        }
    }

    // Free the old table
    free(map->table);

    // Update the pointers
    map->table = new_table;
    map->table_size = new_size;
    map->max_size = new_max_size;
}

/**
 * Puts a key/value pair. Replaces existing values.
 * @arg key The key to set. This is copied, and a seperate
 * version is owned by the hashmap. The caller the key at will.
 * @notes This method is not thread safe.
 * @arg key_len The key length
 * @arg value The value to set.
 * @arg metadata arbitrary information
 * 0 if updated, 1 if added.
 */
int hashmap_put(hashmap *map, const char *key, void *value, void *metadata) {
    // Check if we need to double the size
    if (map->count + 1 > map->max_size) {
        // Doubles the size of the hashmap, re-try the insert
        hashmap_double_size(map);
        return hashmap_put(map, key, value, metadata);
    }

    // Insert into the map, comparing keys and duplicating keys
    int new = hashmap_insert_table(map->table, map->table_size, key,
                                   strlen(key), value, metadata, 1);
    if (new > 0) {
        map->count += 1;
    }
    return new;
}

/**
 * Deletes a key/value pair.
 * @notes This method is not thread safe.
 * @arg key The key to delete
 * 0 on success. -1 if not found.
 */
int hashmap_delete(hashmap *map, const char *key) {
    // Compute the hash value of the key
    const size_t key_len = strlen(key);
    hashmap_entry *entry = hashmap_index(map->table, map->table_size, key, key_len);
    hashmap_entry *last_entry = NULL;

    // Scan the keys
    while (entry && entry->key) {
        // Found it
        if (hashmap_keys_equal(entry, key, key_len)) {
            // Free the key
            free(entry->key);
            map->count -= 1;

            // Check if we are in the table
            if (last_entry == NULL) {
                // Copy the keys from the node, and free it
                if (entry->next) {
                    hashmap_entry *n = entry->next;
                    entry->key_len = n->key_len;
                    entry->key = n->key;
                    entry->value = n->value;
                    entry->next = n->next;
                    entry->metadata = n->metadata;
                    free(n);

                    // Zero everything out
                } else {
                    entry->key_len = 0;
                    entry->key = NULL;
                    entry->value = NULL;
                    entry->next = NULL;
                    entry->metadata = NULL;
                }

            } else {
                // Skip us in the list
                last_entry->next = entry->next;

                // Free ourself
                free(entry);
            }
            return 0;
        }
        last_entry = entry;
        // Walk the chain
        entry = entry->next;
    }

    // Not found
    return -1;
}

/**
 * Clears all the key/value pairs.
 * @notes This method is not thread safe.
 * 0 on success. -1 if not found.
 */
int hashmap_clear(hashmap *map) {
    hashmap_entry *entry, *old;
    int in_table;
    for (int i = 0; i < map->table_size; i++) {
        entry = map->table + i;
        in_table = 1;
        while (entry && entry->key) {
            // Walk the next links
            old = entry;
            entry = entry->next;

            // Clear the objects
            free(old->key);

            // The initial entry is in the table
            // and we should not free that one.
            if (!in_table) {
                free(old);
            } else {
                old->key = NULL;
            }
            in_table = 0;
        }
    }

    // Reset the sizes
    map->count = 0;
    return 0;
}

/**
 * Iterates through the key/value pairs in the map, invoking a
 * callback for each. The call back gets a key, value for each and
 * returns an integer stop value.  If the callback returns
 * HASHMAP_ITER_STOP, then the iteration stops. The current key can be
 * removed by return HASHMAP_ITER_DELETE.
 *
 * @arg map The hashmap to iterate over
 * @arg cb The callback function to invoke
 * @arg data Opaque handle passed to the callback
 * @return 0 on success
 */
int hashmap_iter(hashmap *map, hashmap_callback cb, void *data) {
    hashmap_entry *entry;

    int should_break = 0;
    for (int i = 0; i < map->table_size && should_break != 1; i++) {

        hashmap_entry *last_entry = NULL;
        entry = map->table + i;

        while (entry != NULL && entry->key && !should_break) {

            int cb_ret = cb(data, entry->key, entry->value, entry->metadata);
            if (cb_ret == HASHMAP_ITER_DELETE) { /* Delete this node */
                /* Free the key buffer */
                free(entry->key);
                entry->key = NULL;
                /**
                 * decrement item count to reflect delete completion
                 */
                map->count -= 1;

                if (last_entry == NULL) {
                    /* This is the first item in the table, we zip
                     * left to right and copy next to the first slot */
                    if (entry->next) {
                        hashmap_entry *n = entry->next;
                        entry->key = n->key;
                        entry->value = n->value;
                        entry->next = n->next;
                        entry->metadata = n->metadata;
                        free(n);
                        /* Do not record last_entry - we moved
                         * everything one left, and could do so
                         * again */

                    } else {
                        /* This is the only item in the chain, zero
                         * the item slot with no zipping */
                        entry->key = NULL;
                        entry->value = NULL;
                        entry->next = NULL;
                        entry->metadata = NULL;
                    }
                } else {
                    /* We are in the middle or end of a chain, remove
                     * the element and advance forward one */
                    last_entry->next = entry->next;
                    free(entry);
                    entry = last_entry->next;
                    /* Do not move last_entry - it didn't change */
                }
            } else {
                /* No delete, move forward in the list */
                should_break = cb_ret;
                /* Move last_entry to allow removals in the list */
                last_entry = entry;
                entry = entry->next;
            }


        }
    }
    return should_break;
}
