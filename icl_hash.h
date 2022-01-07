/**
 * @file
 *
 * Header file for icl_hash routines.
 *
 */
/* $Id$ */
/* $UTK_Copyright: $ */

#ifndef icl_hash_h
#define icl_hash_h

#include <stdio.h>

#define BITS_IN_int     ( sizeof(int) * CHAR_BIT )
#define THREE_QUARTERS  ((int) ((BITS_IN_int * 3) / 4))
#define ONE_EIGHTH      ((int) (BITS_IN_int / 8))
#define HIGH_BITS       ( ~((unsigned int)(~0) >> ONE_EIGHTH ))

typedef struct icl_entry_s {
    void* key;
    void *data;
    struct icl_entry_s* next;
} icl_entry_t;

typedef struct icl_hash_s {
    int nbuckets;
    int nentries;
    icl_entry_t **buckets;
    unsigned int (*hash_function)(void*);
    int (*hash_key_compare)(void*, void*);
} icl_hash_t;

/**
 * Create a new hash table.
 *
 * @param[in] nbuckets -- number of buckets to create
 * @param[in] hash_function -- pointer to the hashing function to be used
 * @param[in] hash_key_compare -- pointer to the hash key comparison function to be used
 *
 * @returns pointer to new hash table.
 */
icl_hash_t* icl_hash_create( int nbuckets, unsigned int (*hash_function)(void*), int (*hash_key_compare)(void*, void*) );

/**
 * Search for an entry in a hash table.
 *
 * @param ht -- the hash table to be searched
 * @param key -- the key of the item to search for
 *
 * @returns pointer to the data corresponding to the key.
 *   If the key was not found, returns NULL.
 */
void* icl_hash_find(icl_hash_t *, void* );

/**
 * Insert an item into the hash table.
 *
 * @param ht -- the hash table
 * @param key -- the key of the new item
 * @param data -- pointer to the new item's data
 *
 * @returns pointer to the new item.  Returns NULL on error.
 */
icl_entry_t* icl_hash_insert(icl_hash_t *, void*, void *);

//bug in update, aggiunte le funzioni di free
/**
 * Replace entry in hash table with the given entry.
 *
 * @param ht -- the hash table
 * @param key -- the key of the new item
 * @param data -- pointer to the new item's data
 * @param olddata -- pointer to the old item's data (set upon return)
 *
 * @returns pointer to the new item.  Returns NULL on error.
 */
icl_entry_t* icl_hash_update_insert(icl_hash_t *, void*, void *, void **, void (*free_key)(void*), void (*free_data)(void*));

/**
 * Free hash table structures (key and data are freed using functions).
 *
 * @param ht -- the hash table to be freed
 * @param free_key -- pointer to function that frees the key
 * @param free_data -- pointer to function that frees the data
 *
 * @returns 0 on success, -1 on failure.
 */
int icl_hash_destroy(icl_hash_t *, void (*)(void*), void (*)(void*));

/**
 * Dump the hash table's contents to the given file pointer.
 *
 * @param stream -- the file to which the hash table should be dumped
 * @param ht -- the hash table to be dumped
 *
 * @returns 0 on success, -1 on failure.
 */
int icl_hash_dump(FILE *, icl_hash_t *);

/**
 * Free one hash table entry located by key (key and data are freed using functions).
 *
 * @param ht -- the hash table to be freed
 * @param key -- the key of the new item
 * @param free_key -- pointer to function that frees the key
 * @param free_data -- pointer to function that frees the data
 *
 * @returns 0 on success, -1 on failure.
 */
int icl_hash_delete( icl_hash_t *ht, void* key, void (*free_key)(void*), void (*free_data)(void*) );

#define icl_hash_foreach(ht, tmpint, tmpent, kp, dp)    \
    for (tmpint=0;tmpint<ht->nbuckets; tmpint++)        \
        for (tmpent=ht->buckets[tmpint];                                \
             tmpent!=NULL&&((kp=tmpent->key)!=NULL)&&((dp=tmpent->data)!=NULL); \
             tmpent=tmpent->next)

#endif /* icl_hash_h */
