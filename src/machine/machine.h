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

typedef struct Machine Machine;
typedef enum KillWho KillWho;

#include "list.h"
#include "machined.h"
#include "operation.h"

typedef enum MachineState {
        MACHINE_OPENING,    /* Machine is being registered */
        MACHINE_RUNNING,    /* Machine is running */
        MACHINE_CLOSING,    /* Machine is terminating */
        _MACHINE_STATE_MAX,
        _MACHINE_STATE_INVALID = -1
} MachineState;

typedef enum MachineClass {
        MACHINE_CONTAINER,
        MACHINE_VM,
        MACHINE_HOST,
        _MACHINE_CLASS_MAX,
        _MACHINE_CLASS_INVALID = -1
} MachineClass;

enum KillWho {
        KILL_LEADER,
        KILL_ALL,
        _KILL_WHO_MAX,
        _KILL_WHO_INVALID = -1
};

struct Machine {
        Manager *manager;

        char *name;
        sd_id128_t id;

        MachineClass class;

        char *state_file;
        char *service;
        char *root_directory;

        char *unit;
        char *scope_job;

        pid_t leader;

        dual_timestamp timestamp;

        bool in_gc_queue:1;
        bool started:1;
        bool stopping:1;

        sd_bus_message *create_message;

        int *netif;
        unsigned n_netif;

        LIST_HEAD(Operation, operations);

        LIST_FIELDS(Machine, gc_queue);
};

Machine* machine_new(Manager *manager, MachineClass class, const char *name);
void machine_free(Machine *m);
bool machine_may_gc(Machine *m, bool drop_not_started);
void machine_add_to_gc_queue(Machine *m);
int machine_start(Machine *m, sd_bus_message *properties, sd_bus_error *error);
int machine_stop(Machine *m);
int machine_finalize(Machine *m);
int machine_save(Machine *m);
int machine_load(Machine *m);
int machine_kill(Machine *m, KillWho who, int signo);

void machine_release_unit(Machine *m);

MachineState machine_get_state(Machine *u);

const char* machine_class_to_string(MachineClass t) _const_;
MachineClass machine_class_from_string(const char *s) _pure_;

const char* machine_state_to_string(MachineState t) _const_;
MachineState machine_state_from_string(const char *s) _pure_;

const char *kill_who_to_string(KillWho k) _const_;
KillWho kill_who_from_string(const char *s) _pure_;

int machine_openpt(Machine *m, int flags);
int machine_open_terminal(Machine *m, const char *path, int mode);

int machine_get_uid_shift(Machine *m, uid_t *ret);
