/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2016 Lennart Poettering

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

#include <nss.h>

#include "sd-bus.h"

#include "alloc-util.h"
#include "bus-common-errors.h"
#include "env-util.h"
#include "fs-util.h"
#include "macro.h"
#include "nss-util.h"
#include "signal-util.h"
#include "stdio-util.h"
#include "string-util.h"
#include "user-util.h"
#include "util.h"

static const struct passwd root_passwd = {
        .pw_name = (char*) "root",
        .pw_passwd = (char*) "x", /* see shadow file */
        .pw_uid = 0,
        .pw_gid = 0,
        .pw_gecos = (char*) "Super User",
        .pw_dir = (char*) "/root",
        .pw_shell = (char*) "/bin/sh",
};

static const struct passwd nobody_passwd = {
        .pw_name = (char*) NOBODY_USER_NAME,
        .pw_passwd = (char*) "*", /* locked */
        .pw_uid = UID_NOBODY,
        .pw_gid = GID_NOBODY,
        .pw_gecos = (char*) "User Nobody",
        .pw_dir = (char*) "/",
        .pw_shell = (char*) "/sbin/nologin",
};

static const struct group root_group = {
        .gr_name = (char*) "root",
        .gr_gid = 0,
        .gr_passwd = (char*) "x", /* see shadow file */
        .gr_mem = (char*[]) { NULL },
};

static const struct group nobody_group = {
        .gr_name = (char*) NOBODY_GROUP_NAME,
        .gr_gid = GID_NOBODY,
        .gr_passwd = (char*) "*", /* locked */
        .gr_mem = (char*[]) { NULL },
};

NSS_GETPW_PROTOTYPES(systemd);
NSS_GETGR_PROTOTYPES(systemd);

static int direct_lookup_name(const char *name, uid_t *ret) {
        _cleanup_free_ char *s = NULL;
        const char *path;
        int r;

        assert(name);

        /* Normally, we go via the bus to resolve names. That has the benefit that it is available from any mount
         * namespace and subject to proper authentication. However, there's one problem: if our module is called from
         * dbus-daemon itself we really can't use D-Bus to communicate. In this case, resort to a client-side hack,
         * and look for the dynamic names directly. This is pretty ugly, but breaks the cyclic dependency. */

        path = strjoina("/run/systemd/dynamic-uid/direct:", name);
        r = readlink_malloc(path, &s);
        if (r < 0)
                return r;

        return parse_uid(s, ret);
}

static int direct_lookup_uid(uid_t uid, char **ret) {
        char path[STRLEN("/run/systemd/dynamic-uid/direct:") + DECIMAL_STR_MAX(uid_t) + 1], *s;
        int r;

        xsprintf(path, "/run/systemd/dynamic-uid/direct:" UID_FMT, uid);

        r = readlink_malloc(path, &s);
        if (r < 0)
                return r;
        if (!valid_user_group_name(s)) { /* extra safety check */
                free(s);
                return -EINVAL;
        }

        *ret = s;
        return 0;
}

enum nss_status _nss_systemd_getpwnam_r(
                const char *name,
                struct passwd *pwd,
                char *buffer, size_t buflen,
                int *errnop) {

        uint32_t translated;
        size_t l;
        int bypass, r;

        BLOCK_SIGNALS(NSS_SIGNALS_BLOCK);

        assert(name);
        assert(pwd);

        /* If the username is not valid, then we don't know it. Ideally libc would filter these for us anyway. We don't
         * generate EINVAL here, because it isn't really out business to complain about invalid user names. */
        if (!valid_user_group_name(name))
                goto not_found;

        /* Synthesize entries for the root and nobody users, in case they are missing in /etc/passwd */
        if (getenv_bool_secure("SYSTEMD_NSS_BYPASS_SYNTHETIC") <= 0) {
                if (streq(name, root_passwd.pw_name)) {
                        *pwd = root_passwd;
                        *errnop = 0;
                        return NSS_STATUS_SUCCESS;
                }
                if (synthesize_nobody() &&
                    streq(name, nobody_passwd.pw_name)) {
                        *pwd = nobody_passwd;
                        *errnop = 0;
                        return NSS_STATUS_SUCCESS;
                }
        }

        /* Make sure that we don't go in circles when allocating a dynamic UID by checking our own database */
        if (getenv_bool_secure("SYSTEMD_NSS_DYNAMIC_BYPASS") > 0)
                goto not_found;

        bypass = getenv_bool_secure("SYSTEMD_NSS_BYPASS_BUS");
        if (bypass <= 0) {
                _cleanup_(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
                _cleanup_(sd_bus_message_unrefp) sd_bus_message* reply = NULL;
                _cleanup_(sd_bus_flush_close_unrefp) sd_bus *bus = NULL;

                r = sd_bus_open_system(&bus);
                if (r < 0) {
                        bypass = 1;
                        goto direct_lookup;
                }

                r = sd_bus_call_method(bus,
                                       "org.freedesktop.systemd1",
                                       "/org/freedesktop/systemd1",
                                       "org.freedesktop.systemd1.Manager",
                                       "LookupDynamicUserByName",
                                       &error,
                                       &reply,
                                       "s",
                                       name);
                if (r < 0) {
                        if (sd_bus_error_has_name(&error, BUS_ERROR_NO_SUCH_DYNAMIC_USER))
                                goto not_found;

                        goto fail;
                }

                r = sd_bus_message_read(reply, "u", &translated);
                if (r < 0)
                        goto fail;
        }

direct_lookup:
        if (bypass > 0) {
                /* Access the dynamic UID allocation directly if we are called from dbus-daemon, see above. */
                r = direct_lookup_name(name, (uid_t*) &translated);
                if (r == -ENOENT)
                        goto not_found;
                if (r < 0)
                        goto fail;
        }

        l = strlen(name);
        if (buflen < l+1) {
                *errnop = ERANGE;
                return NSS_STATUS_TRYAGAIN;
        }

        memcpy(buffer, name, l+1);

        pwd->pw_name = buffer;
        pwd->pw_uid = (uid_t) translated;
        pwd->pw_gid = (uid_t) translated;
        pwd->pw_gecos = (char*) "Dynamic User";
        pwd->pw_passwd = (char*) "*"; /* locked */
        pwd->pw_dir = (char*) "/";
        pwd->pw_shell = (char*) "/sbin/nologin";

        *errnop = 0;
        return NSS_STATUS_SUCCESS;

not_found:
        *errnop = 0;
        return NSS_STATUS_NOTFOUND;

fail:
        *errnop = -r;
        return NSS_STATUS_UNAVAIL;
}

enum nss_status _nss_systemd_getpwuid_r(
                uid_t uid,
                struct passwd *pwd,
                char *buffer, size_t buflen,
                int *errnop) {

        _cleanup_(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
        _cleanup_(sd_bus_message_unrefp) sd_bus_message* reply = NULL;
        _cleanup_(sd_bus_flush_close_unrefp) sd_bus *bus = NULL;
        _cleanup_free_ char *direct = NULL;
        const char *translated;
        size_t l;
        int bypass, r;

        BLOCK_SIGNALS(NSS_SIGNALS_BLOCK);

        if (!uid_is_valid(uid))
                goto not_found;

        /* Synthesize data for the root user and for nobody in case they are missing from /etc/passwd */
        if (getenv_bool_secure("SYSTEMD_NSS_BYPASS_SYNTHETIC") <= 0) {
                if (uid == root_passwd.pw_uid) {
                        *pwd = root_passwd;
                        *errnop = 0;
                        return NSS_STATUS_SUCCESS;
                }
                if (synthesize_nobody() &&
                    uid == nobody_passwd.pw_uid) {
                        *pwd = nobody_passwd;
                        *errnop = 0;
                        return NSS_STATUS_SUCCESS;
                }
        }

        if (!uid_is_dynamic(uid))
                goto not_found;

        if (getenv_bool_secure("SYSTEMD_NSS_DYNAMIC_BYPASS") > 0)
                goto not_found;

        bypass = getenv_bool_secure("SYSTEMD_NSS_BYPASS_BUS");
        if (bypass <= 0) {
                r = sd_bus_open_system(&bus);
                if (r < 0) {
                        bypass = 1;
                        goto direct_lookup;
                }

                r = sd_bus_call_method(bus,
                                       "org.freedesktop.systemd1",
                                       "/org/freedesktop/systemd1",
                                       "org.freedesktop.systemd1.Manager",
                                       "LookupDynamicUserByUID",
                                       &error,
                                       &reply,
                                       "u",
                                       (uint32_t) uid);
                if (r < 0) {
                        if (sd_bus_error_has_name(&error, BUS_ERROR_NO_SUCH_DYNAMIC_USER))
                                goto not_found;

                        goto fail;
                }

                r = sd_bus_message_read(reply, "s", &translated);
                if (r < 0)
                        goto fail;
        }

direct_lookup:
        if (bypass > 0) {
                r = direct_lookup_uid(uid, &direct);
                if (r == -ENOENT)
                        goto not_found;
                if (r < 0)
                        goto fail;

                translated = direct;

        }

        l = strlen(translated) + 1;
        if (buflen < l) {
                *errnop = ERANGE;
                return NSS_STATUS_TRYAGAIN;
        }

        memcpy(buffer, translated, l);

        pwd->pw_name = buffer;
        pwd->pw_uid = uid;
        pwd->pw_gid = uid;
        pwd->pw_gecos = (char*) "Dynamic User";
        pwd->pw_passwd = (char*) "*"; /* locked */
        pwd->pw_dir = (char*) "/";
        pwd->pw_shell = (char*) "/sbin/nologin";

        *errnop = 0;
        return NSS_STATUS_SUCCESS;

not_found:
        *errnop = 0;
        return NSS_STATUS_NOTFOUND;

fail:
        *errnop = -r;
        return NSS_STATUS_UNAVAIL;
}

#pragma GCC diagnostic ignored "-Wsizeof-pointer-memaccess"

enum nss_status _nss_systemd_getgrnam_r(
                const char *name,
                struct group *gr,
                char *buffer, size_t buflen,
                int *errnop) {

        uint32_t translated;
        size_t l;
        int bypass, r;

        BLOCK_SIGNALS(NSS_SIGNALS_BLOCK);

        assert(name);
        assert(gr);

        if (!valid_user_group_name(name))
                goto not_found;

        /* Synthesize records for root and nobody, in case they are missing form /etc/group */
        if (getenv_bool_secure("SYSTEMD_NSS_BYPASS_SYNTHETIC") <= 0) {
                if (streq(name, root_group.gr_name)) {
                        *gr = root_group;
                        *errnop = 0;
                        return NSS_STATUS_SUCCESS;
                }
                if (synthesize_nobody() &&
                    streq(name, nobody_group.gr_name)) {
                        *gr = nobody_group;
                        *errnop = 0;
                        return NSS_STATUS_SUCCESS;
                }
        }

        if (getenv_bool_secure("SYSTEMD_NSS_DYNAMIC_BYPASS") > 0)
                goto not_found;

        bypass = getenv_bool_secure("SYSTEMD_NSS_BYPASS_BUS");
        if (bypass <= 0) {
                _cleanup_(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
                _cleanup_(sd_bus_message_unrefp) sd_bus_message* reply = NULL;
                _cleanup_(sd_bus_flush_close_unrefp) sd_bus *bus = NULL;

                r = sd_bus_open_system(&bus);
                if (r < 0) {
                        bypass = 1;
                        goto direct_lookup;
                }

                r = sd_bus_call_method(bus,
                                       "org.freedesktop.systemd1",
                                       "/org/freedesktop/systemd1",
                                       "org.freedesktop.systemd1.Manager",
                                       "LookupDynamicUserByName",
                                       &error,
                                       &reply,
                                       "s",
                                       name);
                if (r < 0) {
                        if (sd_bus_error_has_name(&error, BUS_ERROR_NO_SUCH_DYNAMIC_USER))
                                goto not_found;

                        goto fail;
                }

                r = sd_bus_message_read(reply, "u", &translated);
                if (r < 0)
                        goto fail;
        }

direct_lookup:
        if (bypass > 0) {
                /* Access the dynamic GID allocation directly if we are called from dbus-daemon, see above. */
                r = direct_lookup_name(name, (uid_t*) &translated);
                if (r == -ENOENT)
                        goto not_found;
                if (r < 0)
                        goto fail;
        }

        l = sizeof(char*) + strlen(name) + 1;
        if (buflen < l) {
                *errnop = ERANGE;
                return NSS_STATUS_TRYAGAIN;
        }

        memzero(buffer, sizeof(char*));
        strcpy(buffer + sizeof(char*), name);

        gr->gr_name = buffer + sizeof(char*);
        gr->gr_gid = (gid_t) translated;
        gr->gr_passwd = (char*) "*"; /* locked */
        gr->gr_mem = (char**) buffer;

        *errnop = 0;
        return NSS_STATUS_SUCCESS;

not_found:
        *errnop = 0;
        return NSS_STATUS_NOTFOUND;

fail:
        *errnop = -r;
        return NSS_STATUS_UNAVAIL;
}

enum nss_status _nss_systemd_getgrgid_r(
                gid_t gid,
                struct group *gr,
                char *buffer, size_t buflen,
                int *errnop) {

        _cleanup_(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
        _cleanup_(sd_bus_message_unrefp) sd_bus_message* reply = NULL;
        _cleanup_(sd_bus_flush_close_unrefp) sd_bus *bus = NULL;
        _cleanup_free_ char *direct = NULL;
        const char *translated;
        size_t l;
        int bypass, r;

        BLOCK_SIGNALS(NSS_SIGNALS_BLOCK);

        if (!gid_is_valid(gid))
                goto not_found;

        /* Synthesize records for root and nobody, in case they are missing from /etc/group */
        if (getenv_bool_secure("SYSTEMD_NSS_BYPASS_SYNTHETIC") <= 0) {
                if (gid == root_group.gr_gid) {
                        *gr = root_group;
                        *errnop = 0;
                        return NSS_STATUS_SUCCESS;
                }
                if (synthesize_nobody() &&
                    gid == nobody_group.gr_gid) {
                        *gr = nobody_group;
                        *errnop = 0;
                        return NSS_STATUS_SUCCESS;
                }
        }

        if (!gid_is_dynamic(gid))
                goto not_found;

        if (getenv_bool_secure("SYSTEMD_NSS_DYNAMIC_BYPASS") > 0)
                goto not_found;

        bypass = getenv_bool_secure("SYSTEMD_NSS_BYPASS_BUS");
        if (bypass <= 0) {
                r = sd_bus_open_system(&bus);
                if (r < 0) {
                        bypass = 1;
                        goto direct_lookup;
                }

                r = sd_bus_call_method(bus,
                                       "org.freedesktop.systemd1",
                                       "/org/freedesktop/systemd1",
                                       "org.freedesktop.systemd1.Manager",
                                       "LookupDynamicUserByUID",
                                       &error,
                                       &reply,
                                       "u",
                                       (uint32_t) gid);
                if (r < 0) {
                        if (sd_bus_error_has_name(&error, BUS_ERROR_NO_SUCH_DYNAMIC_USER))
                                goto not_found;

                        goto fail;
                }

                r = sd_bus_message_read(reply, "s", &translated);
                if (r < 0)
                        goto fail;
        }

direct_lookup:
        if (bypass > 0) {
                r = direct_lookup_uid(gid, &direct);
                if (r == -ENOENT)
                        goto not_found;
                if (r < 0)
                        goto fail;

                translated = direct;
        }

        l = sizeof(char*) + strlen(translated) + 1;
        if (buflen < l) {
                *errnop = ERANGE;
                return NSS_STATUS_TRYAGAIN;
        }

        memzero(buffer, sizeof(char*));
        strcpy(buffer + sizeof(char*), translated);

        gr->gr_name = buffer + sizeof(char*);
        gr->gr_gid = gid;
        gr->gr_passwd = (char*) "*"; /* locked */
        gr->gr_mem = (char**) buffer;

        *errnop = 0;
        return NSS_STATUS_SUCCESS;

not_found:
        *errnop = 0;
        return NSS_STATUS_NOTFOUND;

fail:
        *errnop = -r;
        return NSS_STATUS_UNAVAIL;
}
