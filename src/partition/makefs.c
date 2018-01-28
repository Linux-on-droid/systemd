/***
  SPDX-License-Identifier: LGPL-2.1+

  This file is part of systemd.

  Copyright 2017 Zbigniew Jędrzejewski-Szmek

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

#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "alloc-util.h"
#include "dissect-image.h"
#include "process-util.h"
#include "signal-util.h"
#include "string-util.h"

static int makefs(const char *type, const char *device) {
        const char *mkfs;
        pid_t pid;
        int r;

        if (streq(type, "swap"))
                mkfs = "/sbin/mkswap";
        else
                mkfs = strjoina("/sbin/mkfs.", type);
        if (access(mkfs, X_OK) != 0)
                return log_error_errno(errno, "%s is not executable: %m", mkfs);

        r = safe_fork("(fsck)", FORK_RESET_SIGNALS|FORK_DEATHSIG|FORK_LOG, &pid);
        if (r < 0)
                return r;
        if (r == 0) {
                const char *cmdline[3] = { mkfs, device, NULL };

                /* Child */

                execv(cmdline[0], (char**) cmdline);
                _exit(EXIT_FAILURE);
        }

        return wait_for_terminate_and_check(mkfs, pid, WAIT_LOG);
}

int main(int argc, char *argv[]) {
        const char *device, *type;
        _cleanup_free_ char *detected = NULL;
        struct stat st;
        int r;

        log_set_target(LOG_TARGET_AUTO);
        log_parse_environment();
        log_open();

        if (argc != 3) {
                log_error("This program expects two arguments.");
                return EXIT_FAILURE;
        }

        type = argv[1];
        device = argv[2];

        if (stat(device, &st) < 0) {
                r = log_error_errno(errno, "Failed to stat \"%s\": %m", device);
                goto finish;
        }

        if (!S_ISBLK(st.st_mode))
                log_info("%s is not a block device.", device);

        r = probe_filesystem(device, &detected);
        if (r < 0) {
                log_warning_errno(r,
                                  r == -EUCLEAN ?
                                  "Cannot reliably determine probe \"%s\", refusing to proceed." :
                                  "Failed to probe \"%s\": %m",
                                  device);
                goto finish;
        }

        if (detected) {
                log_info("%s is not empty (type %s), exiting", device, detected);
                goto finish;
        }

        r = makefs(type, device);

finish:
        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
