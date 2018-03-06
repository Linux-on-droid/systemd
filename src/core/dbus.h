/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

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

#include "manager.h"

int bus_send_queued_message(Manager *m);

int bus_init_private(Manager *m);
int bus_init_api(Manager *m);
int bus_init_system(Manager *m);

void bus_done_private(Manager *m);
void bus_done_api(Manager *m);
void bus_done_system(Manager *m);
void bus_done(Manager *m);

int bus_fdset_add_all(Manager *m, FDSet *fds);

void bus_track_serialize(sd_bus_track *t, FILE *f, const char *prefix);
int bus_track_coldplug(Manager *m, sd_bus_track **t, bool recursive, char **l);

int manager_enqueue_sync_bus_names(Manager *m);

int bus_foreach_bus(Manager *m, sd_bus_track *subscribed2, int (*send_message)(sd_bus *bus, void *userdata), void *userdata);

int bus_verify_manage_units_async(Manager *m, sd_bus_message *call, sd_bus_error *error);
int bus_verify_manage_unit_files_async(Manager *m, sd_bus_message *call, sd_bus_error *error);
int bus_verify_reload_daemon_async(Manager *m, sd_bus_message *call, sd_bus_error *error);
int bus_verify_set_environment_async(Manager *m, sd_bus_message *call, sd_bus_error *error);

int bus_forward_agent_released(Manager *m, const char *path);

uint64_t manager_bus_n_queued_write(Manager *m);
