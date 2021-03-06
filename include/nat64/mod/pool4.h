#ifndef _NF_NAT64_POOL4_H
#define _NF_NAT64_POOL4_H

/**
 * @file
 * The pool of IPv4 addresses (and their ports).
 *
 * @author Alberto Leiva
 */

#include <linux/types.h>
#include <linux/in.h>
#include "nat64/comm/types.h"
#include "nat64/comm/config_proto.h"


/**
 * Readies the rest of this module for future use.
 *
 * @param addr_strs array of strings denoting the IP addresses the pool should start with.
 * @param addr_count size of the "addr_strs" array.
 * @return result status (< 0 on error).
 */
int pool4_init(char *addr_strs[], int addr_count);
/**
 * Frees resources allocated by the pool.
 */
void pool4_destroy(void);

/**
 * Inserts the "address" address (along with its 64k ports) into the "l4protocol" pool.
 * These elements will then become borrowable through the pool_get_* functions.
 */
int pool4_register(struct in_addr *address);
/**
 * Removes the "address" address (along with its 64k ports) from the "l4_protocol" pool.
 * If something was borrowed (not in the pool at the moment) it will be erased later, when the pool
 * retrieves it.
 */
int pool4_remove(struct in_addr *address);

/**
 * Reserves and returns some available IPv4 address from the "l4protocol" pool, along with one of
 * its ports. This port will be 'compatible' with "port".
 * 'Compatible' means same parity and range. See RFC 6146 section 3.5.1.1 for more details on this
 * port hack.
 *
 * @return whether there was something available (and compatible) in the pool. if "false", "result"
 *		will point to garbage.
 */
bool pool4_get_any(u_int8_t l4protocol, __be16 port, struct ipv4_tuple_address *result);
/**
 * Reserves and returns a transport address from the "l4protocol" pool.
 * The address's IPv4 address will be "address.address" and its port will be 'compatible' with
 * "address.l4_id".
 * 'Compatible' means same parity and range. See RFC 6146 section 3.5.1.1 for more details on this
 * port hack.
 *
 * @return the address/port you want to borrow.
 *		Will return NULL if there's nothing available (and compatible) in the pool.
 *		This resulting object will be stored in the heap. If you never return it (by means of
 *		pool4_return()), you're expected to kfree it once you're done with it.
 */
bool pool4_get_similar(u_int8_t l4protocol, struct ipv4_tuple_address *address,
		struct ipv4_tuple_address *result);

bool pool4_get(u_int8_t l4protocol, struct ipv4_tuple_address *address);
/**
 * Don't sweat it too much if this function fails; the user might have removed the address from the
 * pool.
 */
bool pool4_return(u_int8_t l4protocol, struct ipv4_tuple_address *address);

bool pool4_contains(struct in_addr *address);
int pool4_for_each(int (*func)(struct in_addr *, void *), void * arg);

#endif /* _NF_NAT64_POOL4_H */
