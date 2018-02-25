/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
  This file is part of systemd.

  Copyright 2013 Tom Gundersen <teg@jklm.no>

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

typedef struct Route Route;
typedef struct NetworkConfigSection NetworkConfigSection;

#include "networkd-network.h"

struct Route {
        Network *network;
        NetworkConfigSection *section;

        Link *link;

        int family;
        int quickack;

        unsigned char dst_prefixlen;
        unsigned char src_prefixlen;
        unsigned char scope;
        unsigned char protocol;  /* RTPROT_* */
        unsigned char type; /* RTN_* */
        unsigned char tos;
        uint32_t priority; /* note that ip(8) calls this 'metric' */
        uint32_t table;
        uint32_t mtu;
        uint32_t initcwnd;
        uint32_t initrwnd;
        unsigned char pref;
        unsigned flags;

        union in_addr_union gw;
        union in_addr_union dst;
        union in_addr_union src;
        union in_addr_union prefsrc;

        usec_t lifetime;
        sd_event_source *expire;

        LIST_FIELDS(Route, routes);
};

int route_new_static(Network *network, const char *filename, unsigned section_line, Route **ret);
int route_new(Route **ret);
void route_free(Route *route);
int route_configure(Route *route, Link *link, sd_netlink_message_handler_t callback);
int route_remove(Route *route, Link *link, sd_netlink_message_handler_t callback);

int route_get(Link *link, int family, const union in_addr_union *dst, unsigned char dst_prefixlen, unsigned char tos, uint32_t priority, uint32_t table, Route **ret);
int route_add(Link *link, int family, const union in_addr_union *dst, unsigned char dst_prefixlen, unsigned char tos, uint32_t priority, uint32_t table, Route **ret);
int route_add_foreign(Link *link, int family, const union in_addr_union *dst, unsigned char dst_prefixlen, unsigned char tos, uint32_t priority, uint32_t table, Route **ret);
void route_update(Route *route, const union in_addr_union *src, unsigned char src_prefixlen, const union in_addr_union *gw, const union in_addr_union *prefsrc, unsigned char scope, unsigned char protocol, unsigned char type);

int route_expire_handler(sd_event_source *s, uint64_t usec, void *userdata);

DEFINE_TRIVIAL_CLEANUP_FUNC(Route*, route_free);
#define _cleanup_route_free_ _cleanup_(route_freep)

int config_parse_gateway(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_preferred_src(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_destination(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_route_priority(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_route_scope(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_route_table(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_gateway_onlink(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_ipv6_route_preference(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_route_protocol(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_route_type(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_tcp_window(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_quickack(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
