/* SPDX-License-Identifier: LGPL-2.1+ */
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

#include "sd-bus.h"

#include "bus-dump.h"
#include "bus-util.h"
#include "cgroup-util.h"

int main(int argc, char *argv[]) {
        _cleanup_(sd_bus_creds_unrefp) sd_bus_creds *creds = NULL;
        int r;

        log_set_max_level(LOG_DEBUG);
        log_parse_environment();
        log_open();

        if (cg_unified_flush() == -ENOMEDIUM) {
                log_info("Skipping test: /sys/fs/cgroup/ not available");
                return EXIT_TEST_SKIP;
        }

        r = sd_bus_creds_new_from_pid(&creds, 0, _SD_BUS_CREDS_ALL);
        log_full_errno(r < 0 ? LOG_ERR : LOG_DEBUG, r, "sd_bus_creds_new_from_pid: %m");
        assert_se(r >= 0);

        bus_creds_dump(creds, NULL, true);

        creds = sd_bus_creds_unref(creds);

        r = sd_bus_creds_new_from_pid(&creds, 1, _SD_BUS_CREDS_ALL);
        if (r != -EACCES) {
                assert_se(r >= 0);
                putchar('\n');
                bus_creds_dump(creds, NULL, true);
        }

        return 0;
}
