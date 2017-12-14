/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
  This file is part of systemd.

  Copyright 2014 Lennart Poettering
  Copyright 2014 Tom Gundersen

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

#include "sd-netlink.h"

typedef struct Link Link;
typedef struct Manager Manager;

struct Link {
        Manager *manager;

        int ifindex;
        char *ifname;
        unsigned flags;

        bool required_for_online;
        char *operational_state;
        char *state;
};

int link_new(Manager *m, Link **ret, int ifindex, const char *ifname);
Link *link_free(Link *l);
int link_update_rtnl(Link *l, sd_netlink_message *m);
int link_update_monitor(Link *l);

DEFINE_TRIVIAL_CLEANUP_FUNC(Link*, link_free);
