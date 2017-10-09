/***
  This file is part of systemd.

  Copyright 2015 Daniel Mack

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

#include <resolv.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "fd-util.h"
#include "resolved-manager.h"
#include "resolved-mdns.h"

void manager_mdns_stop(Manager *m) {
        assert(m);

        m->mdns_ipv4_event_source = sd_event_source_unref(m->mdns_ipv4_event_source);
        m->mdns_ipv4_fd = safe_close(m->mdns_ipv4_fd);

        m->mdns_ipv6_event_source = sd_event_source_unref(m->mdns_ipv6_event_source);
        m->mdns_ipv6_fd = safe_close(m->mdns_ipv6_fd);
}

int manager_mdns_start(Manager *m) {
        int r;

        assert(m);

        if (m->mdns_support == RESOLVE_SUPPORT_NO)
                return 0;

        r = manager_mdns_ipv4_fd(m);
        if (r == -EADDRINUSE)
                goto eaddrinuse;
        if (r < 0)
                return r;

        if (socket_ipv6_is_supported()) {
                r = manager_mdns_ipv6_fd(m);
                if (r == -EADDRINUSE)
                        goto eaddrinuse;
                if (r < 0)
                        return r;
        }

        return 0;

eaddrinuse:
        log_warning("Another mDNS responder prohibits binding the socket to the same port. Turning off mDNS support.");
        m->mdns_support = RESOLVE_SUPPORT_NO;
        manager_mdns_stop(m);

        return 0;
}

static int mdns_scope_process_query(DnsScope *s, DnsPacket *p) {
        _cleanup_(dns_answer_unrefp) DnsAnswer *answer = NULL, *soa = NULL;
        _cleanup_(dns_packet_unrefp) DnsPacket *reply = NULL;
        DnsResourceKey *key = NULL;
        bool tentative = false;
        int r;

        assert(s);
        assert(p);

        r = dns_packet_extract(p);
        if (r < 0)
                return log_debug_errno(r, "Failed to extract resource records from incoming packet: %m");

        /* TODO: there might be more than one question in mDNS queries. */
        assert_return((dns_question_size(p->question) > 0), -EINVAL);
        key = p->question->keys[0];

        r = dns_zone_lookup(&s->zone, key, 0, &answer, &soa, &tentative);
        if (r < 0)
                return log_debug_errno(r, "Failed to lookup key: %m");
        if (r == 0)
                return 0;

        r = dns_scope_make_reply_packet(s, DNS_PACKET_ID(p), DNS_RCODE_SUCCESS, NULL, answer, NULL, false, &reply);
        if (r < 0)
                return log_debug_errno(r, "Failed to build reply packet: %m");

        if (!ratelimit_test(&s->ratelimit))
                return 0;

        r = dns_scope_emit_udp(s, -1, reply);
        if (r < 0)
                return log_debug_errno(r, "Failed to send reply packet: %m");

        return 0;
}

static int on_mdns_packet(sd_event_source *s, int fd, uint32_t revents, void *userdata) {
        _cleanup_(dns_packet_unrefp) DnsPacket *p = NULL;
        Manager *m = userdata;
        DnsScope *scope;
        int r;

        r = manager_recv(m, fd, DNS_PROTOCOL_MDNS, &p);
        if (r <= 0)
                return r;

        if (manager_our_packet(m, p))
                return 0;

        scope = manager_find_scope(m, p);
        if (!scope) {
                log_warning("Got mDNS UDP packet on unknown scope. Ignoring.");
                return 0;
        }

        if (dns_packet_validate_reply(p) > 0) {
                DnsResourceRecord *rr;

                log_debug("Got mDNS reply packet");

                /*
                 * mDNS is different from regular DNS and LLMNR with regard to handling responses.
                 * While on other protocols, we can ignore every answer that doesn't match a question
                 * we broadcast earlier, RFC6762, section 18.1 recommends looking at and caching all
                 * incoming information, regardless of the DNS packet ID.
                 *
                 * Hence, extract the packet here, and try to find a transaction for answer the we got
                 * and complete it. Also store the new information in scope's cache.
                 */
                r = dns_packet_extract(p);
                if (r < 0) {
                        log_debug("mDNS packet extraction failed.");
                        return 0;
                }

                dns_scope_check_conflicts(scope, p);

                DNS_ANSWER_FOREACH(rr, p->answer) {
                        const char *name = dns_resource_key_name(rr->key);
                        DnsTransaction *t;

                        /* If the received reply packet contains ANY record that is not .local or .in-addr.arpa,
                         * we assume someone's playing tricks on us and discard the packet completely. */
                        if (!(dns_name_endswith(name, "in-addr.arpa") > 0 ||
                              dns_name_endswith(name, "local") > 0))
                                return 0;

                        if (rr->ttl == 0) {
                                log_debug("Got a goodbye packet");
                                /* See the section 10.1 of RFC6762 */
                                rr->ttl = 1;
                        }

                        t = dns_scope_find_transaction(scope, rr->key, false);
                        if (t)
                                dns_transaction_process_reply(t, p);

                        /* Also look for the various types of ANY transactions */
                        t = dns_scope_find_transaction(scope, &DNS_RESOURCE_KEY_CONST(rr->key->class, DNS_TYPE_ANY, dns_resource_key_name(rr->key)), false);
                        if (t)
                                dns_transaction_process_reply(t, p);

                        t = dns_scope_find_transaction(scope, &DNS_RESOURCE_KEY_CONST(DNS_CLASS_ANY, rr->key->type, dns_resource_key_name(rr->key)), false);
                        if (t)
                                dns_transaction_process_reply(t, p);

                        t = dns_scope_find_transaction(scope, &DNS_RESOURCE_KEY_CONST(DNS_CLASS_ANY, DNS_TYPE_ANY, dns_resource_key_name(rr->key)), false);
                        if (t)
                                dns_transaction_process_reply(t, p);
                }

                dns_cache_put(&scope->cache, NULL, DNS_PACKET_RCODE(p), p->answer, false, (uint32_t) -1, 0, p->family, &p->sender);

        } else if (dns_packet_validate_query(p) > 0)  {
                log_debug("Got mDNS query packet for id %u", DNS_PACKET_ID(p));

                r = mdns_scope_process_query(scope, p);
                if (r < 0) {
                        log_debug_errno(r, "mDNS query processing failed: %m");
                        return 0;
                }
        } else
                log_debug("Invalid mDNS UDP packet.");

        return 0;
}

int manager_mdns_ipv4_fd(Manager *m) {
        union sockaddr_union sa = {
                .in.sin_family = AF_INET,
                .in.sin_port = htobe16(MDNS_PORT),
        };
        static const int one = 1, pmtu = IP_PMTUDISC_DONT, ttl = 255;
        int r;

        assert(m);

        if (m->mdns_ipv4_fd >= 0)
                return m->mdns_ipv4_fd;

        m->mdns_ipv4_fd = socket(AF_INET, SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
        if (m->mdns_ipv4_fd < 0)
                return log_error_errno(errno, "mDNS-IPv4: Failed to create socket: %m");

        r = setsockopt(m->mdns_ipv4_fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
        if (r < 0) {
                r = log_error_errno(errno, "mDNS-IPv4: Failed to set IP_TTL: %m");
                goto fail;
        }

        r = setsockopt(m->mdns_ipv4_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
        if (r < 0) {
                r = log_error_errno(errno, "mDNS-IPv4: Failed to set IP_MULTICAST_TTL: %m");
                goto fail;
        }

        r = setsockopt(m->mdns_ipv4_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &one, sizeof(one));
        if (r < 0) {
                r = log_error_errno(errno, "mDNS-IPv4: Failed to set IP_MULTICAST_LOOP: %m");
                goto fail;
        }

        r = setsockopt(m->mdns_ipv4_fd, IPPROTO_IP, IP_PKTINFO, &one, sizeof(one));
        if (r < 0) {
                r = log_error_errno(errno, "mDNS-IPv4: Failed to set IP_PKTINFO: %m");
                goto fail;
        }

        r = setsockopt(m->mdns_ipv4_fd, IPPROTO_IP, IP_RECVTTL, &one, sizeof(one));
        if (r < 0) {
                r = log_error_errno(errno, "mDNS-IPv4: Failed to set IP_RECVTTL: %m");
                goto fail;
        }

        /* Disable Don't-Fragment bit in the IP header */
        r = setsockopt(m->mdns_ipv4_fd, IPPROTO_IP, IP_MTU_DISCOVER, &pmtu, sizeof(pmtu));
        if (r < 0) {
                r = log_error_errno(errno, "mDNS-IPv4: Failed to set IP_MTU_DISCOVER: %m");
                goto fail;
        }

        /* See the section 15.1 of RFC6762 */
        /* first try to bind without SO_REUSEADDR to detect another mDNS responder */
        r = bind(m->mdns_ipv4_fd, &sa.sa, sizeof(sa.in));
        if (r < 0) {
                if (errno != EADDRINUSE) {
                        r = log_error_errno(errno, "mDNS-IPv4: Failed to bind socket: %m");
                        goto fail;
                }

                log_warning("mDNS-IPv4: There appears to be another mDNS responder running, or previously systemd-resolved crashed with some outstanding transfers.");

                /* try again with SO_REUSEADDR */
                r = setsockopt(m->mdns_ipv4_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
                if (r < 0) {
                        r = log_error_errno(errno, "mDNS-IPv4: Failed to set SO_REUSEADDR: %m");
                        goto fail;
                }

                r = bind(m->mdns_ipv4_fd, &sa.sa, sizeof(sa.in));
                if (r < 0) {
                        r = log_error_errno(errno, "mDNS-IPv4: Failed to bind socket: %m");
                        goto fail;
                }
        } else {
                /* enable SO_REUSEADDR for the case that the user really wants multiple mDNS responders */
                r = setsockopt(m->mdns_ipv4_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
                if (r < 0) {
                        r = log_error_errno(errno, "mDNS-IPv4: Failed to set SO_REUSEADDR: %m");
                        goto fail;
                }
        }

        r = sd_event_add_io(m->event, &m->mdns_ipv4_event_source, m->mdns_ipv4_fd, EPOLLIN, on_mdns_packet, m);
        if (r < 0)
                goto fail;

        return m->mdns_ipv4_fd;

fail:
        m->mdns_ipv4_fd = safe_close(m->mdns_ipv4_fd);
        return r;
}

int manager_mdns_ipv6_fd(Manager *m) {
        union sockaddr_union sa = {
                .in6.sin6_family = AF_INET6,
                .in6.sin6_port = htobe16(MDNS_PORT),
        };
        static const int one = 1, ttl = 255;
        int r;

        assert(m);

        if (m->mdns_ipv6_fd >= 0)
                return m->mdns_ipv6_fd;

        m->mdns_ipv6_fd = socket(AF_INET6, SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
        if (m->mdns_ipv6_fd < 0)
                return log_error_errno(errno, "mDNS-IPv6: Failed to create socket: %m");

        r = setsockopt(m->mdns_ipv6_fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &ttl, sizeof(ttl));
        if (r < 0) {
                r = log_error_errno(errno, "mDNS-IPv6: Failed to set IPV6_UNICAST_HOPS: %m");
                goto fail;
        }

        /* RFC 4795, section 2.5 recommends setting the TTL of UDP packets to 255. */
        r = setsockopt(m->mdns_ipv6_fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &ttl, sizeof(ttl));
        if (r < 0) {
                r = log_error_errno(errno, "mDNS-IPv6: Failed to set IPV6_MULTICAST_HOPS: %m");
                goto fail;
        }

        r = setsockopt(m->mdns_ipv6_fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &one, sizeof(one));
        if (r < 0) {
                r = log_error_errno(errno, "mDNS-IPv6: Failed to set IPV6_MULTICAST_LOOP: %m");
                goto fail;
        }

        r = setsockopt(m->mdns_ipv6_fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
        if (r < 0) {
                r = log_error_errno(errno, "mDNS-IPv6: Failed to set IPV6_V6ONLY: %m");
                goto fail;
        }

        r = setsockopt(m->mdns_ipv6_fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &one, sizeof(one));
        if (r < 0) {
                r = log_error_errno(errno, "mDNS-IPv6: Failed to set IPV6_RECVPKTINFO: %m");
                goto fail;
        }

        r = setsockopt(m->mdns_ipv6_fd, IPPROTO_IPV6, IPV6_RECVHOPLIMIT, &one, sizeof(one));
        if (r < 0) {
                r = log_error_errno(errno, "mDNS-IPv6: Failed to set IPV6_RECVHOPLIMIT: %m");
                goto fail;
        }

        /* See the section 15.1 of RFC6762 */
        /* first try to bind without SO_REUSEADDR to detect another mDNS responder */
        r = bind(m->mdns_ipv6_fd, &sa.sa, sizeof(sa.in6));
        if (r < 0) {
                if (errno != EADDRINUSE) {
                        r = log_error_errno(errno, "mDNS-IPv6: Failed to bind socket: %m");
                        goto fail;
                }

                log_warning("mDNS-IPv6: There appears to be another mDNS responder running, or previously systemd-resolved crashed with some outstanding transfers.");

                /* try again with SO_REUSEADDR */
                r = setsockopt(m->mdns_ipv6_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
                if (r < 0) {
                        r = log_error_errno(errno, "mDNS-IPv6: Failed to set SO_REUSEADDR: %m");
                        goto fail;
                }

                r = bind(m->mdns_ipv6_fd, &sa.sa, sizeof(sa.in6));
                if (r < 0) {
                        r = log_error_errno(errno, "mDNS-IPv6: Failed to bind socket: %m");
                        goto fail;
                }
        } else {
                /* enable SO_REUSEADDR for the case that the user really wants multiple mDNS responders */
                r = setsockopt(m->mdns_ipv6_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
                if (r < 0) {
                        r = log_error_errno(errno, "mDNS-IPv6: Failed to set SO_REUSEADDR: %m");
                        goto fail;
                }
        }

        r = sd_event_add_io(m->event, &m->mdns_ipv6_event_source, m->mdns_ipv6_fd, EPOLLIN, on_mdns_packet, m);
        if (r < 0)
                goto fail;

        return m->mdns_ipv6_fd;

fail:
        m->mdns_ipv6_fd = safe_close(m->mdns_ipv6_fd);
        return r;
}
