/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
  This file is part of systemd.

  Copyright 2010 Lennart Poettering
  Copyright 2010 Harald Hoyer

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

#include <stdio.h>

#include "fileio.h"

/* These functions are split out of fileio.h (and not for examplement just as flags to the functions they wrap) in
 * order to optimize linking: This way, -lselinux is needed only for the callers of these functions that need selinux,
 * but not for all */

int write_string_file_atomic_label_ts(const char *fn, const char *line, struct timespec *ts);
static inline int write_string_file_atomic_label(const char *fn, const char *line) {
        return write_string_file_atomic_label_ts(fn, line, NULL);
}
int write_env_file_label(const char *fname, char **l);
int fopen_temporary_label(const char *target, const char *path, FILE **f, char **temp_path);

int create_shutdown_run_nologin_or_warn(void);
