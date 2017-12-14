/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
 This file is part of systemd.

 Copyright (C) 2013 Tom Gundersen <teg@jklm.no>

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

#include <macro.h>
#include <linux/ethtool.h>

#include "missing.h"

struct link_config;

/* we can't use DUPLEX_ prefix, as it
 * clashes with <linux/ethtool.h> */
typedef enum Duplex {
        DUP_HALF = DUPLEX_HALF,
        DUP_FULL = DUPLEX_FULL,
        _DUP_MAX,
        _DUP_INVALID = -1
} Duplex;

typedef enum WakeOnLan {
        WOL_PHY,
        WOL_UCAST,
        WOL_MCAST,
        WOL_BCAST,
        WOL_ARP,
        WOL_MAGIC,
        WOL_MAGICSECURE,
        WOL_OFF,
        _WOL_MAX,
        _WOL_INVALID = -1
} WakeOnLan;

typedef enum NetDevFeature {
        NET_DEV_FEAT_GSO,
        NET_DEV_FEAT_GRO,
        NET_DEV_FEAT_LRO,
        NET_DEV_FEAT_TSO,
        NET_DEV_FEAT_TSO6,
        NET_DEV_FEAT_UFO,
        _NET_DEV_FEAT_MAX,
        _NET_DEV_FEAT_INVALID = -1
} NetDevFeature;

typedef enum NetDevPort {
        NET_DEV_PORT_TP     = 0x00,
        NET_DEV_PORT_AUI    = 0x01,
        NET_DEV_PORT_MII    = 0x02,
        NET_DEV_PORT_FIBRE  = 0x03,
        NET_DEV_PORT_BNC    = 0x04,
        NET_DEV_PORT_DA     = 0x05,
        NET_DEV_PORT_NONE   = 0xef,
        NET_DEV_PORT_OTHER  = 0xff,
        _NET_DEV_PORT_MAX,
        _NET_DEV_PORT_INVALID = -1
} NetDevPort;

#define ETHTOOL_LINK_MODE_MASK_MAX_KERNEL_NU32    (SCHAR_MAX)

/* layout of the struct passed from/to userland */
struct ethtool_link_usettings {
        struct ethtool_link_settings base;

        struct {
                uint32_t supported[ETHTOOL_LINK_MODE_MASK_MAX_KERNEL_NU32];
                uint32_t advertising[ETHTOOL_LINK_MODE_MASK_MAX_KERNEL_NU32];
                uint32_t lp_advertising[ETHTOOL_LINK_MODE_MASK_MAX_KERNEL_NU32];
        } link_modes;
};

int ethtool_connect(int *ret);

int ethtool_get_driver(int *fd, const char *ifname, char **ret);
int ethtool_set_speed(int *fd, const char *ifname, unsigned int speed, Duplex duplex);
int ethtool_set_wol(int *fd, const char *ifname, WakeOnLan wol);
int ethtool_set_features(int *fd, const char *ifname, NetDevFeature *features);
int ethtool_set_glinksettings(int *fd, const char *ifname, struct link_config *link);

const char *duplex_to_string(Duplex d) _const_;
Duplex duplex_from_string(const char *d) _pure_;

const char *wol_to_string(WakeOnLan wol) _const_;
WakeOnLan wol_from_string(const char *wol) _pure_;

const char *port_to_string(NetDevPort port) _const_;
NetDevPort port_from_string(const char *port) _pure_;

int config_parse_duplex(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_wol(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
int config_parse_port(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
