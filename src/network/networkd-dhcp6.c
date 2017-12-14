/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright (C) 2014 Intel Corporation. All rights reserved.

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

#include "sd-dhcp6-client.h"

#include "hostname-util.h"
#include "network-internal.h"
#include "networkd-link.h"
#include "networkd-manager.h"

static int dhcp6_lease_address_acquired(sd_dhcp6_client *client, Link *link);

static int dhcp6_lease_information_acquired(sd_dhcp6_client *client,
                                        Link *link) {
        return 0;
}

static int dhcp6_address_handler(sd_netlink *rtnl, sd_netlink_message *m,
                                 void *userdata) {
        _cleanup_link_unref_ Link *link = userdata;
        int r;

        assert(link);

        r = sd_netlink_message_get_errno(m);
        if (r < 0 && r != -EEXIST) {
                if (link->rtnl_extended_attrs) {
                        log_link_warning(link, "Could not set extended netlink attributes, reverting to fallback mechanism");

                        link->rtnl_extended_attrs = false;
                        dhcp6_lease_address_acquired(link->dhcp6_client, link);

                        return 1;
                }

                log_link_error_errno(link, r, "Could not set DHCPv6 address: %m");

                link_enter_failed(link);

        } else if (r >= 0)
                manager_rtnl_process_address(rtnl, m, link->manager);

        return 1;
}

static int dhcp6_address_change(
                Link *link,
                struct in6_addr *ip6_addr,
                uint32_t lifetime_preferred,
                uint32_t lifetime_valid) {

        _cleanup_address_free_ Address *addr = NULL;
        char buffer[INET6_ADDRSTRLEN];
        int r;

        r = address_new(&addr);
        if (r < 0)
                return r;

        addr->family = AF_INET6;
        memcpy(&addr->in_addr.in6, ip6_addr, sizeof(*ip6_addr));

        addr->flags = IFA_F_NOPREFIXROUTE;
        addr->prefixlen = 128;

        addr->cinfo.ifa_prefered = lifetime_preferred;
        addr->cinfo.ifa_valid = lifetime_valid;

        log_link_info(link,
                      "DHCPv6 address %s/%d timeout preferred %d valid %d",
                      inet_ntop(AF_INET6, &addr->in_addr.in6, buffer, sizeof(buffer)),
                      addr->prefixlen, lifetime_preferred, lifetime_valid);

        r = address_configure(addr, link, dhcp6_address_handler, true);
        if (r < 0)
                log_link_warning_errno(link, r, "Could not assign DHCPv6 address: %m");

        return r;
}

static int dhcp6_lease_address_acquired(sd_dhcp6_client *client, Link *link) {
        int r;
        sd_dhcp6_lease *lease;
        struct in6_addr ip6_addr;
        uint32_t lifetime_preferred, lifetime_valid;

        r = sd_dhcp6_client_get_lease(client, &lease);
        if (r < 0)
                return r;

        sd_dhcp6_lease_reset_address_iter(lease);

        while (sd_dhcp6_lease_get_address(lease, &ip6_addr,
                                                 &lifetime_preferred,
                                                 &lifetime_valid) >= 0) {

                r = dhcp6_address_change(link, &ip6_addr, lifetime_preferred, lifetime_valid);
                if (r < 0)
                        return r;
        }

        return 0;
}

static void dhcp6_handler(sd_dhcp6_client *client, int event, void *userdata) {
        int r;
        Link *link = userdata;

        assert(link);
        assert(link->network);

        if (IN_SET(link->state, LINK_STATE_FAILED, LINK_STATE_LINGER))
                return;

        switch(event) {
        case SD_DHCP6_CLIENT_EVENT_STOP:
        case SD_DHCP6_CLIENT_EVENT_RESEND_EXPIRE:
        case SD_DHCP6_CLIENT_EVENT_RETRANS_MAX:
                if (sd_dhcp6_client_get_lease(client, NULL) >= 0)
                        log_link_warning(link, "DHCPv6 lease lost");

                link->dhcp6_configured = false;
                break;

        case SD_DHCP6_CLIENT_EVENT_IP_ACQUIRE:
                r = dhcp6_lease_address_acquired(client, link);
                if (r < 0) {
                        link_enter_failed(link);
                        return;
                }

                _fallthrough_;
        case SD_DHCP6_CLIENT_EVENT_INFORMATION_REQUEST:
                r = dhcp6_lease_information_acquired(client, link);
                if (r < 0) {
                        link_enter_failed(link);
                        return;
                }

                link->dhcp6_configured = true;
                break;

        default:
                if (event < 0)
                        log_link_warning_errno(link, event, "DHCPv6 error: %m");
                else
                        log_link_warning(link, "DHCPv6 unknown event: %d", event);
                return;
        }

        link_check_ready(link);
}

int dhcp6_request_address(Link *link, int ir) {
        int r, inf_req;
        bool running;

        assert(link);
        assert(link->dhcp6_client);
        assert(in_addr_is_link_local(AF_INET6, (const union in_addr_union*)&link->ipv6ll_address) > 0);

        r = sd_dhcp6_client_is_running(link->dhcp6_client);
        if (r < 0)
                return r;
        else
                running = !!r;

        if (running) {
                r = sd_dhcp6_client_get_information_request(link->dhcp6_client, &inf_req);
                if (r < 0)
                        return r;

                if (inf_req == ir)
                        return 0;

                r = sd_dhcp6_client_stop(link->dhcp6_client);
                if (r < 0)
                        return r;
        } else {
                r = sd_dhcp6_client_set_local_address(link->dhcp6_client, &link->ipv6ll_address);
                if (r < 0)
                        return r;
        }

        r = sd_dhcp6_client_set_information_request(link->dhcp6_client, ir);
        if (r < 0)
                return r;

        r = sd_dhcp6_client_start(link->dhcp6_client);
        if (r < 0)
                return r;

        return 0;
}

static int dhcp6_set_hostname(sd_dhcp6_client *client, Link *link) {
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

        return sd_dhcp6_client_set_fqdn(client, hn);
}

int dhcp6_configure(Link *link) {
        sd_dhcp6_client *client = NULL;
        int r;
        const DUID *duid;

        assert(link);

        if (link->dhcp6_client)
                return 0;

        r = sd_dhcp6_client_new(&client);
        if (r < 0)
                return r;

        r = sd_dhcp6_client_attach_event(client, NULL, 0);
        if (r < 0)
                goto error;

        r = sd_dhcp6_client_set_mac(client,
                                    (const uint8_t *) &link->mac,
                                    sizeof (link->mac), ARPHRD_ETHER);
        if (r < 0)
                goto error;

        r = sd_dhcp6_client_set_iaid(client, link->network->iaid);
        if (r < 0)
                goto error;

        duid = link_duid(link);
        r = sd_dhcp6_client_set_duid(client,
                                     duid->type,
                                     duid->raw_data_len > 0 ? duid->raw_data : NULL,
                                     duid->raw_data_len);
        if (r < 0)
                goto error;

        r = dhcp6_set_hostname(client, link);
        if (r < 0)
                goto error;

        r = sd_dhcp6_client_set_ifindex(client, link->ifindex);
        if (r < 0)
                goto error;

        r = sd_dhcp6_client_set_callback(client, dhcp6_handler, link);
        if (r < 0)
                goto error;

        link->dhcp6_client = client;

        return 0;

error:
        sd_dhcp6_client_unref(client);
        return r;
}
