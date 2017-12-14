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

#include <net/ethernet.h>
#include <net/if.h>

#include "alloc-util.h"
#include "conf-parser.h"
#include "netdev/bridge.h"
#include "netlink-util.h"
#include "networkd-fdb.h"
#include "networkd-manager.h"
#include "util.h"
#include "vlan-util.h"

#define STATIC_FDB_ENTRIES_PER_NETWORK_MAX 1024U

/* create a new FDB entry or get an existing one. */
int fdb_entry_new_static(
                Network *network,
                unsigned section,
                FdbEntry **ret) {

        _cleanup_fdbentry_free_ FdbEntry *fdb_entry = NULL;
        struct ether_addr *mac_addr = NULL;

        assert(network);
        assert(ret);

        /* search entry in hashmap first. */
        if (section) {
                fdb_entry = hashmap_get(network->fdb_entries_by_section, UINT_TO_PTR(section));
                if (fdb_entry) {
                        *ret = fdb_entry;
                        fdb_entry = NULL;

                        return 0;
                }
        }

        if (network->n_static_fdb_entries >= STATIC_FDB_ENTRIES_PER_NETWORK_MAX)
                return -E2BIG;

        /* allocate space for MAC address. */
        mac_addr = new0(struct ether_addr, 1);
        if (!mac_addr)
                return -ENOMEM;

        /* allocate space for and FDB entry. */
        fdb_entry = new0(FdbEntry, 1);
        if (!fdb_entry) {
                /* free previously allocated space for mac_addr. */
                free(mac_addr);
                return -ENOMEM;
        }

        /* init FDB structure. */
        fdb_entry->network = network;
        fdb_entry->mac_addr = mac_addr;

        LIST_PREPEND(static_fdb_entries, network->static_fdb_entries, fdb_entry);
        network->n_static_fdb_entries++;

        if (section) {
                fdb_entry->section = section;
                hashmap_put(network->fdb_entries_by_section,
                            UINT_TO_PTR(fdb_entry->section), fdb_entry);
        }

        /* return allocated FDB structure. */
        *ret = fdb_entry;
        fdb_entry = NULL;

        return 0;
}

static int set_fdb_handler(sd_netlink *rtnl, sd_netlink_message *m, void *userdata) {
        Link *link = userdata;
        int r;

        assert(link);

        r = sd_netlink_message_get_errno(m);
        if (r < 0 && r != -EEXIST)
                log_link_error_errno(link, r, "Could not add FDB entry: %m");

        return 1;
}

/* send a request to the kernel to add a FDB entry in its static MAC table. */
int fdb_entry_configure(Link *link, FdbEntry *fdb_entry) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL;
        sd_netlink *rtnl;
        int r;
        uint8_t flags;
        Bridge *bridge;

        assert(link);
        assert(link->network);
        assert(link->manager);
        assert(fdb_entry);

        rtnl = link->manager->rtnl;
        bridge = BRIDGE(link->network->bridge);

        /* create new RTM message */
        r = sd_rtnl_message_new_neigh(rtnl, &req, RTM_NEWNEIGH, link->ifindex, PF_BRIDGE);
        if (r < 0)
                return rtnl_log_create_error(r);

        if (bridge)
                flags = NTF_MASTER;
        else
                flags = NTF_SELF;

        r = sd_rtnl_message_neigh_set_flags(req, flags);
        if (r < 0)
                return rtnl_log_create_error(r);

        /* only NUD_PERMANENT state supported. */
        r = sd_rtnl_message_neigh_set_state(req, NUD_NOARP | NUD_PERMANENT);
        if (r < 0)
                return rtnl_log_create_error(r);

        r = sd_netlink_message_append_ether_addr(req, NDA_LLADDR, fdb_entry->mac_addr);
        if (r < 0)
                return rtnl_log_create_error(r);

        /* VLAN Id is optional. We'll add VLAN Id only if it's specified. */
        if (0 != fdb_entry->vlan_id) {
                r = sd_netlink_message_append_u16(req, NDA_VLAN, fdb_entry->vlan_id);
                if (r < 0)
                        return rtnl_log_create_error(r);
        }

        /* send message to the kernel to update its internal static MAC table. */
        r = sd_netlink_call_async(rtnl, req, set_fdb_handler, link, 0, NULL);
        if (r < 0)
                return log_link_error_errno(link, r, "Could not send rtnetlink message: %m");

        return 0;
}

/* remove and FDB entry. */
void fdb_entry_free(FdbEntry *fdb_entry) {
        if (!fdb_entry)
                return;

        if (fdb_entry->network) {
                LIST_REMOVE(static_fdb_entries, fdb_entry->network->static_fdb_entries, fdb_entry);

                assert(fdb_entry->network->n_static_fdb_entries > 0);
                fdb_entry->network->n_static_fdb_entries--;

                if (fdb_entry->section)
                        hashmap_remove(fdb_entry->network->fdb_entries_by_section, UINT_TO_PTR(fdb_entry->section));
        }

        free(fdb_entry->mac_addr);

        free(fdb_entry);
}

/* parse the HW address from config files. */
int config_parse_fdb_hwaddr(
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

        Network *network = userdata;
        _cleanup_fdbentry_free_ FdbEntry *fdb_entry = NULL;
        int r;

        assert(filename);
        assert(section);
        assert(lvalue);
        assert(rvalue);
        assert(data);

        r = fdb_entry_new_static(network, section_line, &fdb_entry);
        if (r < 0)
                return log_oom();

        /* read in the MAC address for the FDB table. */
        r = sscanf(rvalue, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                   &fdb_entry->mac_addr->ether_addr_octet[0],
                   &fdb_entry->mac_addr->ether_addr_octet[1],
                   &fdb_entry->mac_addr->ether_addr_octet[2],
                   &fdb_entry->mac_addr->ether_addr_octet[3],
                   &fdb_entry->mac_addr->ether_addr_octet[4],
                   &fdb_entry->mac_addr->ether_addr_octet[5]);

        if (ETHER_ADDR_LEN != r) {
                log_syntax(unit, LOG_ERR, filename, line, 0, "Not a valid MAC address, ignoring assignment: %s", rvalue);
                return 0;
        }

        fdb_entry = NULL;

        return 0;
}

/* parse the VLAN Id from config files. */
int config_parse_fdb_vlan_id(
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

        Network *network = userdata;
        _cleanup_fdbentry_free_ FdbEntry *fdb_entry = NULL;
        int r;

        assert(filename);
        assert(section);
        assert(lvalue);
        assert(rvalue);
        assert(data);

        r = fdb_entry_new_static(network, section_line, &fdb_entry);
        if (r < 0)
                return log_oom();

        r = config_parse_vlanid(unit, filename, line, section,
                                section_line, lvalue, ltype,
                                rvalue, &fdb_entry->vlan_id, userdata);
        if (r < 0)
                return r;

        fdb_entry = NULL;

        return 0;
}
