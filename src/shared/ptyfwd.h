/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
  This file is part of systemd.

  Copyright 2010-2013 Lennart Poettering

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

#include "sd-event.h"

#include "macro.h"

typedef struct PTYForward PTYForward;

typedef enum PTYForwardFlags {
        PTY_FORWARD_READ_ONLY = 1,

        /* Continue reading after hangup? */
        PTY_FORWARD_IGNORE_VHANGUP = 2,

        /* Continue reading after hangup but only if we never read anything else? */
        PTY_FORWARD_IGNORE_INITIAL_VHANGUP = 4,
} PTYForwardFlags;

typedef int (*PTYForwardHandler)(PTYForward *f, int rcode, void*userdata);

int pty_forward_new(sd_event *event, int master, PTYForwardFlags flags, PTYForward **f);
PTYForward *pty_forward_free(PTYForward *f);

int pty_forward_get_last_char(PTYForward *f, char *ch);

int pty_forward_set_ignore_vhangup(PTYForward *f, bool ignore_vhangup);
bool pty_forward_get_ignore_vhangup(PTYForward *f);

bool pty_forward_is_done(PTYForward *f);

void pty_forward_set_handler(PTYForward *f, PTYForwardHandler handler, void *userdata);

bool pty_forward_drain(PTYForward *f);

int pty_forward_set_priority(PTYForward *f, int64_t priority);

DEFINE_TRIVIAL_CLEANUP_FUNC(PTYForward*, pty_forward_free);
