/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
  This file is part of systemd.

  Copyright 2014 Lennart Poettering

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

#include "machined.h"

extern const sd_bus_vtable image_vtable[];

char *image_bus_path(const char *name);

int image_object_find(sd_bus *bus, const char *path, const char *interface, void *userdata, void **found, sd_bus_error *error);
int image_node_enumerator(sd_bus *bus, const char *path, void *userdata, char ***nodes, sd_bus_error *error);

int bus_image_method_remove(sd_bus_message *message, void *userdata, sd_bus_error *error);
int bus_image_method_rename(sd_bus_message *message, void *userdata, sd_bus_error *error);
int bus_image_method_clone(sd_bus_message *message, void *userdata, sd_bus_error *error);
int bus_image_method_mark_read_only(sd_bus_message *message, void *userdata, sd_bus_error *error);
int bus_image_method_set_limit(sd_bus_message *message, void *userdata, sd_bus_error *error);
int bus_image_method_get_hostname(sd_bus_message *message, void *userdata, sd_bus_error *error);
int bus_image_method_get_machine_id(sd_bus_message *message, void *userdata, sd_bus_error *error);
int bus_image_method_get_machine_info(sd_bus_message *message, void *userdata, sd_bus_error *error);
int bus_image_method_get_os_release(sd_bus_message *message, void *userdata, sd_bus_error *error);
