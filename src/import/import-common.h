/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

int import_make_read_only_fd(int fd);
int import_make_read_only(const char *path);

int import_fork_tar_c(const char *path, pid_t *ret);
int import_fork_tar_x(const char *path, pid_t *ret);
