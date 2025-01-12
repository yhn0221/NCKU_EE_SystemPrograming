#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash.h"

unsigned int hashFunc(const char *key, size_t tableSize)
{
    unsigned long hashValue = 5381;
    int c;
    while ((c = *key++))
    {
        hashValue = ((hashValue << 5) + hashValue) + c; // hashValue * 33 + c
    }
    return (unsigned int)(hashValue % tableSize);
}

HashTable *createHashTable(size_t tableSize)
{
    HashTable *ht = (HashTable *)malloc(sizeof(HashTable));
    if (!ht)
    {
        perror("Failed to create hash table");
        return NULL;
    }
    ht->buckets = (Entry **)calloc(tableSize, sizeof(Entry *));
    if (!ht->buckets)
    {
        perror("Failed to create buckets in hash table");
        free(ht);
        return NULL;
    }
    ht->tableSize = tableSize;
    return ht;
}

void insertHashTable(HashTable *ht, const char *key, void *value)
{
    unsigned long index = hashFunc(key, ht->tableSize);
    Entry *newEntry = (Entry *)malloc(sizeof(Entry));
    if (!newEntry)
    {
        perror("Failed to allocate memory for hash entry");
        return;
    }
    strncpy(newEntry->key, key, 10);
    newEntry->key[9] = '\0';
    newEntry->value = value;
    newEntry->next = ht->buckets[index];
    ht->buckets[index] = newEntry;
}

bool searchHashTable(HashTable *ht, const char *key, void **value)
{
    unsigned long index = hashFunc(key, ht->tableSize);
    Entry *current = ht->buckets[index];
    while (current)
    {
        if (strncmp(current->key, key, 10) == 0)
        {
            if (value)
            {
                *value = current->value;
            }
            return true;
        }
        current = current->next;
    }
    return false;
}

void freeHashTable(HashTable *ht, void (*free_value)(void *))
{
    size_t i;
    for (i = 0; i < ht->tableSize; i++)
    {
        Entry *current = ht->buckets[i];
        while (current)
        {
            Entry *temp = current;
            current = current->next;
            if (free_value)
            {
                free_value(temp->value);
            }
            free(temp);
        }
    }
    free(ht->buckets);
    free(ht);
}
