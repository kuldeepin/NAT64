#include "nat64/comm/types.h"

#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <net/ipv6.h>


bool ipv4_addr_equals(struct in_addr *expected, struct in_addr *actual)
{
	if (expected == actual)
		return true;
	if (expected == NULL || actual == NULL)
		return false;
	if (expected->s_addr != actual->s_addr)
		return false;

	return true;
}

bool ipv6_addr_equals(struct in6_addr *expected, struct in6_addr *actual)
{
	if (expected == actual)
		return true;
	if (expected == NULL || actual == NULL)
		return false;
	if (!ipv6_addr_equal(expected, actual))
		return false;

	return true;
}

bool ipv4_tuple_addr_equals(struct ipv4_tuple_address *expected, struct ipv4_tuple_address *actual)
{
	if (expected == actual)
		return true;
	if (expected == NULL || actual == NULL)
		return false;
	if (expected->address.s_addr != actual->address.s_addr)
		return false;
	if (expected->l4_id != actual->l4_id)
		return false;

	return true;
}

__u16 ipv4_tuple_addr_hashcode(struct ipv4_tuple_address *address)
{
	return (address != NULL) ? address->l4_id : 0;
}

bool ipv6_tuple_addr_equals(struct ipv6_tuple_address *expected, struct ipv6_tuple_address *actual)
{
	if (expected == actual)
		return true;
	if (expected == NULL || actual == NULL)
		return false;
	if (!ipv6_addr_equal(&expected->address, &actual->address))
		return false;
	if (expected->l4_id != actual->l4_id)
		return false;

	return true;
}

__u16 ipv6_tuple_addr_hashcode(struct ipv6_tuple_address *address)
{
	/*
	 * address->l4_id would perhaps be the logical hash code, since it's usually random,
	 * but during bib_get_by_ipv6_only() we need to ignore it during lookup
	 * so this needs to be a little more creative.
	 */

	__u16 i;
	__u16 result = 1;

	if (address == NULL)
		return 0;

	for (i = 0; i < 8; i++)
		result = 31 * result + ntohs(address->address.s6_addr16[i]);

	return result;
}

bool ipv4_pair_equals(struct ipv4_pair *pair_1, struct ipv4_pair *pair_2)
{
	if (pair_1 == NULL && pair_2 == NULL)
		return true;
	if (pair_1 == NULL || pair_2 == NULL)
		return false;
	if (!ipv4_tuple_addr_equals(&pair_1->local, &pair_2->local))
		return false;
	if (!ipv4_tuple_addr_equals(&pair_1->remote, &pair_2->remote))
		return false;

	return true;
}

bool ipv6_pair_equals(struct ipv6_pair *pair_1, struct ipv6_pair *pair_2)
{
	if (pair_1 == NULL && pair_2 == NULL)
		return true;
	if (pair_1 == NULL || pair_2 == NULL)
		return false;
	if (!ipv6_tuple_addr_equals(&pair_1->local, &pair_2->local))
		return false;
	if (!ipv6_tuple_addr_equals(&pair_1->remote, &pair_2->remote))
		return false;

	return true;
}

__u16 ipv4_pair_hashcode(struct ipv4_pair *pair)
{
	/*
	 * pair->remote.l4_id would perhaps be the logical hash code, since it's usually random,
	 * but during session_allow() we need to ignore it during lookup
	 * so this needs to be a little more creative.
	 */

	union ipv4_addr_union {
		__be32 by32;
		__be16 by16[2];
	} local, remote;
	__u16 result = 1;

	if (pair == NULL)
		return 0;

	local.by32 = pair->local.address.s_addr;
	remote.by32 = pair->remote.address.s_addr;

	result = 31 * result + ntohs(local.by16[0]);
	result = 31 * result + ntohs(remote.by16[0]);
	result = 31 * result + ntohs(local.by16[1]);
	result = 31 * result + ntohs(remote.by16[1]);

	return result;
}

__u16 ipv6_pair_hashcode(struct ipv6_pair *pair)
{
	return (pair != NULL) ? pair->local.l4_id : 0;
}

bool ipv6_prefix_equals(struct ipv6_prefix *expected, struct ipv6_prefix *actual)
{
	if (expected == actual)
		return true;
	if (expected == NULL || actual == NULL)
		return false;
	if (!ipv6_addr_equal(&expected->address, &actual->address))
		return false;
	if (expected->len != actual->len)
		return false;

	return true;
}

bool is_icmp6_info(__u8 type)
{
	return (type == ICMPV6_ECHO_REQUEST) || (type == ICMPV6_ECHO_REPLY);
}

bool is_icmp6_error(__u8 type)
{
	return !is_icmp6_info(type);
}

bool is_icmp4_info(__u8 type)
{
	return (type == ICMP_ECHO) || (type == ICMP_ECHOREPLY);
}

bool is_icmp4_error(__u8 type)
{
	return !is_icmp4_info(type);
}

static unsigned char *l3proto_to_str(int l3_proto)
{
	switch (l3_proto) {
	case PF_INET:
		return "IPv4";
	case PF_INET6:
		return "IPv6";
	default:
		return "Unknown";
	}
}

static unsigned char *l4proto_to_str(int l4_proto)
{
	switch (l4_proto) {
	case IPPROTO_UDP:
		return "UDP";
	case IPPROTO_TCP:
		return "TCP";
	case IPPROTO_ICMP:
		return "ICMPv4";
	case IPPROTO_ICMPV6:
		return "ICMPv6";
	default:
		return "Unknown";
	}
}

/**
* log_tuple() - Prints the "tuple" tuple on the kernel ring buffer.
* @tuple: Structure to be dumped on logging.
*
* It's a ripoff of nf_ct_dump_tuple(), adjusted to comply to this project's logging requirements.
*/
void log_tuple(struct tuple *tuple)
{
	switch (tuple->l3_proto) {
	case PF_INET:
		log_debug("tuple %s-%s %pI4#%u -> %pI4#%u",
				l3proto_to_str(tuple->l3_proto), l4proto_to_str(tuple->l4_proto),
				&tuple->src.addr.ipv4, tuple->src.l4_id,
				&tuple->dst.addr.ipv4, tuple->dst.l4_id);
		break;
	case PF_INET6:
		log_debug("tuple %s-%s %pI6c#%u -> %pI6c#%u",
				l3proto_to_str(tuple->l3_proto), l4proto_to_str(tuple->l4_proto),
				&tuple->src.addr.ipv6, tuple->src.l4_id,
				&tuple->dst.addr.ipv6, tuple->dst.l4_id);
		break;
	}
}
