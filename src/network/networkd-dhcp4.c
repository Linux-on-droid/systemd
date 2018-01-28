/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2013-2014 Tom Gundersen <teg@jklm.no>

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

#include <netinet/ether.h>
#include <linux/if.h>

#include "alloc-util.h"
#include "dhcp-lease-internal.h"
#include "hostname-util.h"
#include "parse-util.h"
#include "netdev/vrf.h"
#include "network-internal.h"
#include "networkd-link.h"
#include "networkd-manager.h"
#include "networkd-network.h"
#include "string-util.h"
#include "sysctl-util.h"

static int dhcp4_route_handler(sd_netlink *rtnl, sd_netlink_message *m,
                               void *userdata) {
        _cleanup_link_unref_ Link *link = userdata;
        int r;

        assert(link);
        assert(link->dhcp4_messages > 0);

        link->dhcp4_messages--;

        r = sd_netlink_message_get_errno(m);
        if (r < 0 && r != -EEXIST) {
                log_link_error_errno(link, r, "Could not set DHCPv4 route: %m");
                link_enter_failed(link);
        }

        if (link->dhcp4_messages == 0) {
                link->dhcp4_configured = true;
                link_check_ready(link);
        }

        return 1;
}

static int route_scope_from_address(const Route *route, const struct in_addr *self_addr) {
        assert(route);
        assert(self_addr);

        if (in_addr_is_localhost(AF_INET, &route->dst) ||
            (self_addr->s_addr && route->dst.in.s_addr == self_addr->s_addr))
                return RT_SCOPE_HOST;
        else if (in4_addr_is_null(&route->gw.in))
                return RT_SCOPE_LINK;
        else
                return RT_SCOPE_UNIVERSE;
}

static int link_set_dhcp_routes(Link *link) {
        _cleanup_free_ sd_dhcp_route **static_routes = NULL;
        bool classless_route = false, static_route = false;
        struct in_addr gateway, address;
        int r, n, i;
        uint32_t table;

        assert(link);

        if (!link->dhcp_lease) /* link went down while we configured the IP addresses? */
                return 0;

        if (!link->network) /* link went down while we configured the IP addresses? */
                return 0;

        if (!link->network->dhcp_use_routes)
                return 0;

        /* When the interface is part of an VRF use the VRFs routing table, unless
         * there is a another table specified. */
        table = link->network->dhcp_route_table;
        if (!link->network->dhcp_route_table_set && link->network->vrf != NULL)
                table = VRF(link->network->vrf)->table;

        r = sd_dhcp_lease_get_address(link->dhcp_lease, &address);
        if (r < 0)
                return log_link_warning_errno(link, r, "DHCP error: could not get address: %m");

        n = sd_dhcp_lease_get_routes(link->dhcp_lease, &static_routes);
        if (n < 0)
                log_link_debug_errno(link, n, "DHCP error: could not get routes: %m");

        for (i = 0; i < n; i++) {
                if (static_routes[i]->option == SD_DHCP_OPTION_CLASSLESS_STATIC_ROUTE)
                        classless_route = true;

                if (static_routes[i]->option == SD_DHCP_OPTION_STATIC_ROUTE)
                        static_route = true;
        }

        for (i = 0; i < n; i++) {
                _cleanup_route_free_ Route *route = NULL;

                /* if the DHCP server returns both a Classless Static Routes option and a Static Routes option,
                   the DHCP client MUST ignore the Static Routes option. */
                if (classless_route && static_routes[i]->option == SD_DHCP_OPTION_STATIC_ROUTE)
                        continue;

                r = route_new(&route);
                if (r < 0)
                        return log_link_error_errno(link, r, "Could not allocate route: %m");

                route->family = AF_INET;
                route->protocol = RTPROT_DHCP;
                assert_se(sd_dhcp_route_get_gateway(static_routes[i], &route->gw.in) >= 0);
                assert_se(sd_dhcp_route_get_destination(static_routes[i], &route->dst.in) >= 0);
                assert_se(sd_dhcp_route_get_destination_prefix_length(static_routes[i], &route->dst_prefixlen) >= 0);
                route->priority = link->network->dhcp_route_metric;
                route->table = table;
                route->scope = route_scope_from_address(route, &address);

                r = route_configure(route, link, dhcp4_route_handler);
                if (r < 0)
                        return log_link_warning_errno(link, r, "Could not set host route: %m");

                link->dhcp4_messages++;
        }

        r = sd_dhcp_lease_get_router(link->dhcp_lease, &gateway);
        if (r == -ENODATA)
                log_link_info_errno(link, r, "DHCP: No routes received from DHCP server: %m");
        else if (r < 0)
                log_link_warning_errno(link, r, "DHCP error: could not get gateway: %m");

        /* According to RFC 3442: If the DHCP server returns both a Classless Static Routes option and
           a Router option, the DHCP client MUST ignore the Router option. */
        if (classless_route && static_route)
                log_link_warning(link, "Classless static routes received from DHCP server: ignoring static-route option and router option");

        if (r >= 0 && !classless_route) {
                _cleanup_route_free_ Route *route = NULL;
                _cleanup_route_free_ Route *route_gw = NULL;

                r = route_new(&route);
                if (r < 0)
                        return log_link_error_errno(link, r, "Could not allocate route: %m");

                route->protocol = RTPROT_DHCP;

                r = route_new(&route_gw);
                if (r < 0)
                        return log_link_error_errno(link, r,  "Could not allocate route: %m");

                /* The dhcp netmask may mask out the gateway. Add an explicit
                 * route for the gw host so that we can route no matter the
                 * netmask or existing kernel route tables. */
                route_gw->family = AF_INET;
                route_gw->dst.in = gateway;
                route_gw->dst_prefixlen = 32;
                route_gw->prefsrc.in = address;
                route_gw->scope = RT_SCOPE_LINK;
                route_gw->protocol = RTPROT_DHCP;
                route_gw->priority = link->network->dhcp_route_metric;
                route_gw->table = table;

                r = route_configure(route_gw, link, dhcp4_route_handler);
                if (r < 0)
                        return log_link_warning_errno(link, r, "Could not set host route: %m");

                link->dhcp4_messages++;

                route->family = AF_INET;
                route->gw.in = gateway;
                route->prefsrc.in = address;
                route->priority = link->network->dhcp_route_metric;
                route->table = table;

                r = route_configure(route, link, dhcp4_route_handler);
                if (r < 0) {
                        log_link_warning_errno(link, r, "Could not set routes: %m");
                        link_enter_failed(link);
                        return r;
                }

                link->dhcp4_messages++;
        }

        return 0;
}

static int dhcp_lease_lost(Link *link) {
        _cleanup_address_free_ Address *address = NULL;
        struct in_addr addr;
        struct in_addr netmask;
        struct in_addr gateway;
        unsigned prefixlen = 0;
        int r;

        assert(link);
        assert(link->dhcp_lease);

        log_link_warning(link, "DHCP lease lost");

        if (link->network->dhcp_use_routes) {
                _cleanup_free_ sd_dhcp_route **routes = NULL;
                int n, i;

                n = sd_dhcp_lease_get_routes(link->dhcp_lease, &routes);
                if (n >= 0) {
                        for (i = 0; i < n; i++) {
                                _cleanup_route_free_ Route *route = NULL;

                                r = route_new(&route);
                                if (r >= 0) {
                                        route->family = AF_INET;
                                        assert_se(sd_dhcp_route_get_gateway(routes[i], &route->gw.in) >= 0);
                                        assert_se(sd_dhcp_route_get_destination(routes[i], &route->dst.in) >= 0);
                                        assert_se(sd_dhcp_route_get_destination_prefix_length(routes[i], &route->dst_prefixlen) >= 0);

                                        route_remove(route, link,
                                                     link_route_remove_handler);
                                }
                        }
                }
        }

        r = address_new(&address);
        if (r >= 0) {
                r = sd_dhcp_lease_get_router(link->dhcp_lease, &gateway);
                if (r >= 0) {
                        _cleanup_route_free_ Route *route_gw = NULL;
                        _cleanup_route_free_ Route *route = NULL;

                        r = route_new(&route_gw);
                        if (r >= 0) {
                                route_gw->family = AF_INET;
                                route_gw->dst.in = gateway;
                                route_gw->dst_prefixlen = 32;
                                route_gw->scope = RT_SCOPE_LINK;

                                route_remove(route_gw, link,
                                             link_route_remove_handler);
                        }

                        r = route_new(&route);
                        if (r >= 0) {
                                route->family = AF_INET;
                                route->gw.in = gateway;

                                route_remove(route, link,
                                             link_route_remove_handler);
                        }
                }

                r = sd_dhcp_lease_get_address(link->dhcp_lease, &addr);
                if (r >= 0) {
                        r = sd_dhcp_lease_get_netmask(link->dhcp_lease, &netmask);
                        if (r >= 0)
                                prefixlen = in4_addr_netmask_to_prefixlen(&netmask);

                        address->family = AF_INET;
                        address->in_addr.in = addr;
                        address->prefixlen = prefixlen;

                        address_remove(address, link, link_address_remove_handler);
                }
        }

        if (link->network->dhcp_use_mtu) {
                uint16_t mtu;

                r = sd_dhcp_lease_get_mtu(link->dhcp_lease, &mtu);
                if (r >= 0 && link->original_mtu != mtu) {
                        r = link_set_mtu(link, link->original_mtu);
                        if (r < 0) {
                                log_link_warning(link,
                                                 "DHCP error: could not reset MTU");
                                link_enter_failed(link);
                                return r;
                        }
                }
        }

        if (link->network->dhcp_use_hostname) {
                const char *hostname = NULL;

                if (link->network->dhcp_hostname)
                        hostname = link->network->dhcp_hostname;
                else
                        (void) sd_dhcp_lease_get_hostname(link->dhcp_lease, &hostname);

                if (hostname) {
                        /* If a hostname was set due to the lease, then unset it now. */
                        r = manager_set_hostname(link->manager, NULL);
                        if (r < 0)
                                log_link_warning_errno(link, r, "Failed to reset transient hostname: %m");
                }
        }

        link->dhcp_lease = sd_dhcp_lease_unref(link->dhcp_lease);
        link_dirty(link);
        link->dhcp4_configured = false;

        return 0;
}

static int dhcp4_address_handler(sd_netlink *rtnl, sd_netlink_message *m,
                                 void *userdata) {
        _cleanup_link_unref_ Link *link = userdata;
        int r;

        assert(link);

        r = sd_netlink_message_get_errno(m);
        if (r < 0 && r != -EEXIST) {
                log_link_error_errno(link, r, "Could not set DHCPv4 address: %m");
                link_enter_failed(link);
        } else if (r >= 0)
                manager_rtnl_process_address(rtnl, m, link->manager);

        link_set_dhcp_routes(link);

        return 1;
}

static int dhcp4_update_address(Link *link,
                                struct in_addr *address,
                                struct in_addr *netmask,
                                uint32_t lifetime) {
        _cleanup_address_free_ Address *addr = NULL;
        unsigned prefixlen;
        int r;

        assert(address);
        assert(netmask);
        assert(lifetime);

        prefixlen = in4_addr_netmask_to_prefixlen(netmask);

        r = address_new(&addr);
        if (r < 0)
                return r;

        addr->family = AF_INET;
        addr->in_addr.in.s_addr = address->s_addr;
        addr->cinfo.ifa_prefered = lifetime;
        addr->cinfo.ifa_valid = lifetime;
        addr->prefixlen = prefixlen;
        addr->broadcast.s_addr = address->s_addr | ~netmask->s_addr;

        /* allow reusing an existing address and simply update its lifetime
         * in case it already exists */
        r = address_configure(addr, link, dhcp4_address_handler, true);
        if (r < 0)
                return r;

        return 0;
}

static int dhcp_lease_renew(sd_dhcp_client *client, Link *link) {
        sd_dhcp_lease *lease;
        struct in_addr address;
        struct in_addr netmask;
        uint32_t lifetime = CACHE_INFO_INFINITY_LIFE_TIME;
        int r;

        assert(link);
        assert(client);
        assert(link->network);

        r = sd_dhcp_client_get_lease(client, &lease);
        if (r < 0)
                return log_link_warning_errno(link, r, "DHCP error: no lease: %m");

        sd_dhcp_lease_unref(link->dhcp_lease);
        link->dhcp4_configured = false;
        link->dhcp_lease = sd_dhcp_lease_ref(lease);
        link_dirty(link);

        r = sd_dhcp_lease_get_address(lease, &address);
        if (r < 0)
                return log_link_warning_errno(link, r, "DHCP error: no address: %m");

        r = sd_dhcp_lease_get_netmask(lease, &netmask);
        if (r < 0)
                return log_link_warning_errno(link, r, "DHCP error: no netmask: %m");

        if (!link->network->dhcp_critical) {
                r = sd_dhcp_lease_get_lifetime(link->dhcp_lease, &lifetime);
                if (r < 0)
                        return log_link_warning_errno(link, r, "DHCP error: no lifetime: %m");
        }

        r = dhcp4_update_address(link, &address, &netmask, lifetime);
        if (r < 0) {
                log_link_warning_errno(link, r, "Could not update IP address: %m");
                link_enter_failed(link);
                return r;
        }

        return 0;
}

static int dhcp_lease_acquired(sd_dhcp_client *client, Link *link) {
        sd_dhcp_lease *lease;
        struct in_addr address;
        struct in_addr netmask;
        struct in_addr gateway;
        unsigned prefixlen;
        uint32_t lifetime = CACHE_INFO_INFINITY_LIFE_TIME;
        int r;

        assert(client);
        assert(link);

        r = sd_dhcp_client_get_lease(client, &lease);
        if (r < 0)
                return log_link_error_errno(link, r, "DHCP error: No lease: %m");

        r = sd_dhcp_lease_get_address(lease, &address);
        if (r < 0)
                return log_link_error_errno(link, r, "DHCP error: No address: %m");

        r = sd_dhcp_lease_get_netmask(lease, &netmask);
        if (r < 0)
                return log_link_error_errno(link, r, "DHCP error: No netmask: %m");

        prefixlen = in4_addr_netmask_to_prefixlen(&netmask);

        r = sd_dhcp_lease_get_router(lease, &gateway);
        if (r < 0 && r != -ENODATA)
                return log_link_error_errno(link, r, "DHCP error: Could not get gateway: %m");

        if (r >= 0)
                log_struct(LOG_INFO,
                           LOG_LINK_INTERFACE(link),
                           LOG_LINK_MESSAGE(link, "DHCPv4 address %u.%u.%u.%u/%u via %u.%u.%u.%u",
                                            ADDRESS_FMT_VAL(address),
                                            prefixlen,
                                            ADDRESS_FMT_VAL(gateway)),
                           "ADDRESS=%u.%u.%u.%u", ADDRESS_FMT_VAL(address),
                           "PREFIXLEN=%u", prefixlen,
                           "GATEWAY=%u.%u.%u.%u", ADDRESS_FMT_VAL(gateway),
                           NULL);
        else
                log_struct(LOG_INFO,
                           LOG_LINK_INTERFACE(link),
                           LOG_LINK_MESSAGE(link, "DHCPv4 address %u.%u.%u.%u/%u",
                                            ADDRESS_FMT_VAL(address),
                                            prefixlen),
                           "ADDRESS=%u.%u.%u.%u", ADDRESS_FMT_VAL(address),
                           "PREFIXLEN=%u", prefixlen,
                           NULL);

        link->dhcp_lease = sd_dhcp_lease_ref(lease);
        link_dirty(link);

        if (link->network->dhcp_use_mtu) {
                uint16_t mtu;

                r = sd_dhcp_lease_get_mtu(lease, &mtu);
                if (r >= 0) {
                        r = link_set_mtu(link, mtu);
                        if (r < 0)
                                log_link_error_errno(link, r, "Failed to set MTU to %" PRIu16 ": %m", mtu);
                }
        }

        if (link->network->dhcp_use_hostname) {
                const char *dhcpname = NULL;
                _cleanup_free_ char *hostname = NULL;

                if (link->network->dhcp_hostname)
                        dhcpname = link->network->dhcp_hostname;
                else
                        (void) sd_dhcp_lease_get_hostname(lease, &dhcpname);

                if (dhcpname) {
                        r = shorten_overlong(dhcpname, &hostname);
                        if (r < 0)
                                log_link_warning_errno(link, r, "Unable to shorten overlong DHCP hostname '%s', ignoring: %m", dhcpname);
                        if (r == 1)
                                log_link_notice(link, "Overlong DCHP hostname received, shortened from '%s' to '%s'", dhcpname, hostname);
                }

                if (hostname) {
                        r = manager_set_hostname(link->manager, hostname);
                        if (r < 0)
                                log_link_error_errno(link, r, "Failed to set transient hostname to '%s': %m", hostname);
                }
        }

        if (link->network->dhcp_use_timezone) {
                const char *tz = NULL;

                (void) sd_dhcp_lease_get_timezone(link->dhcp_lease, &tz);

                if (tz) {
                        r = manager_set_timezone(link->manager, tz);
                        if (r < 0)
                                log_link_error_errno(link, r, "Failed to set timezone to '%s': %m", tz);
                }
        }

        if (!link->network->dhcp_critical) {
                r = sd_dhcp_lease_get_lifetime(link->dhcp_lease, &lifetime);
                if (r < 0) {
                        log_link_warning_errno(link, r, "DHCP error: no lifetime: %m");
                        return r;
                }
        }

        r = dhcp4_update_address(link, &address, &netmask, lifetime);
        if (r < 0) {
                log_link_warning_errno(link, r, "Could not update IP address: %m");
                link_enter_failed(link);
                return r;
        }

        return 0;
}
static void dhcp4_handler(sd_dhcp_client *client, int event, void *userdata) {
        Link *link = userdata;
        int r = 0;

        assert(link);
        assert(link->network);
        assert(link->manager);

        if (IN_SET(link->state, LINK_STATE_FAILED, LINK_STATE_LINGER))
                return;

        switch (event) {
                case SD_DHCP_CLIENT_EVENT_EXPIRED:
                case SD_DHCP_CLIENT_EVENT_STOP:
                case SD_DHCP_CLIENT_EVENT_IP_CHANGE:
                        if (link->network->dhcp_critical) {
                                log_link_error(link, "DHCPv4 connection considered system critical, ignoring request to reconfigure it.");
                                return;
                        }

                        if (link->dhcp_lease) {
                                r = dhcp_lease_lost(link);
                                if (r < 0) {
                                        link_enter_failed(link);
                                        return;
                                }
                        }

                        if (event == SD_DHCP_CLIENT_EVENT_IP_CHANGE) {
                                r = dhcp_lease_acquired(client, link);
                                if (r < 0) {
                                        link_enter_failed(link);
                                        return;
                                }
                        }

                        break;
                case SD_DHCP_CLIENT_EVENT_RENEW:
                        r = dhcp_lease_renew(client, link);
                        if (r < 0) {
                                link_enter_failed(link);
                                return;
                        }
                        break;
                case SD_DHCP_CLIENT_EVENT_IP_ACQUIRE:
                        r = dhcp_lease_acquired(client, link);
                        if (r < 0) {
                                link_enter_failed(link);
                                return;
                        }
                        break;
                default:
                        if (event < 0)
                                log_link_warning_errno(link, event, "DHCP error: Client failed: %m");
                        else
                                log_link_warning(link, "DHCP unknown event: %i", event);
                        break;
        }

        return;
}

static int dhcp4_set_hostname(Link *link) {
        _cleanup_free_ char *hostname = NULL;
        const char *hn;
        int r;

        assert(link);

        if (!link->network->dhcp_send_hostname)
                hn = NULL;
        else if (link->network->dhcp_hostname)
                hn = link->network->dhcp_hostname;
        else {
                r = gethostname_strict(&hostname);
                if (r < 0 && r != -ENXIO) /* ENXIO: no hostname set or hostname is "localhost" */
                        return r;

                hn = hostname;
        }

        return sd_dhcp_client_set_hostname(link->dhcp_client, hn);
}

static bool promote_secondaries_enabled(const char *ifname) {
        _cleanup_free_ char *promote_secondaries_sysctl = NULL;
        char *promote_secondaries_path;
        int r;

        promote_secondaries_path = strjoina("net/ipv4/conf/", ifname, "/promote_secondaries");
        r = sysctl_read(promote_secondaries_path, &promote_secondaries_sysctl);
        if (r < 0) {
                log_debug_errno(r, "Cannot read sysctl %s", promote_secondaries_path);
                return false;
        }

        truncate_nl(promote_secondaries_sysctl);
        r = parse_boolean(promote_secondaries_sysctl);
        if (r < 0)
                log_warning_errno(r, "Cannot parse sysctl %s with content %s as boolean", promote_secondaries_path, promote_secondaries_sysctl);
        return r > 0;
}

/* dhcp4_set_promote_secondaries will ensure this interface has
 * the "promote_secondaries" option in the kernel set. If this sysctl
 * is not set DHCP will work only as long as the IP address does not
 * changes between leases. The kernel will remove all secondary IP
 * addresses of an interface otherwise. The way systemd-network works
 * is that the new IP of a lease is added as a secondary IP and when
 * the primary one expires it relies on the kernel to promote the
 * secondary IP. See also https://github.com/systemd/systemd/issues/7163
 */
int dhcp4_set_promote_secondaries(Link *link) {
        int r;

        assert(link);
        assert(link->network);
        assert(link->network->dhcp & ADDRESS_FAMILY_IPV4);

        /* check if the kernel has promote_secondaries enabled for our
         * interface. If it is not globally enabled or enabled for the
         * specific interface we must either enable it.
         */
        if (!(promote_secondaries_enabled("all") || promote_secondaries_enabled(link->ifname))) {
                char *promote_secondaries_path = NULL;

                log_link_debug(link, "promote_secondaries is unset, setting it");
                promote_secondaries_path = strjoina("net/ipv4/conf/", link->ifname, "/promote_secondaries");
                r = sysctl_write(promote_secondaries_path, "1");
                if (r < 0)
                        log_link_warning_errno(link, r, "cannot set sysctl %s to 1", promote_secondaries_path);
                return r > 0;
        }

        return 0;
}

int dhcp4_configure(Link *link) {
        int r;

        assert(link);
        assert(link->network);
        assert(link->network->dhcp & ADDRESS_FAMILY_IPV4);

        if (!link->dhcp_client) {
                r = sd_dhcp_client_new(&link->dhcp_client, link->network->dhcp_anonymize);
                if (r < 0)
                        return r;
        }

        r = sd_dhcp_client_attach_event(link->dhcp_client, NULL, 0);
        if (r < 0)
                return r;

        r = sd_dhcp_client_set_mac(link->dhcp_client,
                                   (const uint8_t *) &link->mac,
                                   sizeof (link->mac), ARPHRD_ETHER);
        if (r < 0)
                return r;

        r = sd_dhcp_client_set_ifindex(link->dhcp_client, link->ifindex);
        if (r < 0)
                return r;

        r = sd_dhcp_client_set_callback(link->dhcp_client, dhcp4_handler, link);
        if (r < 0)
                return r;

        r = sd_dhcp_client_set_request_broadcast(link->dhcp_client,
                                                 link->network->dhcp_broadcast);
        if (r < 0)
                return r;

        if (link->mtu) {
                r = sd_dhcp_client_set_mtu(link->dhcp_client, link->mtu);
                if (r < 0)
                        return r;
        }

        if (link->network->dhcp_use_mtu) {
                r = sd_dhcp_client_set_request_option(link->dhcp_client,
                                                      SD_DHCP_OPTION_INTERFACE_MTU);
                if (r < 0)
                        return r;
        }

        /* NOTE: even if this variable is called "use", it also "sends" PRL
         * options, maybe there should be a different configuration variable
         * to send or not route options?. */
        /* NOTE: when using Anonymize=yes, routes PRL options are sent
         * by default, so they don't need to be added here. */
        if (link->network->dhcp_use_routes && !link->network->dhcp_anonymize) {
                r = sd_dhcp_client_set_request_option(link->dhcp_client,
                                                      SD_DHCP_OPTION_STATIC_ROUTE);
                if (r < 0)
                        return r;
                r = sd_dhcp_client_set_request_option(link->dhcp_client,
                                                      SD_DHCP_OPTION_CLASSLESS_STATIC_ROUTE);
                if (r < 0)
                        return r;
        }

        if (link->network->dhcp_use_ntp) {
                r = sd_dhcp_client_set_request_option(link->dhcp_client, SD_DHCP_OPTION_NTP_SERVER);
                if (r < 0)
                        return r;
        }

        if (link->network->dhcp_use_timezone) {
                r = sd_dhcp_client_set_request_option(link->dhcp_client, SD_DHCP_OPTION_NEW_TZDB_TIMEZONE);
                if (r < 0)
                        return r;
        }

        r = dhcp4_set_hostname(link);
        if (r < 0)
                return r;

        if (link->network->dhcp_vendor_class_identifier) {
                r = sd_dhcp_client_set_vendor_class_identifier(link->dhcp_client,
                                                               link->network->dhcp_vendor_class_identifier);
                if (r < 0)
                        return r;
        }

        if (link->network->dhcp_client_port) {
                r = sd_dhcp_client_set_client_port(link->dhcp_client, link->network->dhcp_client_port);
                if (r < 0)
                        return r;
        }

        switch (link->network->dhcp_client_identifier) {
        case DHCP_CLIENT_ID_DUID: {
                /* If configured, apply user specified DUID and/or IAID */
                const DUID *duid = link_duid(link);

                r = sd_dhcp_client_set_iaid_duid(link->dhcp_client,
                                                 link->network->iaid,
                                                 duid->type,
                                                 duid->raw_data_len > 0 ? duid->raw_data : NULL,
                                                 duid->raw_data_len);
                if (r < 0)
                        return r;
                break;
        }
        case DHCP_CLIENT_ID_MAC:
                r = sd_dhcp_client_set_client_id(link->dhcp_client,
                                                 ARPHRD_ETHER,
                                                 (const uint8_t *) &link->mac,
                                                 sizeof(link->mac));
                if (r < 0)
                        return r;
                break;
        default:
                assert_not_reached("Unknown client identifier type.");
        }

        return 0;
}
