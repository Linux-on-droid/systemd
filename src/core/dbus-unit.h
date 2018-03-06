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

#include "sd-bus.h"

#include "unit.h"

extern const sd_bus_vtable bus_unit_vtable[];
extern const sd_bus_vtable bus_unit_cgroup_vtable[];

void bus_unit_send_change_signal(Unit *u);
void bus_unit_send_removed_signal(Unit *u);

int bus_unit_method_start_generic(sd_bus_message *message, Unit *u, JobType job_type, bool reload_if_possible, sd_bus_error *error);
int bus_unit_method_kill(sd_bus_message *message, void *userdata, sd_bus_error *error);
int bus_unit_method_reset_failed(sd_bus_message *message, void *userdata, sd_bus_error *error);

int bus_unit_set_properties(Unit *u, sd_bus_message *message, UnitWriteFlags flags, bool commit, sd_bus_error *error);
int bus_unit_method_set_properties(sd_bus_message *message, void *userdata, sd_bus_error *error);
int bus_unit_method_get_processes(sd_bus_message *message, void *userdata, sd_bus_error *error);
int bus_unit_method_attach_processes(sd_bus_message *message, void *userdata, sd_bus_error *error);
int bus_unit_method_ref(sd_bus_message *message, void *userdata, sd_bus_error *error);
int bus_unit_method_unref(sd_bus_message *message, void *userdata, sd_bus_error *error);

int bus_unit_queue_job(sd_bus_message *message, Unit *u, JobType type, JobMode mode, bool reload_if_possible, sd_bus_error *error);
int bus_unit_check_load_state(Unit *u, sd_bus_error *error);

int bus_unit_track_add_name(Unit *u, const char *name);
int bus_unit_track_add_sender(Unit *u, sd_bus_message *m);
int bus_unit_track_remove_sender(Unit *u, sd_bus_message *m);
