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

#include "netdev/netdev.h"

typedef struct Bridge {
        NetDev meta;

        int mcast_querier;
        int mcast_snooping;
        int vlan_filtering;
        int stp;
        uint16_t priority;
        uint16_t group_fwd_mask;
        uint16_t default_pvid;

        usec_t forward_delay;
        usec_t hello_time;
        usec_t max_age;
        usec_t ageing_time;
} Bridge;

DEFINE_NETDEV_CAST(BRIDGE, Bridge);
extern const NetDevVTable bridge_vtable;
