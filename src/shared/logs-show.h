/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
  This file is part of systemd.

  Copyright 2012 Lennart Poettering

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

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#include "sd-journal.h"

#include "macro.h"
#include "output-mode.h"
#include "time-util.h"
#include "util.h"

int output_journal(
                FILE *f,
                sd_journal *j,
                OutputMode mode,
                unsigned n_columns,
                OutputFlags flags,
                char **output_fields,
                size_t highlight[2],
                bool *ellipsized);

int add_match_this_boot(sd_journal *j, const char *machine);

int add_matches_for_unit(
                sd_journal *j,
                const char *unit);

int add_matches_for_user_unit(
                sd_journal *j,
                const char *unit,
                uid_t uid);

int show_journal_by_unit(
                FILE *f,
                const char *unit,
                OutputMode mode,
                unsigned n_columns,
                usec_t not_before,
                unsigned how_many,
                uid_t uid,
                OutputFlags flags,
                int journal_open_flags,
                bool system_unit,
                bool *ellipsized);

void json_escape(
                FILE *f,
                const char* p,
                size_t l,
                OutputFlags flags);
