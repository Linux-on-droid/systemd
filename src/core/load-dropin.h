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

#include "dropin.h"
#include "unit.h"

/* Read service data supplementary drop-in directories */

static inline int unit_find_dropin_paths(Unit *u, char ***paths) {
        return unit_file_find_dropin_conf_paths(NULL,
                                                u->manager->lookup_paths.search_path,
                                                u->manager->unit_path_cache,
                                                u->names,
                                                paths);
}

int unit_load_dropin(Unit *u);
