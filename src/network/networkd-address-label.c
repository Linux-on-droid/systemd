/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2017 Susant Sahani

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
#include <linux/if_addrlabel.h>

#include "alloc-util.h"
#include "conf-parser.h"
#include "networkd-address-label.h"
#include "netlink-util.h"
#include "networkd-manager.h"
#include "parse-util.h"
#include "socket-util.h"

int address_label_new(AddressLabel **ret) {
        _cleanup_address_label_free_ AddressLabel *addrlabel = NULL;

        addrlabel = new0(AddressLabel, 1);
        if (!addrlabel)
                return -ENOMEM;

        *ret = addrlabel;
        addrlabel = NULL;

        return 0;
}

void address_label_free(AddressLabel *label) {
        if (!label)
                return;

        if (label->network) {
                LIST_REMOVE(labels, label->network->address_labels, label);
                assert(label->network->n_address_labels > 0);
                label->network->n_address_labels--;

                if (label->section) {
                        hashmap_remove(label->network->address_labels_by_section, label->section);
                        network_config_section_free(label->section);
                }
        }

        free(label);
}

static int address_label_new_static(Network *network, const char *filename, unsigned section_line, AddressLabel **ret) {
        _cleanup_network_config_section_free_ NetworkConfigSection *n = NULL;
        _cleanup_address_label_free_ AddressLabel *label = NULL;
        int r;

        assert(network);
        assert(ret);
        assert(!!filename == (section_line > 0));

        r = network_config_section_new(filename, section_line, &n);
        if (r < 0)
                return r;

        label = hashmap_get(network->address_labels_by_section, n);
        if (label) {
                *ret = label;
                label = NULL;

                return 0;
        }

        r = address_label_new(&label);
        if (r < 0)
                return r;

        label->section = n;
        n = NULL;

        r = hashmap_put(network->address_labels_by_section, label->section, label);
        if (r < 0)
                return r;

        label->network = network;
        LIST_APPEND(labels, network->address_labels, label);
        network->n_address_labels++;

        *ret = label;
        label = NULL;

        return 0;
}

int address_label_configure(
                AddressLabel *label,
                Link *link,
                sd_netlink_message_handler_t callback,
                bool update) {

        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL;
        int r;

        assert(label);
        assert(link);
        assert(link->ifindex > 0);
        assert(link->manager);
        assert(link->manager->rtnl);

        r = sd_rtnl_message_new_addrlabel(link->manager->rtnl, &req, RTM_NEWADDRLABEL,
                                          link->ifindex, AF_INET6);
        if (r < 0)
                return log_error_errno(r, "Could not allocate RTM_NEWADDR message: %m");

        r = sd_rtnl_message_addrlabel_set_prefixlen(req, label->prefixlen);
        if (r < 0)
                return log_error_errno(r, "Could not set prefixlen: %m");

        r = sd_netlink_message_append_u32(req, IFAL_LABEL, label->label);
        if (r < 0)
                return log_error_errno(r, "Could not append IFAL_LABEL attribute: %m");

        r = sd_netlink_message_append_in6_addr(req, IFA_ADDRESS, &label->in_addr.in6);
        if (r < 0)
                return log_error_errno(r, "Could not append IFA_ADDRESS attribute: %m");

        r = sd_netlink_call_async(link->manager->rtnl, req, callback, link, 0, NULL);
        if (r < 0)
                return log_error_errno(r, "Could not send rtnetlink message: %m");

        link_ref(link);

        return 0;
}

int config_parse_address_label_prefix(const char *unit,
                                      const char *filename,
                                      unsigned line,
                                      const char *section,
                                      unsigned section_line,
                                      const char *lvalue,
                                      int ltype,
                                      const char *rvalue,
                                      void *data,
                                      void *userdata) {

        _cleanup_address_label_free_ AddressLabel *n = NULL;
        Network *network = userdata;
        int r;

        assert(filename);
        assert(section);
        assert(lvalue);
        assert(rvalue);
        assert(data);

        r = address_label_new_static(network, filename, section_line, &n);
        if (r < 0)
                return r;

        r = in_addr_prefix_from_string(rvalue, AF_INET6, &n->in_addr, &n->prefixlen);
        if (r < 0) {
                log_syntax(unit, LOG_ERR, filename, line, r, "Address label is invalid, ignoring assignment: %s", rvalue);
                return 0;
        }

        n = NULL;

        return 0;
}

int config_parse_address_label(
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

        _cleanup_address_label_free_ AddressLabel *n = NULL;
        Network *network = userdata;
        uint32_t k;
        int r;

        assert(filename);
        assert(section);
        assert(lvalue);
        assert(rvalue);
        assert(data);

        r = address_label_new_static(network, filename, section_line, &n);
        if (r < 0)
                return r;

        r = safe_atou32(rvalue, &k);
        if (r < 0) {
                log_syntax(unit, LOG_ERR, filename, line, r, "Failed to parse address label, ignoring: %s", rvalue);
                return 0;
        }

        if (k == 0xffffffffUL) {
                log_syntax(unit, LOG_ERR, filename, line, r, "Adress label is invalid, ignoring: %s", rvalue);
                return 0;
        }

        n->label = k;
        n = NULL;

        return 0;
}
