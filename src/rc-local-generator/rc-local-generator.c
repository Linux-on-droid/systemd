/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2010 Lennart Poettering
  Copyright 2011 Michal Schmidt

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

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "log.h"
#include "mkdir.h"
#include "string-util.h"
#include "util.h"

static const char *arg_dest = "/tmp";

static int add_symlink(const char *service, const char *where) {
        const char *from, *to;
        int r;

        assert(service);
        assert(where);

        from = strjoina(SYSTEM_DATA_UNIT_PATH "/", service);
        to = strjoina(arg_dest, "/", where, ".wants/", service);

        (void) mkdir_parents_label(to, 0755);

        r = symlink(from, to);
        if (r < 0) {
                if (errno == EEXIST)
                        return 0;

                return log_error_errno(errno, "Failed to create symlink %s: %m", to);
        }

        return 1;
}

int main(int argc, char *argv[]) {
        int ret = EXIT_SUCCESS;

        if (argc > 1 && argc != 4) {
                log_error("This program takes three or no arguments.");
                return EXIT_FAILURE;
        }

        if (argc > 1)
                arg_dest = argv[1];

        log_set_prohibit_ipc(true);
        log_set_target(LOG_TARGET_AUTO);
        log_parse_environment();
        log_open();

        umask(0022);

        if (access(RC_LOCAL_SCRIPT_PATH_START, X_OK) >= 0) {
                log_debug("Automatically adding rc-local.service.");

                if (add_symlink("rc-local.service", "multi-user.target") < 0)
                        ret = EXIT_FAILURE;
        }

        if (access(RC_LOCAL_SCRIPT_PATH_STOP, X_OK) >= 0) {
                log_debug("Automatically adding halt-local.service.");

                if (add_symlink("halt-local.service", "final.target") < 0)
                        ret = EXIT_FAILURE;
        }

        return ret;
}
