/* SPDX-License-Identifier: LGPL-2.1+ */
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

#include <stdio_ext.h>

#include "alloc-util.h"
#include "async.h"
#include "bus-util.h"
#include "dbus-cgroup.h"
#include "dbus-execute.h"
#include "dbus-kill.h"
#include "dbus-service.h"
#include "fd-util.h"
#include "fileio.h"
#include "path-util.h"
#include "service.h"
#include "string-util.h"
#include "strv.h"
#include "unit.h"

static BUS_DEFINE_PROPERTY_GET_ENUM(property_get_type, service_type, ServiceType);
static BUS_DEFINE_PROPERTY_GET_ENUM(property_get_result, service_result, ServiceResult);
static BUS_DEFINE_PROPERTY_GET_ENUM(property_get_restart, service_restart, ServiceRestart);
static BUS_DEFINE_PROPERTY_GET_ENUM(property_get_notify_access, notify_access, NotifyAccess);
static BUS_DEFINE_PROPERTY_GET_ENUM(property_get_emergency_action, emergency_action, EmergencyAction);

const sd_bus_vtable bus_service_vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_PROPERTY("Type", "s", property_get_type, offsetof(Service, type), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("Restart", "s", property_get_restart, offsetof(Service, restart), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("PIDFile", "s", NULL, offsetof(Service, pid_file), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("NotifyAccess", "s", property_get_notify_access, offsetof(Service, notify_access), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("RestartUSec", "t", bus_property_get_usec, offsetof(Service, restart_usec), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("TimeoutStartUSec", "t", bus_property_get_usec, offsetof(Service, timeout_start_usec), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("TimeoutStopUSec", "t", bus_property_get_usec, offsetof(Service, timeout_stop_usec), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("RuntimeMaxUSec", "t", bus_property_get_usec, offsetof(Service, runtime_max_usec), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("WatchdogUSec", "t", bus_property_get_usec, offsetof(Service, watchdog_usec), SD_BUS_VTABLE_PROPERTY_CONST),
        BUS_PROPERTY_DUAL_TIMESTAMP("WatchdogTimestamp", offsetof(Service, watchdog_timestamp), 0),
        SD_BUS_PROPERTY("PermissionsStartOnly", "b", bus_property_get_bool, offsetof(Service, permissions_start_only), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("RootDirectoryStartOnly", "b", bus_property_get_bool, offsetof(Service, root_directory_start_only), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("RemainAfterExit", "b", bus_property_get_bool, offsetof(Service, remain_after_exit), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("GuessMainPID", "b", bus_property_get_bool, offsetof(Service, guess_main_pid), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("MainPID", "u", bus_property_get_pid, offsetof(Service, main_pid), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("ControlPID", "u", bus_property_get_pid, offsetof(Service, control_pid), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("BusName", "s", NULL, offsetof(Service, bus_name), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("FileDescriptorStoreMax", "u", bus_property_get_unsigned, offsetof(Service, n_fd_store_max), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("NFileDescriptorStore", "u", bus_property_get_unsigned, offsetof(Service, n_fd_store), 0),
        SD_BUS_PROPERTY("StatusText", "s", NULL, offsetof(Service, status_text), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("StatusErrno", "i", NULL, offsetof(Service, status_errno), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("Result", "s", property_get_result, offsetof(Service, result), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("USBFunctionDescriptors", "s", NULL, offsetof(Service, usb_function_descriptors), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("USBFunctionStrings", "s", NULL, offsetof(Service, usb_function_strings), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("UID", "u", NULL, offsetof(Unit, ref_uid), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("GID", "u", NULL, offsetof(Unit, ref_gid), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("NRestarts", "u", bus_property_get_unsigned, offsetof(Service, n_restarts), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),

        BUS_EXEC_STATUS_VTABLE("ExecMain", offsetof(Service, main_exec_status), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        BUS_EXEC_COMMAND_LIST_VTABLE("ExecStartPre", offsetof(Service, exec_command[SERVICE_EXEC_START_PRE]), SD_BUS_VTABLE_PROPERTY_EMITS_INVALIDATION),
        BUS_EXEC_COMMAND_LIST_VTABLE("ExecStart", offsetof(Service, exec_command[SERVICE_EXEC_START]), SD_BUS_VTABLE_PROPERTY_EMITS_INVALIDATION),
        BUS_EXEC_COMMAND_LIST_VTABLE("ExecStartPost", offsetof(Service, exec_command[SERVICE_EXEC_START_POST]), SD_BUS_VTABLE_PROPERTY_EMITS_INVALIDATION),
        BUS_EXEC_COMMAND_LIST_VTABLE("ExecReload", offsetof(Service, exec_command[SERVICE_EXEC_RELOAD]), SD_BUS_VTABLE_PROPERTY_EMITS_INVALIDATION),
        BUS_EXEC_COMMAND_LIST_VTABLE("ExecStop", offsetof(Service, exec_command[SERVICE_EXEC_STOP]), SD_BUS_VTABLE_PROPERTY_EMITS_INVALIDATION),
        BUS_EXEC_COMMAND_LIST_VTABLE("ExecStopPost", offsetof(Service, exec_command[SERVICE_EXEC_STOP_POST]), SD_BUS_VTABLE_PROPERTY_EMITS_INVALIDATION),

        /* The following four are obsolete, and thus marked hidden here. They moved into the Unit interface */
        SD_BUS_PROPERTY("StartLimitInterval", "t", bus_property_get_usec, offsetof(Unit, start_limit.interval), SD_BUS_VTABLE_PROPERTY_CONST|SD_BUS_VTABLE_HIDDEN),
        SD_BUS_PROPERTY("StartLimitBurst", "u", bus_property_get_unsigned, offsetof(Unit, start_limit.burst), SD_BUS_VTABLE_PROPERTY_CONST|SD_BUS_VTABLE_HIDDEN),
        SD_BUS_PROPERTY("StartLimitAction", "s", property_get_emergency_action, offsetof(Unit, start_limit_action), SD_BUS_VTABLE_PROPERTY_CONST|SD_BUS_VTABLE_HIDDEN),
        SD_BUS_PROPERTY("FailureAction", "s", property_get_emergency_action, offsetof(Unit, failure_action), SD_BUS_VTABLE_PROPERTY_CONST|SD_BUS_VTABLE_HIDDEN),
        SD_BUS_PROPERTY("RebootArgument", "s", NULL, offsetof(Unit, reboot_arg), SD_BUS_VTABLE_PROPERTY_CONST|SD_BUS_VTABLE_HIDDEN),
        SD_BUS_VTABLE_END
};

static int bus_service_set_transient_property(
                Service *s,
                const char *name,
                sd_bus_message *message,
                UnitWriteFlags flags,
                sd_bus_error *error) {

        ServiceExecCommand ci;
        int r;

        assert(s);
        assert(name);
        assert(message);

        flags |= UNIT_PRIVATE;

        if (streq(name, "RemainAfterExit")) {
                int b;

                r = sd_bus_message_read(message, "b", &b);
                if (r < 0)
                        return r;

                if (!UNIT_WRITE_FLAGS_NOOP(flags)) {
                        s->remain_after_exit = b;
                        unit_write_settingf(UNIT(s), flags, name, "RemainAfterExit=%s", yes_no(b));
                }

                return 1;

        } else if (streq(name, "Type")) {
                const char *t;
                ServiceType k;

                r = sd_bus_message_read(message, "s", &t);
                if (r < 0)
                        return r;

                k = service_type_from_string(t);
                if (k < 0)
                        return sd_bus_error_setf(error, SD_BUS_ERROR_INVALID_ARGS, "Invalid service type %s", t);

                if (!UNIT_WRITE_FLAGS_NOOP(flags)) {
                        s->type = k;
                        unit_write_settingf(UNIT(s), flags, name, "Type=%s", service_type_to_string(s->type));
                }

                return 1;
        } else if (streq(name, "RuntimeMaxUSec")) {
                usec_t u;

                r = sd_bus_message_read(message, "t", &u);
                if (r < 0)
                        return r;

                if (!UNIT_WRITE_FLAGS_NOOP(flags)) {
                        s->runtime_max_usec = u;
                        unit_write_settingf(UNIT(s), flags, name, "RuntimeMaxSec=" USEC_FMT "us", u);
                }

                return 1;

        } else if (streq(name, "Restart")) {
                ServiceRestart sr;
                const char *v;

                r = sd_bus_message_read(message, "s", &v);
                if (r < 0)
                        return r;

                if (isempty(v))
                        sr = SERVICE_RESTART_NO;
                else {
                        sr = service_restart_from_string(v);
                        if (sr < 0)
                                return sd_bus_error_setf(error, SD_BUS_ERROR_INVALID_ARGS, "Invalid restart setting: %s", v);
                }

                if (!UNIT_WRITE_FLAGS_NOOP(flags)) {
                        s->restart = sr;
                        unit_write_settingf(UNIT(s), flags, name, "Restart=%s", service_restart_to_string(sr));
                }

                return 1;

        } else if (STR_IN_SET(name,
                              "StandardInputFileDescriptor",
                              "StandardOutputFileDescriptor",
                              "StandardErrorFileDescriptor")) {
                int fd;

                r = sd_bus_message_read(message, "h", &fd);
                if (r < 0)
                        return r;

                if (!UNIT_WRITE_FLAGS_NOOP(flags)) {
                        int copy;

                        copy = fcntl(fd, F_DUPFD_CLOEXEC, 3);
                        if (copy < 0)
                                return -errno;

                        if (streq(name, "StandardInputFileDescriptor")) {
                                asynchronous_close(s->stdin_fd);
                                s->stdin_fd = copy;
                        } else if (streq(name, "StandardOutputFileDescriptor")) {
                                asynchronous_close(s->stdout_fd);
                                s->stdout_fd = copy;
                        } else {
                                asynchronous_close(s->stderr_fd);
                                s->stderr_fd = copy;
                        }

                        s->exec_context.stdio_as_fds = true;
                }

                return 1;

        } else if (streq(name, "FileDescriptorStoreMax")) {
                uint32_t u;

                r = sd_bus_message_read(message, "u", &u);
                if (r < 0)
                        return r;

                if (!UNIT_WRITE_FLAGS_NOOP(flags)) {
                        s->n_fd_store_max = (unsigned) u;
                        unit_write_settingf(UNIT(s), flags, name, "FileDescriptorStoreMax=%" PRIu32, u);
                }

                return 1;

        } else if (streq(name, "NotifyAccess")) {
                const char *t;
                NotifyAccess k;

                r = sd_bus_message_read(message, "s", &t);
                if (r < 0)
                        return r;

                k = notify_access_from_string(t);
                if (k < 0)
                        return sd_bus_error_setf(error, SD_BUS_ERROR_INVALID_ARGS, "Invalid notify access setting %s", t);

                if (!UNIT_WRITE_FLAGS_NOOP(flags)) {
                        s->notify_access = k;
                        unit_write_settingf(UNIT(s), flags, name, "NotifyAccess=%s", notify_access_to_string(s->notify_access));
                }

                return 1;

        } else if ((ci = service_exec_command_from_string(name)) >= 0) {
                unsigned n = 0;

                r = sd_bus_message_enter_container(message, 'a', "(sasb)");
                if (r < 0)
                        return r;

                while ((r = sd_bus_message_enter_container(message, 'r', "sasb")) > 0) {
                        _cleanup_strv_free_ char **argv = NULL;
                        const char *path;
                        int b;

                        r = sd_bus_message_read(message, "s", &path);
                        if (r < 0)
                                return r;

                        if (!path_is_absolute(path))
                                return sd_bus_error_setf(error, SD_BUS_ERROR_INVALID_ARGS, "Path %s is not absolute.", path);

                        r = sd_bus_message_read_strv(message, &argv);
                        if (r < 0)
                                return r;

                        r = sd_bus_message_read(message, "b", &b);
                        if (r < 0)
                                return r;

                        r = sd_bus_message_exit_container(message);
                        if (r < 0)
                                return r;

                        if (!UNIT_WRITE_FLAGS_NOOP(flags)) {
                                ExecCommand *c;

                                c = new0(ExecCommand, 1);
                                if (!c)
                                        return -ENOMEM;

                                c->path = strdup(path);
                                if (!c->path) {
                                        free(c);
                                        return -ENOMEM;
                                }

                                c->argv = argv;
                                argv = NULL;

                                c->flags = b ? EXEC_COMMAND_IGNORE_FAILURE : 0;

                                path_kill_slashes(c->path);
                                exec_command_append_list(&s->exec_command[ci], c);
                        }

                        n++;
                }

                if (r < 0)
                        return r;

                r = sd_bus_message_exit_container(message);
                if (r < 0)
                        return r;

                if (!UNIT_WRITE_FLAGS_NOOP(flags)) {
                        _cleanup_free_ char *buf = NULL;
                        _cleanup_fclose_ FILE *f = NULL;
                        ExecCommand *c;
                        size_t size = 0;

                        if (n == 0)
                                s->exec_command[ci] = exec_command_free_list(s->exec_command[ci]);

                        f = open_memstream(&buf, &size);
                        if (!f)
                                return -ENOMEM;

                        (void) __fsetlocking(f, FSETLOCKING_BYCALLER);

                        fputs("ExecStart=\n", f);

                        LIST_FOREACH(command, c, s->exec_command[ci]) {
                                _cleanup_free_ char *a = NULL, *t = NULL;
                                const char *p;

                                p = unit_escape_setting(c->path, UNIT_ESCAPE_C|UNIT_ESCAPE_SPECIFIERS, &t);
                                if (!p)
                                        return -ENOMEM;

                                a = unit_concat_strv(c->argv, UNIT_ESCAPE_C|UNIT_ESCAPE_SPECIFIERS);
                                if (!a)
                                        return -ENOMEM;

                                fprintf(f, "%s=%s@%s %s\n",
                                        name,
                                        c->flags & EXEC_COMMAND_IGNORE_FAILURE ? "-" : "",
                                        p,
                                        a);
                        }

                        r = fflush_and_check(f);
                        if (r < 0)
                                return r;

                        unit_write_setting(UNIT(s), flags, name, buf);
                }

                return 1;
        }

        return 0;
}

int bus_service_set_property(
                Unit *u,
                const char *name,
                sd_bus_message *message,
                UnitWriteFlags flags,
                sd_bus_error *error) {

        Service *s = SERVICE(u);
        int r;

        assert(s);
        assert(name);
        assert(message);

        r = bus_cgroup_set_property(u, &s->cgroup_context, name, message, flags, error);
        if (r != 0)
                return r;

        if (u->transient && u->load_state == UNIT_STUB) {
                /* This is a transient unit, let's load a little more */

                r = bus_service_set_transient_property(s, name, message, flags, error);
                if (r != 0)
                        return r;

                r = bus_exec_context_set_transient_property(u, &s->exec_context, name, message, flags, error);
                if (r != 0)
                        return r;

                r = bus_kill_context_set_transient_property(u, &s->kill_context, name, message, flags, error);
                if (r != 0)
                        return r;
        }

        return 0;
}

int bus_service_commit_properties(Unit *u) {
        assert(u);

        unit_update_cgroup_members_masks(u);
        unit_realize_cgroup(u);

        return 0;
}
