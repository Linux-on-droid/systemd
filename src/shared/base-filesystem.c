/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2014 Kay Sievers

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

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include "alloc-util.h"
#include "base-filesystem.h"
#include "fd-util.h"
#include "log.h"
#include "macro.h"
#include "string-util.h"
#include "umask-util.h"
#include "user-util.h"
#include "util.h"

typedef struct BaseFilesystem {
        const char *dir;
        mode_t mode;
        const char *target;
        const char *exists;
        bool ignore_failure;
} BaseFilesystem;

static const BaseFilesystem table[] = {
        { "bin",      0, "usr/bin\0",                  NULL },
        { "lib",      0, "usr/lib\0",                  NULL },
        { "root",  0755, NULL,                         NULL, true },
        { "sbin",     0, "usr/sbin\0",                 NULL },
        { "usr",   0755, NULL,                         NULL },
        { "var",   0755, NULL,                         NULL },
        { "etc",   0755, NULL,                         NULL },
        { "proc",  0755, NULL,                         NULL, true },
        { "sys",   0755, NULL,                         NULL, true },
        { "dev",   0755, NULL,                         NULL, true },
#if defined(__i386__) || defined(__x86_64__)
        { "lib64",    0, "usr/lib/x86_64-linux-gnu\0"
                         "usr/lib64\0",                "ld-linux-x86-64.so.2" },
#endif
};

int base_filesystem_create(const char *root, uid_t uid, gid_t gid) {
        _cleanup_close_ int fd = -1;
        unsigned i;
        int r = 0;

        fd = open(root, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if (fd < 0)
                return log_error_errno(errno, "Failed to open root file system: %m");

        for (i = 0; i < ELEMENTSOF(table); i ++) {
                if (faccessat(fd, table[i].dir, F_OK, AT_SYMLINK_NOFOLLOW) >= 0)
                        continue;

                if (table[i].target) {
                        const char *target = NULL, *s;

                        /* check if one of the targets exists */
                        NULSTR_FOREACH(s, table[i].target) {
                                if (faccessat(fd, s, F_OK, AT_SYMLINK_NOFOLLOW) < 0)
                                        continue;

                                /* check if a specific file exists at the target path */
                                if (table[i].exists) {
                                        _cleanup_free_ char *p = NULL;

                                        p = strjoin(s, "/", table[i].exists);
                                        if (!p)
                                                return log_oom();

                                        if (faccessat(fd, p, F_OK, AT_SYMLINK_NOFOLLOW) < 0)
                                                continue;
                                }

                                target = s;
                                break;
                        }

                        if (!target)
                                continue;

                        r = symlinkat(target, fd, table[i].dir);
                        if (r < 0 && errno != EEXIST)
                                return log_error_errno(errno, "Failed to create symlink at %s/%s: %m", root, table[i].dir);

                        if (uid_is_valid(uid) || gid_is_valid(gid)) {
                                if (fchownat(fd, table[i].dir, uid, gid, AT_SYMLINK_NOFOLLOW) < 0)
                                        return log_error_errno(errno, "Failed to chown symlink at %s/%s: %m", root, table[i].dir);
                        }

                        continue;
                }

                RUN_WITH_UMASK(0000)
                        r = mkdirat(fd, table[i].dir, table[i].mode);
                if (r < 0 && errno != EEXIST) {
                        log_full_errno(table[i].ignore_failure ? LOG_DEBUG : LOG_ERR, errno,
                                       "Failed to create directory at %s/%s: %m", root, table[i].dir);

                        if (!table[i].ignore_failure)
                                return -errno;

                        continue;
                }

                if (uid != UID_INVALID || gid != UID_INVALID) {
                        if (fchownat(fd, table[i].dir, uid, gid, AT_SYMLINK_NOFOLLOW) < 0)
                                return log_error_errno(errno, "Failed to chown directory at %s/%s: %m", root, table[i].dir);
                }
        }

        return 0;
}
