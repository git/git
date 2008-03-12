/*
 * Some generic hashing helpers.
 */
#include "cache.h"
#include "hash.h"

/*
 * Look up a hash entry in the hash table. Return the pointer to
 * the existing entry, or the empty slot if none existed. The caller
 * can then look at the (*ptr) to see whether it existed or not.
 */
static struct hash_table_entry *lookup_hash_entry(unsigned int hash, const struct hash_table *table)
{
	unsigned int size = table->size, nr = hash % size;
	struct hash_table_entry *array = table->array;

	while (array[nr].ptr) {
		if (array[nr].hash == hash)
			break;
		nr++;
		if (nr >= size)
			nr = 0;
	}
	return array + nr;
}


/*
 * Insert a new hash entry pointer into the table.
 *
 * If that hash entry already existed, return the pointer to
 * the existing entry (and the caller can create a list of the
 * pointers or do anything else). If it didn't exist, return
 * NULL (and the caller knows the pointer has been inserted).
 */
static void **insert_hash_entry(unsigned int hash, void *ptr, struct hash_table *table)
{
	struct hash_table_entry *entry = lookup_hash_entry(hash, table);

	if (!entry->ptr) {
		entry->ptr = ptr;
		entry->hash = hash;
		table->nr++;
		return NULL;
	}
	return &entry->ptr;
}

static void grow_hash_table(struct hash_table *table)
{
	unsigned int i;
	unsigned int old_size = table->size, new_size;
	struct hash_table_entry *old_array = table->array, *new_array;

	new_size = alloc_nr(old_size);
	new_array = xcalloc(sizeof(struct hash_table_entry), new_size);
	table->size = new_size;
	table->array = new_array;
	table->nr = 0;
	for (i = 0; i < old_size; i++) {
		unsigned int hash = old_array[i].hash;
		void *ptr = old_array[i].ptr;
		if (ptr)
			insert_hash_entry(hash, ptr, table);
	}
	free(old_array);
}

void *lookup_hash(unsigned int hash, const struct hash_table *table)
{
	if (!table->array)
		return NULL;
	return lookup_hash_entry(hash, table)->ptr;
}

void **insert_hash(unsigned int hash, void *ptr, struct hash_table *table)
{
	unsigned int nr = table->nr;
	if (nr >= table->size/2)
		grow_hash_table(table);
	return insert_hash_entry(hash, ptr, table);
}

int for_each_hash(const struct hash_table *table, int (*fn)(void *))
{
	int sum = 0;
	unsigned int i;
	unsigned int size = table->size;
	struct hash_table_entry *array = table->array;

	for (i = 0; i < size; i++) {
		void *ptr = array->ptr;
		array++;
		if (ptr) {
			int val = fn(ptr);
			if (val < 0)
				return val;
			sum += val;
		}
	}
	return sum;
}

void free_hash(struct hash_table *table)
{
	free(table->array);
	table->array = NULL;
	table->size = 0;
	table->nr = 0;
}
