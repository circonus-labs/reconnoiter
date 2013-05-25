/*
 * $Id: linkhash.h,v 1.6 2006/01/30 23:07:57 mclark Exp $
 *
 * Copyright (c) 2004, 2005 Metaparadigm Pte. Ltd.
 * Michael Clark <michael@metaparadigm.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See COPYING for details.
 *
 */
 
#ifndef _linkhash_h_
#define _linkhash_h_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * golden prime used in hash functions
 */
#define LH_PRIME 0x9e370001UL

/**
 * sentinel pointer value for empty slots
 */
#define LH_EMPTY (void*)-1

/**
 * sentinel pointer value for freed slots
 */
#define LH_FREED (void*)-2

struct jl_lh_entry;

/**
 * callback function prototypes
 */
typedef void (jl_lh_entry_free_fn) (struct jl_lh_entry *e);
/**
 * callback function prototypes
 */
typedef unsigned long (jl_lh_hash_fn) (const void *k);
/**
 * callback function prototypes
 */
typedef int (jl_lh_equal_fn) (const void *k1, const void *k2);

/**
 * An entry in the hash table
 */
struct jl_lh_entry {
	/**
	 * The key.
	 */
	void *k;
	/**
	 * The value.
	 */
	const void *v;
	/**
	 * The next entry
	 */
	struct jl_lh_entry *next;
	/**
	 * The previous entry.
	 */
	struct jl_lh_entry *prev;
};


/**
 * The hash table structure.
 */
struct jl_lh_table {
	/**
	 * Size of our hash.
	 */
	int size;
	/**
	 * Numbers of entries.
	 */
	int count;

	/**
	 * Number of collisions.
	 */
	int collisions;

	/**
	 * Number of resizes.
	 */
	int resizes;

	/**
	 * Number of lookups.
	 */
	int lookups;

	/**
	 * Number of inserts.
	 */
	int inserts;

	/**
	 * Number of deletes.
	 */
	int deletes;

	/**
	 * Name of the hash table.
	 */
	const char *name;

	/**
	 * The first entry.
	 */
	struct jl_lh_entry *head;

	/**
	 * The last entry.
	 */
	struct jl_lh_entry *tail;

	struct jl_lh_entry *table;

	/**
	 * A pointer onto the function responsible for freeing an entry.
	 */
	jl_lh_entry_free_fn *free_fn;
	jl_lh_hash_fn *hash_fn;
	jl_lh_equal_fn *equal_fn;
};


/**
 * Pre-defined hash and equality functions
 */
extern unsigned long jl_lh_ptr_hash(const void *k);
extern int jl_lh_ptr_equal(const void *k1, const void *k2);

extern unsigned long jl_lh_char_hash(const void *k);
extern int jl_lh_char_equal(const void *k1, const void *k2);


/**
 * Convenience list iterator.
 */
#define jl_lh_foreach(table, entry) \
for(entry = table->head; entry; entry = entry->next)

/**
 * jl_lh_foreach_safe allows calling of deletion routine while iterating.
 */
#define jl_lh_foreach_safe(table, entry, tmp) \
for(entry = table->head; entry && ((tmp = entry->next) || 1); entry = tmp)



/**
 * Create a new linkhash table.
 * @param size initial table size. The table is automatically resized
 * although this incurs a performance penalty.
 * @param name the table name.
 * @param free_fn callback function used to free memory for entries
 * when jl_lh_table_free or jl_lh_table_delete is called.
 * If NULL is provided, then memory for keys and values
 * must be freed by the caller.
 * @param hash_fn  function used to hash keys. 2 standard ones are defined:
 * jl_lh_ptr_hash and jl_lh_char_hash for hashing pointer values
 * and C strings respectively.
 * @param equal_fn comparison function to compare keys. 2 standard ones defined:
 * jl_lh_ptr_hash and jl_lh_char_hash for comparing pointer values
 * and C strings respectively.
 * @return a pointer onto the linkhash table.
 */
extern struct jl_lh_table* jl_lh_table_new(int size, const char *name,
				     jl_lh_entry_free_fn *free_fn,
				     jl_lh_hash_fn *hash_fn,
				     jl_lh_equal_fn *equal_fn);

/**
 * Convenience function to create a new linkhash
 * table with char keys.
 * @param size initial table size.
 * @param name table name.
 * @param free_fn callback function used to free memory for entries.
 * @return a pointer onto the linkhash table.
 */
extern struct jl_lh_table* jl_lh_kchar_table_new(int size, const char *name,
					   jl_lh_entry_free_fn *free_fn);


/**
 * Convenience function to create a new linkhash
 * table with ptr keys.
 * @param size initial table size.
 * @param name table name.
 * @param free_fn callback function used to free memory for entries.
 * @return a pointer onto the linkhash table.
 */
extern struct jl_lh_table* jl_lh_kptr_table_new(int size, const char *name,
					  jl_lh_entry_free_fn *free_fn);


/**
 * Free a linkhash table.
 * If a callback free function is provided then it is called for all
 * entries in the table.
 * @param t table to free.
 */
extern void jl_lh_table_free(struct jl_lh_table *t);


/**
 * Insert a record into the table.
 * @param t the table to insert into.
 * @param k a pointer to the key to insert.
 * @param v a pointer to the value to insert.
 */
extern int jl_lh_table_insert(struct jl_lh_table *t, void *k, const void *v);


/**
 * Lookup a record into the table.
 * @param t the table to lookup
 * @param k a pointer to the key to lookup
 * @return a pointer to the record structure of the value or NULL if it does not exist.
 */
extern struct jl_lh_entry* jl_lh_table_lookup_entry(struct jl_lh_table *t, const void *k);

/**
 * Lookup a record into the table
 * @param t the table to lookup
 * @param k a pointer to the key to lookup
 * @return a pointer to the found value or NULL if it does not exist.
 */
extern const void* jl_lh_table_lookup(struct jl_lh_table *t, const void *k);


/**
 * Delete a record from the table.
 * If a callback free function is provided then it is called for the
 * for the item being deleted.
 * @param t the table to delete from.
 * @param e a pointer to the entry to delete.
 * @return 0 if the item was deleted.
 * @return -1 if it was not found.
 */
extern int jl_lh_table_delete_entry(struct jl_lh_table *t, struct jl_lh_entry *e);


/**
 * Delete a record from the table.
 * If a callback free function is provided then it is called for the
 * for the item being deleted.
 * @param t the table to delete from.
 * @param k a pointer to the key to delete.
 * @return 0 if the item was deleted.
 * @return -1 if it was not found.
 */
extern int jl_lh_table_delete(struct jl_lh_table *t, const void *k);


void jl_lh_abort(const char *msg, ...);
void jl_lh_table_resize(struct jl_lh_table *t, int new_size);

#ifdef __cplusplus
}
#endif

#endif
