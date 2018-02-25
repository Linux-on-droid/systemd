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
#include "dbus-util.h"
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

static BUS_DEFINE_SET_TRANSIENT_PARSE(kill_mode, KillMode, kill_mode_from_string);
static BUS_DEFINE_SET_TRANSIENT_TO_STRING(kill_signal, "i", int32_t, int, "%" PRIi32, signal_to_string_with_check);

int bus_kill_context_set_transient_property(
                Unit *u,
                KillContext *c,
                const char *name,
                sd_bus_message *message,
                UnitWriteFlags flags,
                sd_bus_error *error) {

        assert(u);
        assert(c);
        assert(name);
        assert(message);

        flags |= UNIT_PRIVATE;

        if (streq(name, "KillMode"))
                return bus_set_transient_kill_mode(u, name, &c->kill_mode, message, flags, error);

        if (streq(name, "SendSIGHUP"))
                return bus_set_transient_bool(u, name, &c->send_sighup, message, flags, error);

        if (streq(name, "SendSIGKILL"))
                return bus_set_transient_bool(u, name, &c->send_sigkill, message, flags, error);

        if (streq(name, "KillSignal"))
                return bus_set_transient_kill_signal(u, name, &c->kill_signal, message, flags, error);

        return 0;
}
