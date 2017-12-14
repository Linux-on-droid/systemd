/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
  This file is part of systemd.

  Copyright 2015 Lennart Poettering

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

#include <sys/types.h>

#include "sd-id128.h"

#include "nspawn-mount.h"

int register_machine(sd_bus *bus, const char *machine_name, pid_t pid, const char *directory, sd_id128_t uuid, int local_ifindex, const char *slice, CustomMount *mounts, unsigned n_mounts, int kill_signal, char **properties, bool keep_unit, const char *service);
int terminate_machine(sd_bus *bus, pid_t pid);

int allocate_scope(sd_bus *bus, const char *machine_name, pid_t pid, const char *slice, CustomMount *mounts, unsigned n_mounts, int kill_signal, char **properties);
