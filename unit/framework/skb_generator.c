#include "nat64/unit/skb_generator.h"
#include "nat64/comm/str_utils.h"

#include <linux/if_ether.h>
#include <linux/ipv6.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/icmp.h>


#define IPV4_HDR_LEN sizeof(struct iphdr)
static int init_ipv4_hdr(void *l3_hdr, u16 payload_len, u8 nexthdr, void *arg)
{
	struct iphdr *hdr = l3_hdr;
	struct ipv4_pair *pair4 = arg;

	hdr->version = 4;
	hdr->ihl = 5;
	hdr->tos = 0;
	hdr->tot_len = cpu_to_be16(sizeof(*hdr) + payload_len);
	hdr->id = 0;
	hdr->frag_off = 0;
	hdr->ttl = 32;
	hdr->protocol = nexthdr;
	hdr->saddr = pair4->remote.address.s_addr;
	hdr->daddr = pair4->local.address.s_addr;

	hdr->check = 0;
	hdr->check = ip_fast_csum(hdr, hdr->ihl);

	return 0;
}

#define IPV6_HDR_LEN sizeof(struct ipv6hdr)
static int init_ipv6_hdr(void *l3_hdr, u16 payload_len, u8 nexthdr, void *arg)
{
	struct ipv6hdr *hdr = l3_hdr;
	struct ipv6_pair *pair6 = arg;

	hdr->version = 6;
	hdr->priority = 0;
	hdr->flow_lbl[0] = 0;
	hdr->flow_lbl[1] = 0;
	hdr->flow_lbl[2] = 0;
	hdr->payload_len = cpu_to_be16(payload_len);
	hdr->nexthdr = nexthdr;
	hdr->hop_limit = 32;
	hdr->saddr = pair6->remote.address;
	hdr->daddr = pair6->local.address;

	return 0;
}

#define UDP_HDR_LEN sizeof(struct udphdr)
static int init_udp_hdr(void *l4_hdr, int l3_hdr_type, u16 datagram_len, void *arg)
{
	struct udphdr *hdr = l4_hdr;
	struct ipv6_pair *pair6;
	struct ipv4_pair *pair4;

	switch (l3_hdr_type) {
	case ETH_P_IPV6:
		pair6 = arg;
		hdr->source = cpu_to_be16(pair6->remote.l4_id);
		hdr->dest = cpu_to_be16(pair6->local.l4_id);
		break;
	case ETH_P_IP:
		pair4 = arg;
		hdr->source = cpu_to_be16(pair4->remote.l4_id);
		hdr->dest = cpu_to_be16(pair4->local.l4_id);
		break;
	default:
		log_warning("Unsupported network protocol: %d.", l3_hdr_type);
		return -EINVAL;
	}

	hdr->len = cpu_to_be16(datagram_len);
	hdr->check = 0;

	return 0;
}

#define TCP_HDR_LEN sizeof(struct tcphdr)
static int init_tcp_hdr(void *l4_hdr, int l3_hdr_type, u16 datagram_len, void *arg)
{
	struct tcphdr *hdr = l4_hdr;
	struct ipv6_pair *pair6;
	struct ipv4_pair *pair4;

	switch (l3_hdr_type) {
	case ETH_P_IPV6:
		pair6 = arg;
		hdr->source = cpu_to_be16(pair6->remote.l4_id);
		hdr->dest = cpu_to_be16(pair6->local.l4_id);
		break;
	case ETH_P_IP:
		pair4 = arg;
		hdr->source = cpu_to_be16(pair4->remote.l4_id);
		hdr->dest = cpu_to_be16(pair4->local.l4_id);
		break;
	default:
		log_warning("Unsupported network protocol: %d.", l3_hdr_type);
		return -EINVAL;
	}

	hdr->seq = cpu_to_be16(10000);
	hdr->ack_seq = cpu_to_be16(11000);
	hdr->doff = sizeof(*hdr) / 4;
	hdr->res1 = 0;
	hdr->cwr = 0;
	hdr->ece = 0;
	hdr->urg = 0;
	hdr->ack = 0;
	hdr->psh = 0;
	hdr->rst = 0;
	hdr->syn = 1;
	hdr->fin = 0;
	hdr->window = 10;
	hdr->check = 0;
	hdr->urg_ptr = 0;

	return 0;
}

#define ICMP4_HDR_LEN sizeof(struct icmphdr)
static int init_icmp4_hdr(void *l4_hdr, int l3_hdr_type, u16 datagram_len, void *arg)
{
	struct icmphdr *hdr = l4_hdr;
	struct ipv4_pair *pair4 = arg;

	hdr->type = ICMP_ECHO;
	hdr->code = 0;
	hdr->checksum = 0;
	hdr->un.echo.id = cpu_to_be16(pair4->remote.l4_id);
	hdr->un.echo.sequence = cpu_to_be16(2000);

	return 0;
}

#define ICMP6_HDR_LEN sizeof(struct icmp6hdr)
static int init_icmp6_hdr(void *l4_hdr, int l3_hdr_type, u16 datagram_len, void *arg)
{
	struct icmp6hdr *hdr = l4_hdr;
	struct ipv6_pair *pair6 = arg;

	hdr->icmp6_type = ICMPV6_ECHO_REQUEST;
	hdr->icmp6_code = 0;
	hdr->icmp6_cksum = 0;
	hdr->icmp6_dataun.u_echo.identifier = cpu_to_be16(pair6->remote.l4_id);
	hdr->icmp6_dataun.u_echo.sequence = cpu_to_be16(4000);

	return 0;
}

#define PAYLOAD_LEN 5
static int init_payload_normal(void *l4_hdr)
{
	unsigned char *payload = l4_hdr;
	int i;

	for (i = 0; i < PAYLOAD_LEN; i++)
		payload[i] = i;

	return 0;
}

static int ipv4_udp_post(void *l4_hdr, u16 datagram_len, void *arg)
{
	struct udphdr *hdr = l4_hdr;
	struct ipv4_pair *pair4 = arg;

	hdr->check = csum_tcpudp_magic(pair4->remote.address.s_addr, pair4->local.address.s_addr,
			datagram_len, IPPROTO_UDP, csum_partial(l4_hdr, datagram_len, 0));

	return 0;
}

static int ipv4_tcp_post(void *l4_hdr, u16 datagram_len, void *arg)
{
	struct tcphdr *hdr = l4_hdr;
	struct ipv4_pair *pair4 = arg;

	hdr->check = csum_tcpudp_magic(pair4->remote.address.s_addr, pair4->local.address.s_addr,
			datagram_len, IPPROTO_TCP, csum_partial(l4_hdr, datagram_len, 0));

	return 0;
}

static int ipv4_icmp_post(void *l4_hdr, u16 datagram_len, void *arg)
{
	struct icmphdr *hdr = l4_hdr;
	hdr->checksum = ip_compute_csum(hdr, datagram_len);
	return 0;
}

static int ipv6_udp_post(void *l4_hdr, u16 datagram_len, void *arg)
{
	struct udphdr *hdr = l4_hdr;
	struct ipv6_pair *pair6 = arg;

	hdr->check = csum_ipv6_magic(&pair6->remote.address, &pair6->local.address, datagram_len,
			IPPROTO_UDP, csum_partial(l4_hdr, datagram_len, 0));

	return 0;
}

static int ipv6_tcp_post(void *l4_hdr, u16 datagram_len, void *arg)
{
	struct tcphdr *hdr = l4_hdr;
	struct ipv6_pair *pair6 = arg;

	hdr->check = csum_ipv6_magic(&pair6->remote.address, &pair6->local.address, datagram_len,
			IPPROTO_TCP, csum_partial(l4_hdr, datagram_len, 0));

	return 0;
}

static int ipv6_icmp_post(void *l4_hdr, u16 datagram_len, void *arg)
{
	struct icmp6hdr *hdr = l4_hdr;
	struct ipv6_pair *pair6 = arg;

	hdr->icmp6_cksum = csum_ipv6_magic(&pair6->remote.address, &pair6->local.address, datagram_len,
			IPPROTO_ICMPV6, csum_partial(l4_hdr, datagram_len, 0));

	return 0;
}

static int create_skb(int (*l3_hdr_cb)(void *, u16, u8, void *), int l3_hdr_type, int l3_hdr_len,
		int (*l4_hdr_cb)(void *, int, u16, void *), int l4_hdr_type, int l4_hdr_len,
		int (*payload_cb)(void *), int payload_len,
		int (*l4_post_cb)(void *, u16, void *),
		struct sk_buff **result, void *arg)
{
	struct sk_buff *skb;
	int datagram_len = l4_hdr_len + payload_len;
	int error;

	skb = alloc_skb(LL_MAX_HEADER + l3_hdr_len + datagram_len, GFP_ATOMIC);
	if (!skb) {
		log_err(ERR_ALLOC_FAILED, "New packet allocation failed.");
		return -ENOMEM;
	}
	skb->protocol = htons(l3_hdr_type);

	skb_reserve(skb, LL_MAX_HEADER);
	skb_put(skb, l3_hdr_len + l4_hdr_len + payload_len);

	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	skb_set_transport_header(skb, l3_hdr_len);

	error = l3_hdr_cb(skb_network_header(skb), datagram_len, l4_hdr_type, arg);
	if (error)
		goto failure;
	error = l4_hdr_cb(skb_transport_header(skb), l3_hdr_type, datagram_len, arg);
	if (error)
		goto failure;
	error = payload_cb(skb_transport_header(skb) + l4_hdr_len);
	if (error)
		goto failure;
	error = l4_post_cb(skb_transport_header(skb), datagram_len, arg);
	if (error)
		goto failure;

	*result = skb;
	return 0;

failure:
	kfree_skb(skb);
	return error;
}

int create_skb_ipv6_udp(struct ipv6_pair *pair6, struct sk_buff **result)
{
	return create_skb(init_ipv6_hdr, ETH_P_IPV6, IPV6_HDR_LEN,
			init_udp_hdr, IPPROTO_UDP, UDP_HDR_LEN,
			init_payload_normal, PAYLOAD_LEN,
			ipv6_udp_post,
			result, pair6);
}

int create_skb_ipv6_tcp(struct ipv6_pair *pair6, struct sk_buff **result)
{
	return create_skb(init_ipv6_hdr, ETH_P_IPV6, IPV6_HDR_LEN,
			init_tcp_hdr, IPPROTO_TCP, TCP_HDR_LEN,
			init_payload_normal, PAYLOAD_LEN,
			ipv6_tcp_post,
			result, pair6);
}

int create_skb_ipv6_icmp(struct ipv6_pair *pair6, struct sk_buff **result)
{
	return create_skb(init_ipv6_hdr, ETH_P_IPV6, IPV6_HDR_LEN,
			init_icmp6_hdr, IPPROTO_ICMPV6, ICMP6_HDR_LEN,
			init_payload_normal, PAYLOAD_LEN,
			ipv6_icmp_post,
			result, pair6);
}

int create_skb_ipv4_udp(struct ipv4_pair *pair4, struct sk_buff **result)
{
	return create_skb(init_ipv4_hdr, ETH_P_IP, IPV4_HDR_LEN,
			init_udp_hdr, IPPROTO_UDP, UDP_HDR_LEN,
			init_payload_normal, PAYLOAD_LEN,
			ipv4_udp_post,
			result, pair4);
}

int create_skb_ipv4_tcp(struct ipv4_pair *pair4, struct sk_buff **result)
{
	return create_skb(init_ipv4_hdr, ETH_P_IP, IPV4_HDR_LEN,
			init_tcp_hdr, IPPROTO_TCP, TCP_HDR_LEN,
			init_payload_normal, PAYLOAD_LEN,
			ipv4_tcp_post,
			result, pair4);
}

int create_skb_ipv4_icmp(struct ipv4_pair *pair4, struct sk_buff **result)
{
	return create_skb(init_ipv4_hdr, ETH_P_IP, IPV4_HDR_LEN,
			init_icmp4_hdr, IPPROTO_ICMP, ICMP4_HDR_LEN,
			init_payload_normal, PAYLOAD_LEN,
			ipv4_icmp_post,
			result, pair4);
}
