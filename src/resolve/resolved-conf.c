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

#include "alloc-util.h"
#include "conf-parser.h"
#include "def.h"
#include "extract-word.h"
#include "parse-util.h"
#include "resolved-conf.h"
#include "string-table.h"
#include "string-util.h"

DEFINE_CONFIG_PARSE_ENUM(config_parse_dns_stub_listener_mode, dns_stub_listener_mode, DnsStubListenerMode, "Failed to parse DNS stub listener mode setting");

static const char* const dns_stub_listener_mode_table[_DNS_STUB_LISTENER_MODE_MAX] = {
        [DNS_STUB_LISTENER_NO] = "no",
        [DNS_STUB_LISTENER_UDP] = "udp",
        [DNS_STUB_LISTENER_TCP] = "tcp",
        [DNS_STUB_LISTENER_YES] = "yes",
};
DEFINE_STRING_TABLE_LOOKUP_WITH_BOOLEAN(dns_stub_listener_mode, DnsStubListenerMode, DNS_STUB_LISTENER_YES);

int manager_add_dns_server_by_string(Manager *m, DnsServerType type, const char *word) {
        union in_addr_union address;
        int family, r, ifindex = 0;
        DnsServer *s;

        assert(m);
        assert(word);

        r = in_addr_ifindex_from_string_auto(word, &family, &address, &ifindex);
        if (r < 0)
                return r;

        /* Silently filter out 0.0.0.0 and 127.0.0.53 (our own stub DNS listener) */
        if (!dns_server_address_valid(family, &address))
                return 0;

        /* Filter out duplicates */
        s = dns_server_find(manager_get_first_dns_server(m, type), family, &address, ifindex);
        if (s) {
                /*
                 * Drop the marker. This is used to find the servers
                 * that ceased to exist, see
                 * manager_mark_dns_servers() and
                 * manager_flush_marked_dns_servers().
                 */
                dns_server_move_back_and_unmark(s);
                return 0;
        }

        return dns_server_new(m, NULL, type, NULL, family, &address, ifindex);
}

int manager_parse_dns_server_string_and_warn(Manager *m, DnsServerType type, const char *string) {
        int r;

        assert(m);
        assert(string);

        for (;;) {
                _cleanup_free_ char *word = NULL;

                r = extract_first_word(&string, &word, NULL, 0);
                if (r < 0)
                        return r;
                if (r == 0)
                        break;

                r = manager_add_dns_server_by_string(m, type, word);
                if (r < 0)
                        log_warning_errno(r, "Failed to add DNS server address '%s', ignoring: %m", word);
        }

        return 0;
}

int manager_add_search_domain_by_string(Manager *m, const char *domain) {
        DnsSearchDomain *d;
        bool route_only;
        int r;

        assert(m);
        assert(domain);

        route_only = *domain == '~';
        if (route_only)
                domain++;

        if (dns_name_is_root(domain) || streq(domain, "*")) {
                route_only = true;
                domain = ".";
        }

        r = dns_search_domain_find(m->search_domains, domain, &d);
        if (r < 0)
                return r;
        if (r > 0)
                dns_search_domain_move_back_and_unmark(d);
        else {
                r = dns_search_domain_new(m, &d, DNS_SEARCH_DOMAIN_SYSTEM, NULL, domain);
                if (r < 0)
                        return r;
        }

        d->route_only = route_only;
        return 0;
}

int manager_parse_search_domains_and_warn(Manager *m, const char *string) {
        int r;

        assert(m);
        assert(string);

        for (;;) {
                _cleanup_free_ char *word = NULL;

                r = extract_first_word(&string, &word, NULL, EXTRACT_QUOTES);
                if (r < 0)
                        return r;
                if (r == 0)
                        break;

                r = manager_add_search_domain_by_string(m, word);
                if (r < 0)
                        log_warning_errno(r, "Failed to add search domain '%s', ignoring: %m", word);
        }

        return 0;
}

int config_parse_dns_servers(
                const char *unit,
                const char *filename,
                unsigned line,
                const char *section,
                unsigned section_line,
                const char *lvalue,
                int ltype,
                const char *rvalue,
                void *data,
                void *userdata) {

        Manager *m = userdata;
        int r;

        assert(filename);
        assert(lvalue);
        assert(rvalue);
        assert(m);

        if (isempty(rvalue))
                /* Empty assignment means clear the list */
                dns_server_unlink_all(manager_get_first_dns_server(m, ltype));
        else {
                /* Otherwise, add to the list */
                r = manager_parse_dns_server_string_and_warn(m, ltype, rvalue);
                if (r < 0) {
                        log_syntax(unit, LOG_ERR, filename, line, r, "Failed to parse DNS server string '%s'. Ignoring.", rvalue);
                        return 0;
                }
        }

        /* If we have a manual setting, then we stop reading
         * /etc/resolv.conf */
        if (ltype == DNS_SERVER_SYSTEM)
                m->read_resolv_conf = false;
        if (ltype == DNS_SERVER_FALLBACK)
                m->need_builtin_fallbacks = false;

        return 0;
}

int config_parse_search_domains(
                const char *unit,
                const char *filename,
                unsigned line,
                const char *section,
                unsigned section_line,
                const char *lvalue,
                int ltype,
                const char *rvalue,
                void *data,
                void *userdata) {

        Manager *m = userdata;
        int r;

        assert(filename);
        assert(lvalue);
        assert(rvalue);
        assert(m);

        if (isempty(rvalue))
                /* Empty assignment means clear the list */
                dns_search_domain_unlink_all(m->search_domains);
        else {
                /* Otherwise, add to the list */
                r = manager_parse_search_domains_and_warn(m, rvalue);
                if (r < 0) {
                        log_syntax(unit, LOG_ERR, filename, line, r, "Failed to parse search domains string '%s'. Ignoring.", rvalue);
                        return 0;
                }
        }

        /* If we have a manual setting, then we stop reading
         * /etc/resolv.conf */
        m->read_resolv_conf = false;

        return 0;
}

int manager_parse_config_file(Manager *m) {
        int r;

        assert(m);

        r = config_parse_many_nulstr(PKGSYSCONFDIR "/resolved.conf",
                                     CONF_PATHS_NULSTR("systemd/resolved.conf.d"),
                                     "Resolve\0",
                                     config_item_perf_lookup, resolved_gperf_lookup,
                                     false, m);
        if (r < 0)
                return r;

        if (m->need_builtin_fallbacks) {
                r = manager_parse_dns_server_string_and_warn(m, DNS_SERVER_FALLBACK, DNS_SERVERS);
                if (r < 0)
                        return r;
        }

#if ! HAVE_GCRYPT
        if (m->dnssec_mode != DNSSEC_NO) {
                log_warning("DNSSEC option cannot be enabled or set to allow-downgrade when systemd-resolved is built without gcrypt support. Turning off DNSSEC support.");
                m->dnssec_mode = DNSSEC_NO;
        }
#endif
        return 0;

}
