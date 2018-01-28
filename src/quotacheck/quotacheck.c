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

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "proc-cmdline.h"
#include "process-util.h"
#include "signal-util.h"
#include "string-util.h"
#include "util.h"

static bool arg_skip = false;
static bool arg_force = false;

static int parse_proc_cmdline_item(const char *key, const char *value, void *data) {

        if (streq(key, "quotacheck.mode")) {

                if (proc_cmdline_value_missing(key, value))
                        return 0;

                if (streq(value, "auto"))
                        arg_force = arg_skip = false;
                else if (streq(value, "force"))
                        arg_force = true;
                else if (streq(value, "skip"))
                        arg_skip = true;
                else
                        log_warning("Invalid quotacheck.mode= parameter '%s'. Ignoring.", value);
        }

#if HAVE_SYSV_COMPAT
        else if (streq(key, "forcequotacheck") && !value) {
                log_warning("Please use 'quotacheck.mode=force' rather than 'forcequotacheck' on the kernel command line.");
                arg_force = true;
        }
#endif

        return 0;
}

static void test_files(void) {

#if HAVE_SYSV_COMPAT
        if (access("/forcequotacheck", F_OK) >= 0) {
                log_error("Please pass 'quotacheck.mode=force' on the kernel command line rather than creating /forcequotacheck on the root file system.");
                arg_force = true;
        }
#endif
}

int main(int argc, char *argv[]) {
        int r;

        if (argc > 1) {
                log_error("This program takes no arguments.");
                return EXIT_FAILURE;
        }

        log_set_target(LOG_TARGET_AUTO);
        log_parse_environment();
        log_open();

        umask(0022);

        r = proc_cmdline_parse(parse_proc_cmdline_item, NULL, 0);
        if (r < 0)
                log_warning_errno(r, "Failed to parse kernel command line, ignoring: %m");

        test_files();

        if (!arg_force) {
                if (arg_skip)
                        return EXIT_SUCCESS;

                if (access("/run/systemd/quotacheck", F_OK) < 0)
                        return EXIT_SUCCESS;
        }

        r = safe_fork("(quotacheck)", FORK_RESET_SIGNALS|FORK_DEATHSIG|FORK_LOG|FORK_WAIT, NULL);
        if (r < 0)
                goto finish;
        if (r == 0) {
                static const char * const cmdline[] = {
                        QUOTACHECK,
                        "-anug",
                        NULL
                };

                /* Child */

                execv(cmdline[0], (char**) cmdline);
                _exit(EXIT_FAILURE); /* Operational error */
        }

finish:
        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
