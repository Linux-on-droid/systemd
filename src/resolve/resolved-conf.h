/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
  This file is part of systemd.

  Copyright 2014 Tom Gundersen <teg@jklm.no>

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

typedef enum DnsStubListenerMode DnsStubListenerMode;

enum DnsStubListenerMode {
        DNS_STUB_LISTENER_NO,
        DNS_STUB_LISTENER_UDP,
        DNS_STUB_LISTENER_TCP,
        DNS_STUB_LISTENER_YES,
        _DNS_STUB_LISTENER_MODE_MAX,
        _DNS_STUB_LISTENER_MODE_INVALID = -1
};

#include "resolved-manager.h"
#include "resolved-dns-server.h"

int manager_parse_config_file(Manager *m);

int manager_add_search_domain_by_string(Manager *m, const char *domain);
int manager_parse_search_domains_and_warn(Manager *m, const char *string);

int manager_add_dns_server_by_string(Manager *m, DnsServerType type, const char *word);
int manager_parse_dns_server_string_and_warn(Manager *m, DnsServerType type, const char *string);

const struct ConfigPerfItem* resolved_gperf_lookup(const char *key, GPERF_LEN_TYPE length);

const struct ConfigPerfItem* resolved_dnssd_gperf_lookup(const char *key, GPERF_LEN_TYPE length);

int config_parse_dns_servers(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_search_domains(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_dns_stub_listener_mode(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_dnssd_service_name(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_dnssd_service_type(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_dnssd_txt(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);

const char* dns_stub_listener_mode_to_string(DnsStubListenerMode p) _const_;
DnsStubListenerMode dns_stub_listener_mode_from_string(const char *s) _pure_;
