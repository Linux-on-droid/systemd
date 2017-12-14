/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2014 Lennart Poettering

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

#include "alloc-util.h"
#include "fd-util.h"
#include "io-util.h"
#include "parse-util.h"
#include "show-status.h"
#include "string-util.h"
#include "terminal-util.h"
#include "util.h"

int parse_show_status(const char *v, ShowStatus *ret) {
        int r;

        assert(v);
        assert(ret);

        if (streq(v, "auto")) {
                *ret = SHOW_STATUS_AUTO;
                return 0;
        }

        r = parse_boolean(v);
        if (r < 0)
                return r;

        *ret = r ? SHOW_STATUS_YES : SHOW_STATUS_NO;
        return 0;
}

int status_vprintf(const char *status, bool ellipse, bool ephemeral, const char *format, va_list ap) {
        static const char status_indent[] = "         "; /* "[" STATUS "] " */
        _cleanup_free_ char *s = NULL;
        _cleanup_close_ int fd = -1;
        struct iovec iovec[6] = {};
        int n = 0;
        static bool prev_ephemeral;

        assert(format);

        /* This is independent of logging, as status messages are
         * optional and go exclusively to the console. */

        if (vasprintf(&s, format, ap) < 0)
                return log_oom();

        /* Before you ask: yes, on purpose we open/close the console for each status line we write individually. This
         * is a good strategy to avoid PID 1 getting killed by the kernel's SAK concept (it doesn't fix this entirely,
         * but minimizes the time window the kernel might end up killing PID 1 due to SAK). It also makes things easier
         * for us so that we don't have to recover from hangups and suchlike triggered on the console. */

        fd = open_terminal("/dev/console", O_WRONLY|O_NOCTTY|O_CLOEXEC);
        if (fd < 0)
                return fd;

        if (ellipse) {
                char *e;
                size_t emax, sl;
                int c;

                c = fd_columns(fd);
                if (c <= 0)
                        c = 80;

                sl = status ? sizeof(status_indent)-1 : 0;

                emax = c - sl - 1;
                if (emax < 3)
                        emax = 3;

                e = ellipsize(s, emax, 50);
                if (e) {
                        free(s);
                        s = e;
                }
        }

        if (prev_ephemeral)
                iovec[n++] = IOVEC_MAKE_STRING("\r" ANSI_ERASE_TO_END_OF_LINE);
        prev_ephemeral = ephemeral;

        if (status) {
                if (!isempty(status)) {
                        iovec[n++] = IOVEC_MAKE_STRING("[");
                        iovec[n++] = IOVEC_MAKE_STRING(status);
                        iovec[n++] = IOVEC_MAKE_STRING("] ");
                } else
                        iovec[n++] = IOVEC_MAKE_STRING(status_indent);
        }

        iovec[n++] = IOVEC_MAKE_STRING(s);
        if (!ephemeral)
                iovec[n++] = IOVEC_MAKE_STRING("\n");

        if (writev(fd, iovec, n) < 0)
                return -errno;

        return 0;
}

int status_printf(const char *status, bool ellipse, bool ephemeral, const char *format, ...) {
        va_list ap;
        int r;

        assert(format);

        va_start(ap, format);
        r = status_vprintf(status, ellipse, ephemeral, format, ap);
        va_end(ap);

        return r;
}
