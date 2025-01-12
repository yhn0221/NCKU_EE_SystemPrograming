#ifndef HASH_H
#define HASH_H

#include <stdbool.h>
#define MAX_KEY_LENGTH 10
typedef struct Entry
{
    char key[MAX_KEY_LENGTH];
    void *value;        // Could be an address or an opcode
    struct Entry *next; // For handling collisions via chaining
} Entry;

typedef struct Hashtable
{
    Entry **buckets; // Each bucket is a linked list
    size_t tableSize;
} HashTable;

unsigned int hashFunc(const char *key, size_t tableSize);
HashTable *createHashTable(size_t tableSize);
void insertHashTable(HashTable *ht, const char *key, void *value);
bool searchHashTable(HashTable *ht, const char *key, void **value);
void freeHashTable(HashTable *ht, void (*free_value)(void *));
#endif
