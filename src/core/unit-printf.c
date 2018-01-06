/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2010 Lennart Poettering

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

#include "alloc-util.h"
#include "cgroup-util.h"
#include "format-util.h"
#include "macro.h"
#include "specifier.h"
#include "string-util.h"
#include "strv.h"
#include "unit-name.h"
#include "unit-printf.h"
#include "unit.h"
#include "user-util.h"

static int specifier_prefix_and_instance(char specifier, void *data, void *userdata, char **ret) {
        Unit *u = userdata;

        assert(u);

        return unit_name_to_prefix_and_instance(u->id, ret);
}

static int specifier_prefix(char specifier, void *data, void *userdata, char **ret) {
        Unit *u = userdata;

        assert(u);

        return unit_name_to_prefix(u->id, ret);
}

static int specifier_prefix_unescaped(char specifier, void *data, void *userdata, char **ret) {
        _cleanup_free_ char *p = NULL;
        Unit *u = userdata;
        int r;

        assert(u);

        r = unit_name_to_prefix(u->id, &p);
        if (r < 0)
                return r;

        return unit_name_unescape(p, ret);
}

static int specifier_instance_unescaped(char specifier, void *data, void *userdata, char **ret) {
        Unit *u = userdata;

        assert(u);

        return unit_name_unescape(strempty(u->instance), ret);
}

static int specifier_filename(char specifier, void *data, void *userdata, char **ret) {
        Unit *u = userdata;

        assert(u);

        if (u->instance)
                return unit_name_path_unescape(u->instance, ret);
        else
                return unit_name_to_path(u->id, ret);
}

static void bad_specifier(Unit *u, char specifier) {
        log_unit_warning(u, "Specifier '%%%c' used in unit configuration, which is deprecated. Please update your unit file, as it does not work as intended.", specifier);
}

static int specifier_cgroup(char specifier, void *data, void *userdata, char **ret) {
        Unit *u = userdata;
        char *n;

        assert(u);

        bad_specifier(u, specifier);

        if (u->cgroup_path)
                n = strdup(u->cgroup_path);
        else
                n = unit_default_cgroup_path(u);
        if (!n)
                return -ENOMEM;

        *ret = n;
        return 0;
}

static int specifier_cgroup_root(char specifier, void *data, void *userdata, char **ret) {
        Unit *u = userdata;
        char *n;

        assert(u);

        bad_specifier(u, specifier);

        n = strdup(u->manager->cgroup_root);
        if (!n)
                return -ENOMEM;

        *ret = n;
        return 0;
}

static int specifier_cgroup_slice(char specifier, void *data, void *userdata, char **ret) {
        Unit *u = userdata;
        char *n;

        assert(u);

        bad_specifier(u, specifier);

        if (UNIT_ISSET(u->slice)) {
                Unit *slice;

                slice = UNIT_DEREF(u->slice);

                if (slice->cgroup_path)
                        n = strdup(slice->cgroup_path);
                else
                        n = unit_default_cgroup_path(slice);
        } else
                n = strdup(u->manager->cgroup_root);
        if (!n)
                return -ENOMEM;

        *ret = n;
        return 0;
}

static int specifier_special_directory(char specifier, void *data, void *userdata, char **ret) {
        Unit *u = userdata;
        char *n = NULL;

        assert(u);

        n = strdup(u->manager->prefix[PTR_TO_UINT(data)]);
        if (!n)
                return -ENOMEM;

        *ret = n;
        return 0;
}

int unit_name_printf(Unit *u, const char* format, char **ret) {

        /*
         * This will use the passed string as format string and replace the following specifiers (which should all be
         * safe for inclusion in unit names):
         *
         * %n: the full id of the unit                 (foo@bar.waldo)
         * %N: the id of the unit without the suffix   (foo@bar)
         * %p: the prefix                              (foo)
         * %i: the instance                            (bar)
         *
         * %U: the UID of the running user
         * %u: the username of the running user
         *
         * %m: the machine ID of the running system
         * %H: the host name of the running system
         * %b: the boot ID of the running system
         */

        const Specifier table[] = {
                { 'n', specifier_string,              u->id },
                { 'N', specifier_prefix_and_instance, NULL },
                { 'p', specifier_prefix,              NULL },
                { 'i', specifier_string,              u->instance },

                { 'U', specifier_user_id,             NULL },
                { 'u', specifier_user_name,           NULL },

                { 'm', specifier_machine_id,          NULL },
                { 'H', specifier_host_name,           NULL },
                { 'b', specifier_boot_id,             NULL },
                {}
        };

        assert(u);
        assert(format);
        assert(ret);

        return specifier_printf(format, table, u, ret);
}

int unit_full_printf(Unit *u, const char *format, char **ret) {

        /* This is similar to unit_name_printf() but also supports unescaping. Also, adds a couple of additional codes
         * (which are likely not suitable for unescaped inclusion in unit names):
         *
         * %f: the unescaped instance if set, otherwise the id unescaped as path
         *
         * %c: cgroup path of unit (deprecated)
         * %r: where units in this slice are placed in the cgroup tree (deprecated)
         * %R: the root of this systemd's instance tree (deprecated)
         *
         * %t: the runtime directory root (e.g. /run or $XDG_RUNTIME_DIR)
         * %S: the state directory root (e.g. /var/lib or $XDG_CONFIG_HOME)
         * %C: the cache directory root (e.g. /var/cache or $XDG_CACHE_HOME)
         * %L: the log directory root (e.g. /var/log or $XDG_CONFIG_HOME/log)
         *
         * %h: the homedir of the running user
         * %s: the shell of the running user
         *
         * %v: `uname -r` of the running system
         *
         * NOTICE: When you add new entries here, please be careful: specifiers which depend on settings of the unit
         * file itself are broken by design, as they would resolve differently depending on whether they are used
         * before or after the relevant configuration setting. Hence: don't add them.
         */

        const Specifier table[] = {
                { 'n', specifier_string,              u->id },
                { 'N', specifier_prefix_and_instance, NULL },
                { 'p', specifier_prefix,              NULL },
                { 'P', specifier_prefix_unescaped,    NULL },
                { 'i', specifier_string,              u->instance },
                { 'I', specifier_instance_unescaped,  NULL },

                { 'f', specifier_filename,            NULL },
                { 'c', specifier_cgroup,              NULL },
                { 'r', specifier_cgroup_slice,        NULL },
                { 'R', specifier_cgroup_root,         NULL },
                { 't', specifier_special_directory,   UINT_TO_PTR(EXEC_DIRECTORY_RUNTIME) },
                { 'S', specifier_special_directory,   UINT_TO_PTR(EXEC_DIRECTORY_STATE) },
                { 'C', specifier_special_directory,   UINT_TO_PTR(EXEC_DIRECTORY_CACHE) },
                { 'L', specifier_special_directory,   UINT_TO_PTR(EXEC_DIRECTORY_LOGS) },

                { 'U', specifier_user_id,             NULL },
                { 'u', specifier_user_name,           NULL },
                { 'h', specifier_user_home,           NULL },
                { 's', specifier_user_shell,          NULL },

                { 'm', specifier_machine_id,          NULL },
                { 'H', specifier_host_name,           NULL },
                { 'b', specifier_boot_id,             NULL },
                { 'v', specifier_kernel_release,      NULL },
                {}
        };

        assert(u);
        assert(format);
        assert(ret);

        return specifier_printf(format, table, u, ret);
}

int unit_full_printf_strv(Unit *u, char **l, char ***ret) {
        size_t n;
        char **r, **i, **j;
        int q;

        /* Applies unit_full_printf to every entry in l */

        assert(u);

        n = strv_length(l);
        r = new(char*, n+1);
        if (!r)
                return -ENOMEM;

        for (i = l, j = r; *i; i++, j++) {
                q = unit_full_printf(u, *i, j);
                if (q < 0)
                        goto fail;
        }

        *j = NULL;
        *ret = r;
        return 0;

fail:
        for (j--; j >= r; j--)
                free(*j);

        free(r);
        return q;
}
