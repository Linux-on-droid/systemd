/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
  This file is part of systemd.

  Copyright 2013 Lennart Poettering

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

typedef struct Scope Scope;

#include "cgroup.h"
#include "kill.h"
#include "unit.h"

typedef enum ScopeResult {
        SCOPE_SUCCESS,
        SCOPE_FAILURE_RESOURCES,
        SCOPE_FAILURE_TIMEOUT,
        _SCOPE_RESULT_MAX,
        _SCOPE_RESULT_INVALID = -1
} ScopeResult;

struct Scope {
        Unit meta;

        CGroupContext cgroup_context;
        KillContext kill_context;

        ScopeState state, deserialized_state;
        ScopeResult result;

        usec_t timeout_stop_usec;

        char *controller;
        sd_bus_track *controller_track;

        bool was_abandoned;

        sd_event_source *timer_event_source;
};

extern const UnitVTable scope_vtable;

int scope_abandon(Scope *s);

const char* scope_result_to_string(ScopeResult i) _const_;
ScopeResult scope_result_from_string(const char *s) _pure_;
