/* SPDX-License-Identifier: LGPL-2.1+ */
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

#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>

#include "conf-parser.h"
#include "ethtool-util.h"
#include "link-config.h"
#include "log.h"
#include "missing.h"
#include "socket-util.h"
#include "string-table.h"
#include "strxcpyx.h"
#include "util.h"

static const char* const duplex_table[_DUP_MAX] = {
        [DUP_FULL] = "full",
        [DUP_HALF] = "half"
};

DEFINE_STRING_TABLE_LOOKUP(duplex, Duplex);
DEFINE_CONFIG_PARSE_ENUM(config_parse_duplex, duplex, Duplex, "Failed to parse duplex setting");

static const char* const wol_table[_WOL_MAX] = {
        [WOL_PHY]         = "phy",
        [WOL_UCAST]       = "unicast",
        [WOL_MCAST]       = "multicast",
        [WOL_BCAST]       = "broadcast",
        [WOL_ARP]         = "arp",
        [WOL_MAGIC]       = "magic",
        [WOL_MAGICSECURE] = "secureon",
        [WOL_OFF]         = "off"
};

DEFINE_STRING_TABLE_LOOKUP(wol, WakeOnLan);
DEFINE_CONFIG_PARSE_ENUM(config_parse_wol, wol, WakeOnLan, "Failed to parse WakeOnLan setting");

static const char* const port_table[_NET_DEV_PORT_MAX] = {
        [NET_DEV_PORT_TP]     = "tp",
        [NET_DEV_PORT_AUI]    = "aui",
        [NET_DEV_PORT_MII]    = "mii",
        [NET_DEV_PORT_FIBRE]  = "fibre",
        [NET_DEV_PORT_BNC]    = "bnc"
};

DEFINE_STRING_TABLE_LOOKUP(port, NetDevPort);
DEFINE_CONFIG_PARSE_ENUM(config_parse_port, port, NetDevPort, "Failed to parse Port setting");

static const char* const netdev_feature_table[_NET_DEV_FEAT_MAX] = {
        [NET_DEV_FEAT_GSO]  = "tx-generic-segmentation",
        [NET_DEV_FEAT_GRO]  = "rx-gro",
        [NET_DEV_FEAT_LRO]  = "rx-lro",
        [NET_DEV_FEAT_TSO]  = "tx-tcp-segmentation",
        [NET_DEV_FEAT_TSO6] = "tx-tcp6-segmentation",
        [NET_DEV_FEAT_UFO]  = "tx-udp-fragmentation",
};

int ethtool_connect(int *ret) {
        int fd;

        assert_return(ret, -EINVAL);

        fd = socket_ioctl_fd();
        if (fd < 0)
                return fd;

        *ret = fd;

        return 0;
}

int ethtool_get_driver(int *fd, const char *ifname, char **ret) {
        struct ethtool_drvinfo ecmd = {
                .cmd = ETHTOOL_GDRVINFO
        };
        struct ifreq ifr = {
                .ifr_data = (void*) &ecmd
        };
        char *d;
        int r;

        if (*fd < 0) {
                r = ethtool_connect(fd);
                if (r < 0)
                        return log_warning_errno(r, "link_config: could not connect to ethtool: %m");
        }

        strscpy(ifr.ifr_name, IFNAMSIZ, ifname);

        r = ioctl(*fd, SIOCETHTOOL, &ifr);
        if (r < 0)
                return -errno;

        d = strdup(ecmd.driver);
        if (!d)
                return -ENOMEM;

        *ret = d;
        return 0;
}

int ethtool_set_speed(int *fd, const char *ifname, unsigned int speed, Duplex duplex) {
        struct ethtool_cmd ecmd = {
                .cmd = ETHTOOL_GSET
        };
        struct ifreq ifr = {
                .ifr_data = (void*) &ecmd
        };
        bool need_update = false;
        int r;

        if (speed == 0 && duplex == _DUP_INVALID)
                return 0;

        if (*fd < 0) {
                r = ethtool_connect(fd);
                if (r < 0)
                        return log_warning_errno(r, "link_config: could not connect to ethtool: %m");
        }

        strscpy(ifr.ifr_name, IFNAMSIZ, ifname);

        r = ioctl(*fd, SIOCETHTOOL, &ifr);
        if (r < 0)
                return -errno;

        if (ethtool_cmd_speed(&ecmd) != speed) {
                ethtool_cmd_speed_set(&ecmd, speed);
                need_update = true;
        }

        switch (duplex) {
                case DUP_HALF:
                        if (ecmd.duplex != DUPLEX_HALF) {
                                ecmd.duplex = DUPLEX_HALF;
                                need_update = true;
                        }
                        break;
                case DUP_FULL:
                        if (ecmd.duplex != DUPLEX_FULL) {
                                ecmd.duplex = DUPLEX_FULL;
                                need_update = true;
                        }
                        break;
                default:
                        break;
        }

        if (need_update) {
                ecmd.cmd = ETHTOOL_SSET;

                r = ioctl(*fd, SIOCETHTOOL, &ifr);
                if (r < 0)
                        return -errno;
        }

        return 0;
}

int ethtool_set_wol(int *fd, const char *ifname, WakeOnLan wol) {
        struct ethtool_wolinfo ecmd = {
                .cmd = ETHTOOL_GWOL
        };
        struct ifreq ifr = {
                .ifr_data = (void*) &ecmd
        };
        bool need_update = false;
        int r;

        if (wol == _WOL_INVALID)
                return 0;

        if (*fd < 0) {
                r = ethtool_connect(fd);
                if (r < 0)
                        return log_warning_errno(r, "link_config: could not connect to ethtool: %m");
        }

        strscpy(ifr.ifr_name, IFNAMSIZ, ifname);

        r = ioctl(*fd, SIOCETHTOOL, &ifr);
        if (r < 0)
                return -errno;

        switch (wol) {
        case WOL_PHY:
                if (ecmd.wolopts != WAKE_PHY) {
                        ecmd.wolopts = WAKE_PHY;
                        need_update = true;
                }
                break;
        case WOL_UCAST:
                if (ecmd.wolopts != WAKE_UCAST) {
                        ecmd.wolopts = WAKE_UCAST;
                        need_update = true;
                }
                break;
        case WOL_MCAST:
                if (ecmd.wolopts != WAKE_MCAST) {
                        ecmd.wolopts = WAKE_MCAST;
                        need_update = true;
                }
                break;
        case WOL_BCAST:
                if (ecmd.wolopts != WAKE_BCAST) {
                        ecmd.wolopts = WAKE_BCAST;
                        need_update = true;
                }
                break;
        case WOL_ARP:
                if (ecmd.wolopts != WAKE_ARP) {
                        ecmd.wolopts = WAKE_ARP;
                        need_update = true;
                }
                break;
        case WOL_MAGIC:
                if (ecmd.wolopts != WAKE_MAGIC) {
                        ecmd.wolopts = WAKE_MAGIC;
                        need_update = true;
                }
                break;
        case WOL_MAGICSECURE:
                if (ecmd.wolopts != WAKE_MAGICSECURE) {
                        ecmd.wolopts = WAKE_MAGICSECURE;
                        need_update = true;
                }
                break;
        case WOL_OFF:
                if (ecmd.wolopts != 0) {
                        ecmd.wolopts = 0;
                        need_update = true;
                }
                break;
        default:
                break;
        }

        if (need_update) {
                ecmd.cmd = ETHTOOL_SWOL;

                r = ioctl(*fd, SIOCETHTOOL, &ifr);
                if (r < 0)
                        return -errno;
        }

        return 0;
}

static int get_stringset(int fd, struct ifreq *ifr, int stringset_id, struct ethtool_gstrings **gstrings) {
        _cleanup_free_ struct ethtool_gstrings *strings = NULL;
        struct {
                struct ethtool_sset_info info;
                uint32_t space;
        } buffer = {
                .info = {
                        .cmd = ETHTOOL_GSSET_INFO,
                        .sset_mask = UINT64_C(1) << stringset_id,
                },
        };
        unsigned len;
        int r;

        ifr->ifr_data = (void *) &buffer.info;

        r = ioctl(fd, SIOCETHTOOL, ifr);
        if (r < 0)
                return -errno;

        if (!buffer.info.sset_mask)
                return -EINVAL;

        len = buffer.info.data[0];

        strings = malloc0(sizeof(struct ethtool_gstrings) + len * ETH_GSTRING_LEN);
        if (!strings)
                return -ENOMEM;

        strings->cmd = ETHTOOL_GSTRINGS;
        strings->string_set = stringset_id;
        strings->len = len;

        ifr->ifr_data = (void *) strings;

        r = ioctl(fd, SIOCETHTOOL, ifr);
        if (r < 0)
                return -errno;

        *gstrings = strings;
        strings = NULL;

        return 0;
}

static int find_feature_index(struct ethtool_gstrings *strings, const char *feature) {
        unsigned i;

        for (i = 0; i < strings->len; i++) {
                if (streq((char *) &strings->data[i * ETH_GSTRING_LEN], feature))
                        return i;
        }

        return -1;
}

int ethtool_set_features(int *fd, const char *ifname, NetDevFeature *features) {
        _cleanup_free_ struct ethtool_gstrings *strings = NULL;
        struct ethtool_sfeatures *sfeatures;
        int block, bit, i, r;
        struct ifreq ifr = {};

        if (*fd < 0) {
                r = ethtool_connect(fd);
                if (r < 0)
                        return log_warning_errno(r, "link_config: could not connect to ethtool: %m");
        }

        strscpy(ifr.ifr_name, IFNAMSIZ, ifname);

        r = get_stringset(*fd, &ifr, ETH_SS_FEATURES, &strings);
        if (r < 0)
                return log_warning_errno(r, "link_config: could not get ethtool features for %s", ifname);

        sfeatures = alloca0(sizeof(struct ethtool_gstrings) + DIV_ROUND_UP(strings->len, 32U) * sizeof(sfeatures->features[0]));
        sfeatures->cmd = ETHTOOL_SFEATURES;
        sfeatures->size = DIV_ROUND_UP(strings->len, 32U);

        for (i = 0; i < _NET_DEV_FEAT_MAX; i++) {

                if (features[i] != -1) {

                        r = find_feature_index(strings, netdev_feature_table[i]);
                        if (r < 0) {
                                log_warning_errno(r, "link_config: could not find feature: %s", netdev_feature_table[i]);
                                continue;
                        }

                        block = r / 32;
                        bit = r % 32;

                        sfeatures->features[block].valid |= 1 << bit;

                        if (features[i])
                                sfeatures->features[block].requested |= 1 << bit;
                        else
                                sfeatures->features[block].requested &= ~(1 << bit);
                }
        }

        ifr.ifr_data = (void *) sfeatures;

        r = ioctl(*fd, SIOCETHTOOL, &ifr);
        if (r < 0)
                return log_warning_errno(r, "link_config: could not set ethtool features for %s", ifname);

        return 0;
}

static int get_glinksettings(int fd, struct ifreq *ifr, struct ethtool_link_usettings **g) {
        struct ecmd {
                struct ethtool_link_settings req;
                __u32 link_mode_data[3 * ETHTOOL_LINK_MODE_MASK_MAX_KERNEL_NU32];
        } ecmd = {
                .req.cmd = ETHTOOL_GLINKSETTINGS,
        };
        struct ethtool_link_usettings *u;
        unsigned offset;
        int r;

        /* The interaction user/kernel via the new API requires a small ETHTOOL_GLINKSETTINGS
           handshake first to agree on the length of the link mode bitmaps. If kernel doesn't
           agree with user, it returns the bitmap length it is expecting from user as a negative
           length (and cmd field is 0). When kernel and user agree, kernel returns valid info in
           all fields (ie. link mode length > 0 and cmd is ETHTOOL_GLINKSETTINGS). Based on
           https://github.com/torvalds/linux/commit/3f1ac7a700d039c61d8d8b99f28d605d489a60cf
        */

        ifr->ifr_data = (void *) &ecmd;

        r = ioctl(fd, SIOCETHTOOL, ifr);
        if (r < 0)
                return -errno;

        if (ecmd.req.link_mode_masks_nwords >= 0 || ecmd.req.cmd != ETHTOOL_GLINKSETTINGS)
                return -EOPNOTSUPP;

        ecmd.req.link_mode_masks_nwords = -ecmd.req.link_mode_masks_nwords;

        ifr->ifr_data = (void *) &ecmd;

        r = ioctl(fd, SIOCETHTOOL, ifr);
        if (r < 0)
                return -errno;

        if (ecmd.req.link_mode_masks_nwords <= 0 || ecmd.req.cmd != ETHTOOL_GLINKSETTINGS)
                return -EOPNOTSUPP;

        u = new0(struct ethtool_link_usettings , 1);
        if (!u)
                return -ENOMEM;

        ecmd.req = u->base;

        offset = 0;
        memcpy(u->link_modes.supported, &ecmd.link_mode_data[offset], 4 * ecmd.req.link_mode_masks_nwords);

        offset += ecmd.req.link_mode_masks_nwords;
        memcpy(u->link_modes.advertising, &ecmd.link_mode_data[offset], 4 * ecmd.req.link_mode_masks_nwords);

        offset += ecmd.req.link_mode_masks_nwords;
        memcpy(u->link_modes.lp_advertising, &ecmd.link_mode_data[offset], 4 * ecmd.req.link_mode_masks_nwords);

        *g = u;

        return 0;
}

static int get_gset(int fd, struct ifreq *ifr, struct ethtool_link_usettings **u) {
        struct ethtool_link_usettings *e;
        struct ethtool_cmd ecmd = {
                .cmd = ETHTOOL_GSET,
        };
        int r;

        ifr->ifr_data = (void *) &ecmd;

        r = ioctl(fd, SIOCETHTOOL, ifr);
        if (r < 0)
                return -errno;

        e = new0(struct ethtool_link_usettings, 1);
        if (!e)
                return -ENOMEM;

        e->base.cmd = ETHTOOL_GSET;

        e->base.link_mode_masks_nwords = 1;
        e->base.speed = ethtool_cmd_speed(&ecmd);
        e->base.duplex = ecmd.duplex;
        e->base.port = ecmd.port;
        e->base.phy_address = ecmd.phy_address;
        e->base.autoneg = ecmd.autoneg;
        e->base.mdio_support = ecmd.mdio_support;

        e->link_modes.supported[0] = ecmd.supported;
        e->link_modes.advertising[0] = ecmd.advertising;
        e->link_modes.lp_advertising[0] = ecmd.lp_advertising;

        *u = e;

        return 0;
}

static int set_slinksettings(int fd, struct ifreq *ifr, const struct ethtool_link_usettings *u) {
        struct {
                struct ethtool_link_settings req;
                __u32 link_mode_data[3 * ETHTOOL_LINK_MODE_MASK_MAX_KERNEL_NU32];
        } ecmd = {};
        unsigned int offset;
        int r;

        if (u->base.cmd != ETHTOOL_GLINKSETTINGS || u->base.link_mode_masks_nwords <= 0)
                return -EINVAL;

        ecmd.req = u->base;
        ecmd.req.cmd = ETHTOOL_SLINKSETTINGS;
        offset = 0;
        memcpy(&ecmd.link_mode_data[offset], u->link_modes.supported, 4 * ecmd.req.link_mode_masks_nwords);

        offset += ecmd.req.link_mode_masks_nwords;
        memcpy(&ecmd.link_mode_data[offset], u->link_modes.advertising, 4 * ecmd.req.link_mode_masks_nwords);

        offset += ecmd.req.link_mode_masks_nwords;
        memcpy(&ecmd.link_mode_data[offset], u->link_modes.lp_advertising, 4 * ecmd.req.link_mode_masks_nwords);

        ifr->ifr_data = (void *) &ecmd;

        r = ioctl(fd, SIOCETHTOOL, ifr);
        if (r < 0)
                return -errno;

        return 0;
}

static int set_sset(int fd, struct ifreq *ifr, const struct ethtool_link_usettings *u) {
        struct ethtool_cmd ecmd = {
                .cmd = ETHTOOL_SSET,
        };
        int r;

        if (u->base.cmd != ETHTOOL_GSET || u->base.link_mode_masks_nwords <= 0)
                return -EINVAL;

        ecmd.supported = u->link_modes.supported[0];
        ecmd.advertising = u->link_modes.advertising[0];
        ecmd.lp_advertising = u->link_modes.lp_advertising[0];

        ethtool_cmd_speed_set(&ecmd, u->base.speed);

        ecmd.duplex = u->base.duplex;
        ecmd.port = u->base.port;
        ecmd.phy_address = u->base.phy_address;
        ecmd.autoneg = u->base.autoneg;
        ecmd.mdio_support = u->base.mdio_support;

        ifr->ifr_data = (void *) &ecmd;

        r = ioctl(fd, SIOCETHTOOL, ifr);
        if (r < 0)
                return -errno;

        return 0;
}

/* If autonegotiation is disabled, the speed and duplex represent the fixed link
 * mode and are writable if the driver supports multiple link modes. If it is
 * enabled then they are read-only. If the link  is up they represent the negotiated
 * link mode; if the link is down, the speed is 0, %SPEED_UNKNOWN or the highest
 * enabled speed and @duplex is %DUPLEX_UNKNOWN or the best enabled duplex mode.
 */

int ethtool_set_glinksettings(int *fd, const char *ifname, struct link_config *link) {
        _cleanup_free_ struct ethtool_link_usettings *u = NULL;
        struct ifreq ifr = {};
        int r;

        if (link->autonegotiation != 0) {
                log_info("link_config: autonegotiation is unset or enabled, the speed and duplex are not writable.");
                return 0;
        }

        if (*fd < 0) {
                r = ethtool_connect(fd);
                if (r < 0)
                        return log_warning_errno(r, "link_config: could not connect to ethtool: %m");
        }

        strscpy(ifr.ifr_name, IFNAMSIZ, ifname);

        r = get_glinksettings(*fd, &ifr, &u);
        if (r < 0) {
                r = get_gset(*fd, &ifr, &u);
                if (r < 0)
                        return log_warning_errno(r, "link_config: Cannot get device settings for %s : %m", ifname);
        }

        if (link->speed)
                u->base.speed = DIV_ROUND_UP(link->speed, 1000000);

        if (link->duplex != _DUP_INVALID)
                u->base.duplex = link->duplex;

        if (link->port != _NET_DEV_PORT_INVALID)
                u->base.port = link->port;

        u->base.autoneg = link->autonegotiation;

        if (u->base.cmd == ETHTOOL_GLINKSETTINGS)
                r = set_slinksettings(*fd, &ifr, u);
        else
                r = set_sset(*fd, &ifr, u);
        if (r < 0)
                return log_warning_errno(r, "link_config: Cannot set device settings for %s : %m", ifname);

        return r;
}
