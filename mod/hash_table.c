/**
 * @file
 * A generic hash table implementation. Its design is largely based off Java's java.util.HashMap.
 * One difference is that the internal array does not resize. One important similarity is that it
 * is not synchronized.
 *
 * Uses the kernel's hlist internally.
 * We're not using hlist directly because it implies a lot of code rewriting (eg. the entry
 * retrieval function; "get") and we need at least four different hash tables.
 *
 * Because C does not support templates or generics, you have to set a number of macros and then
 * include this file. These are the macros:
 * @macro HTABLE_NAME name of the hash table structure to generate. Optional; Default: hash_table.
 * @macro KEY_TYPE data type of the table's keys.
 * @macro VALUE_TYPE data type of the table's values.
 * @macro HASH_TABLE_SIZE The size of the internal array, in slots. Optional;
 *		Default = Max = 64k - 1.
 * @macro GENERATE_PRINT just define it if you want the print function; otherwise it will not be
 *		generated.
 * @macro GENERATE_FOR_EACH just define it if you want the for_each function; otherwise it will not
 *		be generated.
 *
 * This module contains no header file; it needs to be #included directly.
 */

#include "nat64/comm/types.h"
#include <linux/slab.h>

/********************************************
 * Macros.
 ********************************************/

#ifndef HTABLE_NAME
#define HTABLE_NAME hash_table
#endif

#ifndef HASH_TABLE_SIZE
#define HASH_TABLE_SIZE (64 * 1024 - 1)
#endif

/** Creates a token name by concatenating prefix and suffix. */
#define CONCAT_AUX(prefix, suffix) prefix ## suffix
/** Seems useless, but if not present, the compiler won't expand the HTABLE_NAME macro... */
#define CONCAT(prefix, suffix) CONCAT_AUX(prefix, suffix)

/** The name of the key-value structure. */
#define KEY_VALUE_PAIR	CONCAT(HTABLE_NAME, _key_value)
/** The name of the init function. */
#define INIT			CONCAT(HTABLE_NAME, _init)
/** The name of the put function. */
#define PUT				CONCAT(HTABLE_NAME, _put)
/** The name of the get function. */
#define GET				CONCAT(HTABLE_NAME, _get)
/** The name of the remove function. */
#define REMOVE			CONCAT(HTABLE_NAME, _remove)
/** The name of the empty function. */
#define EMPTY			CONCAT(HTABLE_NAME, _empty)
/** The name of the auxiliary get function. */
#define GET_AUX			CONCAT(HTABLE_NAME, _get_aux)
/** The name of the print function. */
#define PRINT			CONCAT(HTABLE_NAME, _print)
/** The name of the for_each function. */
#define FOR_EACH		CONCAT(HTABLE_NAME, _for_each)

/********************************************
 * Structures.
 ********************************************/

/** The hash table. */
struct HTABLE_NAME {
	/**
	 * The array of linked lists.
	 * Each of these contains the values mapped to its index's hash code.
	 */
	struct hlist_head table[HASH_TABLE_SIZE];

	/** Used to locate the slot (within the linked list) of a value. */
	bool (*equals_function)(KEY_TYPE *, KEY_TYPE *);
	/** Used locate the linked list (within the array) of a value. */
	__u16 (*hash_function)(KEY_TYPE *);
};

/** Every entry in the table; the key used to access the value and the value. */
struct KEY_VALUE_PAIR {
	/** Dictates where in the table the value is. */
	KEY_TYPE *key;
	/** The value the user wants to store in the table. */
	VALUE_TYPE *value;
	/** Other key-values chained with this one (see: HTABLE_NAME.table). */
	struct hlist_node nodes;
};

/********************************************
 * Private "methods".
 ********************************************/

/**
 * Returns the key-value mapped to the "key" key within the table.
 *
 * To be used by hash table functions; outside code should use GET instead.
 *
 * @param table hash table instance you want the key-value from.
 * @param key descriptor to which the associated key-value is to be returned.
 * @return the key-value to which "table" maps "key", "null" if there's no mapping for the key.
 */
static struct KEY_VALUE_PAIR *GET_AUX(struct HTABLE_NAME *table, KEY_TYPE *key)
{
	struct hlist_node *current_node;
	struct KEY_VALUE_PAIR *current_pair;
	__u16 hash_code;

	if (!table) {
		log_err(ERR_NULL, "The table is NULL.");
		return false;
	}

	hash_code = table->hash_function(key) % HASH_TABLE_SIZE;
	hlist_for_each(current_node, &table->table[hash_code]) {
		current_pair = list_entry(current_node, struct KEY_VALUE_PAIR, nodes);
		if (table->equals_function(key, current_pair->key))
			return current_pair;
	}

	return NULL;
}

/********************************************
 * "Public" "methods".
 ********************************************/

/**
 * Readies "table" for future use.
 *
 * @param table the HTABLE_NAME instance you want to initialize.
 * @param equals_function function the table will use to locate slots.
 * @param hash_function function the table will use to locate linked lists.
 */
static int INIT(struct HTABLE_NAME *table,
		bool (*equals_function)(KEY_TYPE *, KEY_TYPE *),
		__u16 (*hash_function)(KEY_TYPE *))
{
	__u16 i;

	if (!table) {
		log_err(ERR_NULL, "The table is NULL.");
		return -EINVAL;
	}
	if (!equals_function) {
		log_err(ERR_NULL, "The equals function is NULL.");
		return -EINVAL;
	}
	if (!hash_function) {
		log_err(ERR_NULL, "The hash code function is NULL.");
		return -EINVAL;
	}

	for (i = 0; i < HASH_TABLE_SIZE; i++)
		INIT_HLIST_HEAD(&table->table[i]);

	table->equals_function = equals_function;
	table->hash_function = hash_function;

	return 0;
}

/**
 * Inserts "value" to the "table" table in the slot described by the "key" key.
 *
 * Important: The table stores pointers to (as opposed to "copies of") both key and value.
 * So please consider that neither must be released from memory after the call to this function.
 *
 * Also important: This function differs from HashMap.put() in that it doesn't validate whether the
 * value is already in the table before inserting.
 *
 * @param table the HTABLE_NAME instance you want to insert a value to.
 * @param key descriptor of the slot to place "value" in.
 * @param value element to store in the table.
 * @return success status. The value will not be inserted if a kmalloc fails.
 */
static int PUT(struct HTABLE_NAME *table, KEY_TYPE *key, VALUE_TYPE *value)
{
	struct KEY_VALUE_PAIR *key_value;
	__u16 hash_code;

	if (!table) {
		log_err(ERR_NULL, "The table is NULL.");
		return -EINVAL;
	}

	/*
	 * We're not going to insert the value alone, but a key-value structure.
	 * (Because we'll later need the key available during lookups.)
	 * We generate it here.
	 */
	key_value = kmalloc(sizeof(struct KEY_VALUE_PAIR), GFP_ATOMIC);
	if (!key_value) {
		log_err(ERR_ALLOC_FAILED, "Could not allocate the key-value struct.");
		return -ENOMEM;
	}
	key_value->key = key;
	key_value->value = value;

	/* Insert the key-value to the table. */
	hash_code = table->hash_function(key) % HASH_TABLE_SIZE;
	hlist_add_head(&key_value->nodes, &table->table[hash_code]);

	return 0;
}

/**
 * Returns from "table" the value mapped to the "key" key, if available.
 *
 * You will receive the actual stored value. Please don't release it from memory (use the REMOVE
 * function instead).
 *
 * @param table the HTABLE_NAME instance you want the value from.
 * @param key descriptor to which the associated value is to be returned.
 * @return the value to which "table" maps "key", "null" if there's no mapping for the key.
 */
static VALUE_TYPE *GET(struct HTABLE_NAME *table, KEY_TYPE *key)
{
	struct KEY_VALUE_PAIR *key_value = GET_AUX(table, key);
	return (key_value != NULL) ? key_value->value : NULL;
}

/**
 * Stops "key" from accesing its value in the "table" table.
 * Releases memory as well, depending on the release_* arguments.
 *
 * @param table the HTABLE_NAME instance you want to stop mapping "key" from.
 * @param key descriptor whose associated value will be removed from "table".
 * @param release_key send "true" if the key stored in the table should be released from memory.
 * @param release_value send "true" if the value stored in the table should be released from memory.
 */
static bool REMOVE(struct HTABLE_NAME *table, KEY_TYPE *key, bool release_key, bool release_value)
{
	struct KEY_VALUE_PAIR *key_value = GET_AUX(table, key);
	if (key_value == NULL)
		return false;

	hlist_del(&key_value->nodes);

	if (release_key)
		kfree(key_value->key);
	if (release_value)
		kfree(key_value->value);
	kfree(key_value);

	return true;
}

/**
 * Clears all memory allocated by the table. You definitely want to call this before your table goes
 * into oblivion!!!
 *
 * @param table the HTABLE_NAME instance you want to clear.
 * @param release_keys send "true" if the table's stored keys should be deallocated.
 * @param release_values send "true" if the table's stored keys should be deallocated.
 *
 * Note that even if you want to release the keys and the values, you still need to call this
 * function since you have no control over the key-value pairs.
 */
static void EMPTY(struct HTABLE_NAME *table, bool release_keys, bool release_values)
{
	struct hlist_node *current_node;
	struct KEY_VALUE_PAIR *current_pair;
	__u16 row;

	if (!table) {
		log_err(ERR_NULL, "The table is NULL.");
		return;
	}

	for (row = 0; row < HASH_TABLE_SIZE; row++) {
		while (!hlist_empty(&table->table[row])) {
			current_node = table->table[row].first;
			current_pair = container_of(current_node, struct KEY_VALUE_PAIR, nodes);

			hlist_del(current_node);

			if (release_keys)
				kfree(current_pair->key);
			if (release_values)
				kfree(current_pair->value);
			kfree(current_pair);

			/* log_debug("Deleted a node whose hash code was %u.", row); */
		}
	}
}

#ifdef GENERATE_PRINT
/**
 * Printks the content of the table in KERN_DEBUG level.
 * Use for debugging purposes.
 *
 * @param the HTABLE_NAME instance you want to print.
 * @param header a header label for the table. Will precede the table so you can locate it in dmesg
 *		or something.
 */
static void PRINT(struct HTABLE_NAME *table, char *header)
{
	struct hlist_node *current_node;
	struct KEY_VALUE_PAIR *current_pair;
	__u16 row;

	log_debug("** Printing table: %s **", header);

	if (!table)
		goto end;
	for (row = 0; row < HASH_TABLE_SIZE; row++) {
		hlist_for_each(current_node, &table->table[row]) {
			current_pair = hlist_entry(current_node, struct KEY_VALUE_PAIR, nodes);
			log_debug("  hash:%u - key:%p - value:%p", row, &current_pair->key,
					&current_pair->value);
		}
	}

	/* Fall through.*/
end:
	log_debug("** End of table **");
}
#endif

#ifdef GENERATE_FOR_EACH
/**
 * Executes the "func" function for every element in the table.
 *
 * @param table the HTABLE_NAME instance you want to walk-through.
 * @param func function you want executed for each table entry. Will receive each value and "arg".
 * @param arg anything you want "func" to receive on every call.
 * @return error status.
 */
static int FOR_EACH(struct HTABLE_NAME *table, int (*func)(VALUE_TYPE *, void *), void *arg)
{
	struct hlist_node *current_node;
	struct KEY_VALUE_PAIR *current_pair;
	__u16 row;
	int error;

	if (!table)
		return -EINVAL;

	for (row = 0; row < HASH_TABLE_SIZE; row++) {
		hlist_for_each(current_node, &table->table[row]) {
			current_pair = hlist_entry(current_node, struct KEY_VALUE_PAIR, nodes);
			error = func(current_pair->value, arg);
			if (error)
				return error;
		}
	}

	return 0;
}
#endif

/*
 * Compiler cleanup. The macros are freed, just so you can define another kind
 * of hash table in the same file without compiler warnings.
 */
#undef HTABLE_NAME
#undef KEY_TYPE
#undef VALUE_TYPE
#undef HASH_TABLE_SIZE
#undef GENERATE_PRINT
#undef GENERATE_FOR_EACH
