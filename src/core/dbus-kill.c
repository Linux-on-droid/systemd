/* SPDX-License-Identifier: LGPL-2.1+ */
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

#include "bus-util.h"
#include "dbus-kill.h"
#include "kill.h"
#include "signal-util.h"

static BUS_DEFINE_PROPERTY_GET_ENUM(property_get_kill_mode, kill_mode, KillMode);

const sd_bus_vtable bus_kill_vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_PROPERTY("KillMode", "s", property_get_kill_mode, offsetof(KillContext, kill_mode), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("KillSignal", "i", bus_property_get_int, offsetof(KillContext, kill_signal), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("SendSIGKILL", "b", bus_property_get_bool, offsetof(KillContext, send_sigkill), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("SendSIGHUP", "b", bus_property_get_bool,  offsetof(KillContext, send_sighup), SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_VTABLE_END
};

int bus_kill_context_set_transient_property(
                Unit *u,
                KillContext *c,
                const char *name,
                sd_bus_message *message,
                UnitWriteFlags flags,
                sd_bus_error *error) {

        int r;

        assert(u);
        assert(c);
        assert(name);
        assert(message);

        flags |= UNIT_PRIVATE;

        if (streq(name, "KillMode")) {
                const char *m;
                KillMode k;

                r = sd_bus_message_read(message, "s", &m);
                if (r < 0)
                        return r;

                k = kill_mode_from_string(m);
                if (k < 0)
                        return sd_bus_error_setf(error, SD_BUS_ERROR_INVALID_ARGS, "Kill mode '%s' not known.", m);

                if (!UNIT_WRITE_FLAGS_NOOP(flags)) {
                        c->kill_mode = k;

                        unit_write_settingf(u, flags, name, "KillMode=%s", kill_mode_to_string(k));
                }

                return 1;

        } else if (streq(name, "KillSignal")) {
                int sig;

                r = sd_bus_message_read(message, "i", &sig);
                if (r < 0)
                        return r;

                if (!SIGNAL_VALID(sig))
                        return sd_bus_error_setf(error, SD_BUS_ERROR_INVALID_ARGS, "Signal %i out of range", sig);

                if (!UNIT_WRITE_FLAGS_NOOP(flags)) {
                        c->kill_signal = sig;

                        unit_write_settingf(u, flags, name, "KillSignal=%s", signal_to_string(sig));
                }

                return 1;

        } else if (streq(name, "SendSIGHUP")) {
                int b;

                r = sd_bus_message_read(message, "b", &b);
                if (r < 0)
                        return r;

                if (!UNIT_WRITE_FLAGS_NOOP(flags)) {
                        c->send_sighup = b;

                        unit_write_settingf(u, flags, name, "SendSIGHUP=%s", yes_no(b));
                }

                return 1;

        } else if (streq(name, "SendSIGKILL")) {
                int b;

                r = sd_bus_message_read(message, "b", &b);
                if (r < 0)
                        return r;

                if (!UNIT_WRITE_FLAGS_NOOP(flags)) {
                        c->send_sigkill = b;

                        unit_write_settingf(u, flags, name, "SendSIGKILL=%s", yes_no(b));
                }

                return 1;

        }

        return 0;
}
