/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

#include <stdbool.h>
/***
  This file is part of systemd.

  Copyright 2012 Harald Hoyer, Lennart Poettering

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

int switch_root(const char *new_root, const char *oldroot, bool detach_oldroot, unsigned long mountflags);
