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

#include "hashmap.h"
#include "list.h"
#include "prioq.h"
#include "time-util.h"

typedef struct DnsCache {
        Hashmap *by_key;
        Prioq *by_expiry;
        unsigned n_hit;
        unsigned n_miss;
} DnsCache;

#include "resolved-dns-answer.h"
#include "resolved-dns-packet.h"
#include "resolved-dns-question.h"
#include "resolved-dns-rr.h"

void dns_cache_flush(DnsCache *c);
void dns_cache_prune(DnsCache *c);

int dns_cache_put(DnsCache *c, DnsResourceKey *key, int rcode, DnsAnswer *answer, bool authenticated, uint32_t nsec_ttl, usec_t timestamp, int owner_family, const union in_addr_union *owner_address);
int dns_cache_lookup(DnsCache *c, DnsResourceKey *key, bool clamp_ttl, int *rcode, DnsAnswer **answer, bool *authenticated);

int dns_cache_check_conflicts(DnsCache *cache, DnsResourceRecord *rr, int owner_family, const union in_addr_union *owner_address);

void dns_cache_dump(DnsCache *cache, FILE *f);
bool dns_cache_is_empty(DnsCache *cache);

unsigned dns_cache_size(DnsCache *cache);

int dns_cache_export_shared_to_packet(DnsCache *cache, DnsPacket *p);
