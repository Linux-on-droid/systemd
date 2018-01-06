/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
  This file is part of systemd.

  Copyright 2014 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <net/if.h>

#include "in-addr-util.h"
#include "ratelimit.h"
#include "resolve-util.h"

typedef struct Link Link;
typedef struct LinkAddress LinkAddress;

#include "resolved-dns-rr.h"
#include "resolved-dns-scope.h"
#include "resolved-dns-search-domain.h"
#include "resolved-dns-server.h"
#include "resolved-manager.h"

#define LINK_SEARCH_DOMAINS_MAX 32
#define LINK_DNS_SERVERS_MAX 32

struct LinkAddress {
        Link *link;

        int family;
        union in_addr_union in_addr;

        unsigned char flags, scope;

        DnsResourceRecord *llmnr_address_rr;
        DnsResourceRecord *llmnr_ptr_rr;
        DnsResourceRecord *mdns_address_rr;
        DnsResourceRecord *mdns_ptr_rr;

        LIST_FIELDS(LinkAddress, addresses);
};

struct Link {
        Manager *manager;

        int ifindex;
        unsigned flags;

        LIST_HEAD(LinkAddress, addresses);
        unsigned n_addresses;

        LIST_HEAD(DnsServer, dns_servers);
        DnsServer *current_dns_server;
        unsigned n_dns_servers;

        LIST_HEAD(DnsSearchDomain, search_domains);
        unsigned n_search_domains;

        ResolveSupport llmnr_support;
        ResolveSupport mdns_support;
        DnssecMode dnssec_mode;
        Set *dnssec_negative_trust_anchors;

        DnsScope *unicast_scope;
        DnsScope *llmnr_ipv4_scope;
        DnsScope *llmnr_ipv6_scope;
        DnsScope *mdns_ipv4_scope;
        DnsScope *mdns_ipv6_scope;

        bool is_managed;

        char name[IF_NAMESIZE];
        uint32_t mtu;
        uint8_t operstate;

        bool loaded;
        char *state_file;

        bool unicast_relevant;
};

int link_new(Manager *m, Link **ret, int ifindex);
Link *link_free(Link *l);
int link_process_rtnl(Link *l, sd_netlink_message *m);
int link_update(Link *l);
bool link_relevant(Link *l, int family, bool local_multicast);
LinkAddress* link_find_address(Link *l, int family, const union in_addr_union *in_addr);
void link_add_rrs(Link *l, bool force_remove);

void link_flush_settings(Link *l);
void link_set_dnssec_mode(Link *l, DnssecMode mode);
void link_allocate_scopes(Link *l);

DnsServer* link_set_dns_server(Link *l, DnsServer *s);
DnsServer* link_get_dns_server(Link *l);
void link_next_dns_server(Link *l);

DnssecMode link_get_dnssec_mode(Link *l);
bool link_dnssec_supported(Link *l);

int link_save_user(Link *l);
int link_load_user(Link *l);
void link_remove_user(Link *l);

int link_address_new(Link *l, LinkAddress **ret, int family, const union in_addr_union *in_addr);
LinkAddress *link_address_free(LinkAddress *a);
int link_address_update_rtnl(LinkAddress *a, sd_netlink_message *m);
bool link_address_relevant(LinkAddress *l, bool local_multicast);
void link_address_add_rrs(LinkAddress *a, bool force_remove);

DEFINE_TRIVIAL_CLEANUP_FUNC(Link*, link_free);
