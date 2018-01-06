/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2010 Lennart Poettering
  Copyright (C) 2012 Roberto Sassu - Politecnico di Torino, Italy
                                     TORSEC group — http://security.polito.it

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
#include <unistd.h>

#include "fd-util.h"
#include "fileio.h"
#include "ima-setup.h"
#include "log.h"
#include "util.h"

#define IMA_SECFS_DIR "/sys/kernel/security/ima"
#define IMA_SECFS_POLICY IMA_SECFS_DIR "/policy"
#define IMA_POLICY_PATH "/etc/ima/ima-policy"

int ima_setup(void) {
#if ENABLE_IMA
        _cleanup_fclose_ FILE *input = NULL;
        _cleanup_close_ int imafd = -1;
        unsigned lineno = 0;
        char line[page_size()];

        if (access(IMA_SECFS_DIR, F_OK) < 0) {
                log_debug("IMA support is disabled in the kernel, ignoring.");
                return 0;
        }

        if (access(IMA_SECFS_POLICY, W_OK) < 0) {
                log_warning("Another IMA custom policy has already been loaded, ignoring.");
                return 0;
        }

        if (access(IMA_POLICY_PATH, F_OK) < 0) {
                log_debug("No IMA custom policy file "IMA_POLICY_PATH", ignoring.");
                return 0;
        }

        imafd = open(IMA_SECFS_POLICY, O_WRONLY|O_CLOEXEC);
        if (imafd < 0) {
                log_error_errno(errno, "Failed to open the IMA kernel interface "IMA_SECFS_POLICY", ignoring: %m");
                return 0;
        }

        /* attempt to write the name of the policy file into sysfs file */
        if (write(imafd, IMA_POLICY_PATH, STRLEN(IMA_POLICY_PATH)) > 0)
                goto done;

        /* fall back to copying the policy line-by-line */
        input = fopen(IMA_POLICY_PATH, "re");
        if (!input) {
                log_warning_errno(errno, "Failed to open the IMA custom policy file "IMA_POLICY_PATH", ignoring: %m");
                return 0;
        }

        close(imafd);

        imafd = open(IMA_SECFS_POLICY, O_WRONLY|O_CLOEXEC);
        if (imafd < 0) {
                log_error_errno(errno, "Failed to open the IMA kernel interface "IMA_SECFS_POLICY", ignoring: %m");
                return 0;
        }

        FOREACH_LINE(line, input,
                     return log_error_errno(errno, "Failed to read the IMA custom policy file "IMA_POLICY_PATH": %m")) {
                size_t len;

                len = strlen(line);
                lineno++;

                if (len > 0 && write(imafd, line, len) < 0)
                        return log_error_errno(errno, "Failed to load the IMA custom policy file "IMA_POLICY_PATH"%u: %m",
                                               lineno);
        }

done:
        log_info("Successfully loaded the IMA custom policy "IMA_POLICY_PATH".");
#endif /* ENABLE_IMA */
        return 0;
}
