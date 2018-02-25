/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
  This file is part of systemd.

  Copyright 2014-2015 Tom Gundersen <teg@jklm.no>

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

#include <linux/if_link.h>

#include "missing.h"
#include "netdev/netdev.h"


typedef enum IPVlanMode {
        NETDEV_IPVLAN_MODE_L2 = IPVLAN_MODE_L2,
        NETDEV_IPVLAN_MODE_L3 = IPVLAN_MODE_L3,
        NETDEV_IPVLAN_MODE_L3S = IPVLAN_MODE_L3S,
        _NETDEV_IPVLAN_MODE_MAX,
        _NETDEV_IPVLAN_MODE_INVALID = -1
} IPVlanMode;

typedef enum IPVlanFlags {
        NETDEV_IPVLAN_FLAGS_BRIGDE,
        NETDEV_IPVLAN_FLAGS_PRIVATE = IPVLAN_F_PRIVATE,
        NETDEV_IPVLAN_FLAGS_VEPA = IPVLAN_F_VEPA,
        _NETDEV_IPVLAN_FLAGS_MAX,
        _NETDEV_IPVLAN_FLAGS_INVALID = -1
} IPVlanFlags;

typedef struct IPVlan {
        NetDev meta;

        IPVlanMode mode;
        IPVlanFlags flags;
} IPVlan;

DEFINE_NETDEV_CAST(IPVLAN, IPVlan);
extern const NetDevVTable ipvlan_vtable;

const char *ipvlan_mode_to_string(IPVlanMode d) _const_;
IPVlanMode ipvlan_mode_from_string(const char *d) _pure_;

const char *ipvlan_flags_to_string(IPVlanFlags d) _const_;
IPVlanFlags ipvlan_flags_from_string(const char *d) _pure_;

int config_parse_ipvlan_mode(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_ipvlan_flags(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
