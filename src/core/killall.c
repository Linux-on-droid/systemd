/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2010 ProFUSION embedded systems

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
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "alloc-util.h"
#include "def.h"
#include "dirent-util.h"
#include "fd-util.h"
#include "format-util.h"
#include "killall.h"
#include "parse-util.h"
#include "process-util.h"
#include "set.h"
#include "string-util.h"
#include "terminal-util.h"
#include "util.h"

static bool ignore_proc(pid_t pid, bool warn_rootfs) {
        _cleanup_fclose_ FILE *f = NULL;
        char c;
        const char *p;
        size_t count;
        uid_t uid;
        int r;

        /* We are PID 1, let's not commit suicide */
        if (pid == 1)
                return true;

        r = get_process_uid(pid, &uid);
        if (r < 0)
                return true; /* not really, but better safe than sorry */

        /* Non-root processes otherwise are always subject to be killed */
        if (uid != 0)
                return false;

        p = procfs_file_alloca(pid, "cmdline");
        f = fopen(p, "re");
        if (!f)
                return true; /* not really, but has the desired effect */

        count = fread(&c, 1, 1, f);

        /* Kernel threads have an empty cmdline */
        if (count <= 0)
                return true;

        /* Processes with argv[0][0] = '@' we ignore from the killing spree.
         *
         * http://www.freedesktop.org/wiki/Software/systemd/RootStorageDaemons */
        if (c != '@')
                return false;

        if (warn_rootfs &&
            pid_from_same_root_fs(pid) == 0) {

                _cleanup_free_ char *comm = NULL;

                get_process_comm(pid, &comm);

                log_notice("Process " PID_FMT " (%s) has been marked to be excluded from killing. It is "
                           "running from the root file system, and thus likely to block re-mounting of the "
                           "root file system to read-only. Please consider moving it into an initrd file "
                           "system instead.", pid, strna(comm));
        }

        return true;
}

static void wait_for_children(Set *pids, sigset_t *mask, usec_t timeout) {
        usec_t until;

        assert(mask);

        if (set_isempty(pids))
                return;

        until = now(CLOCK_MONOTONIC) + timeout;
        for (;;) {
                struct timespec ts;
                int k;
                usec_t n;
                void *p;
                Iterator i;

                /* First, let the kernel inform us about killed
                 * children. Most processes will probably be our
                 * children, but some are not (might be our
                 * grandchildren instead...). */
                for (;;) {
                        pid_t pid;

                        pid = waitpid(-1, NULL, WNOHANG);
                        if (pid == 0)
                                break;
                        if (pid < 0) {
                                if (errno == ECHILD)
                                        break;

                                log_error_errno(errno, "waitpid() failed: %m");
                                return;
                        }

                        (void) set_remove(pids, PID_TO_PTR(pid));
                }

                /* Now explicitly check who might be remaining, who
                 * might not be our child. */
                SET_FOREACH(p, pids, i) {

                        /* kill(pid, 0) sends no signal, but it tells
                         * us whether the process still exists. */
                        if (kill(PTR_TO_PID(p), 0) == 0)
                                continue;

                        if (errno != ESRCH)
                                continue;

                        set_remove(pids, p);
                }

                if (set_isempty(pids))
                        return;

                n = now(CLOCK_MONOTONIC);
                if (n >= until)
                        return;

                timespec_store(&ts, until - n);
                k = sigtimedwait(mask, NULL, &ts);
                if (k != SIGCHLD) {

                        if (k < 0 && errno != EAGAIN) {
                                log_error_errno(errno, "sigtimedwait() failed: %m");
                                return;
                        }

                        if (k >= 0)
                                log_warning("sigtimedwait() returned unexpected signal.");
                }
        }
}

static int killall(int sig, Set *pids, bool send_sighup) {
        _cleanup_closedir_ DIR *dir = NULL;
        struct dirent *d;

        dir = opendir("/proc");
        if (!dir)
                return -errno;

        FOREACH_DIRENT_ALL(d, dir, break) {
                pid_t pid;
                int r;

                if (!IN_SET(d->d_type, DT_DIR, DT_UNKNOWN))
                        continue;

                if (parse_pid(d->d_name, &pid) < 0)
                        continue;

                if (ignore_proc(pid, sig == SIGKILL && !in_initrd()))
                        continue;

                if (sig == SIGKILL) {
                        _cleanup_free_ char *s = NULL;

                        get_process_comm(pid, &s);
                        log_notice("Sending SIGKILL to PID "PID_FMT" (%s).", pid, strna(s));
                }

                if (kill(pid, sig) >= 0) {
                        if (pids) {
                                r = set_put(pids, PID_TO_PTR(pid));
                                if (r < 0)
                                        log_oom();
                        }
                } else if (errno != ENOENT)
                        log_warning_errno(errno, "Could not kill %d: %m", pid);

                if (send_sighup) {
                        /* Optionally, also send a SIGHUP signal, but
                        only if the process has a controlling
                        tty. This is useful to allow handling of
                        shells which ignore SIGTERM but react to
                        SIGHUP. We do not send this to processes that
                        have no controlling TTY since we don't want to
                        trigger reloads of daemon processes. Also we
                        make sure to only send this after SIGTERM so
                        that SIGTERM is always first in the queue. */


                        if (get_ctty_devnr(pid, NULL) >= 0)
                                /* it's OK if the process is gone, just ignore the result */
                                (void) kill(pid, SIGHUP);
                }
        }

        return set_size(pids);
}

void broadcast_signal(int sig, bool wait_for_exit, bool send_sighup, usec_t timeout) {
        sigset_t mask, oldmask;
        _cleanup_set_free_ Set *pids = NULL;

        if (wait_for_exit)
                pids = set_new(NULL);

        assert_se(sigemptyset(&mask) == 0);
        assert_se(sigaddset(&mask, SIGCHLD) == 0);
        assert_se(sigprocmask(SIG_BLOCK, &mask, &oldmask) == 0);

        if (kill(-1, SIGSTOP) < 0 && errno != ESRCH)
                log_warning_errno(errno, "kill(-1, SIGSTOP) failed: %m");

        killall(sig, pids, send_sighup);

        if (kill(-1, SIGCONT) < 0 && errno != ESRCH)
                log_warning_errno(errno, "kill(-1, SIGCONT) failed: %m");

        if (wait_for_exit)
                wait_for_children(pids, &mask, timeout);

        assert_se(sigprocmask(SIG_SETMASK, &oldmask, NULL) == 0);
}
