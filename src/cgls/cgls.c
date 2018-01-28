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
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "sd-bus.h"

#include "alloc-util.h"
#include "bus-util.h"
#include "cgroup-show.h"
#include "cgroup-util.h"
#include "fileio.h"
#include "log.h"
#include "output-mode.h"
#include "pager.h"
#include "path-util.h"
#include "strv.h"
#include "unit-name.h"
#include "util.h"

static bool arg_no_pager = false;
static bool arg_kernel_threads = false;
static bool arg_all = false;

static enum {
        SHOW_UNIT_NONE,
        SHOW_UNIT_SYSTEM,
        SHOW_UNIT_USER,
} arg_show_unit = SHOW_UNIT_NONE;
static char **arg_names = NULL;

static int arg_full = -1;
static char* arg_machine = NULL;

static void help(void) {
        printf("%s [OPTIONS...] [CGROUP...]\n\n"
               "Recursively show control group contents.\n\n"
               "  -h --help           Show this help\n"
               "     --version        Show package version\n"
               "     --no-pager       Do not pipe output into a pager\n"
               "  -a --all            Show all groups, including empty\n"
               "  -u --unit           Show the subtrees of specifified system units\n"
               "     --user-unit      Show the subtrees of specifified user units\n"
               "  -l --full           Do not ellipsize output\n"
               "  -k                  Include kernel threads in output\n"
               "  -M --machine=       Show container\n"
               , program_invocation_short_name);
}

static int parse_argv(int argc, char *argv[]) {

        enum {
                ARG_NO_PAGER = 0x100,
                ARG_VERSION,
                ARG_USER_UNIT,
        };

        static const struct option options[] = {
                { "help",      no_argument,       NULL, 'h'           },
                { "version",   no_argument,       NULL, ARG_VERSION   },
                { "no-pager",  no_argument,       NULL, ARG_NO_PAGER  },
                { "all",       no_argument,       NULL, 'a'           },
                { "full",      no_argument,       NULL, 'l'           },
                { "machine",   required_argument, NULL, 'M'           },
                { "unit",      optional_argument, NULL, 'u'           },
                { "user-unit", optional_argument, NULL, ARG_USER_UNIT },
                {}
        };

        int c;

        assert(argc >= 1);
        assert(argv);

        while ((c = getopt_long(argc, argv, "-hkalM:u::", options, NULL)) >= 0)

                switch (c) {

                case 'h':
                        help();
                        return 0;

                case ARG_VERSION:
                        return version();

                case ARG_NO_PAGER:
                        arg_no_pager = true;
                        break;

                case 'a':
                        arg_all = true;
                        break;

                case 'u':
                        arg_show_unit = SHOW_UNIT_SYSTEM;
                        if (strv_push(&arg_names, optarg) < 0) /* push optarg if not empty */
                                return log_oom();
                        break;

                case ARG_USER_UNIT:
                        arg_show_unit = SHOW_UNIT_USER;
                        if (strv_push(&arg_names, optarg) < 0) /* push optarg if not empty */
                                return log_oom();
                        break;

                case 1:
                        /* positional argument */
                        if (strv_push(&arg_names, optarg) < 0)
                                return log_oom();
                        break;

                case 'l':
                        arg_full = true;
                        break;

                case 'k':
                        arg_kernel_threads = true;
                        break;

                case 'M':
                        arg_machine = optarg;
                        break;

                case '?':
                        return -EINVAL;

                default:
                        assert_not_reached("Unhandled option");
                }

        if (arg_machine && arg_show_unit != SHOW_UNIT_NONE) {
                log_error("Cannot combine --unit or --user-unit with --machine=.");
                return -EINVAL;
        }

        return 1;
}

static void show_cg_info(const char *controller, const char *path) {

        if (cg_all_unified() == 0 && controller && !streq(controller, SYSTEMD_CGROUP_CONTROLLER))
                printf("Controller %s; ", controller);

        printf("Control group %s:\n", isempty(path) ? "/" : path);
        fflush(stdout);
}

int main(int argc, char *argv[]) {
        int r, output_flags;

        log_parse_environment();
        log_open();

        r = parse_argv(argc, argv);
        if (r <= 0)
                goto finish;

        if (!arg_no_pager) {
                r = pager_open(arg_no_pager, false);
                if (r > 0 && arg_full < 0)
                        arg_full = true;
        }

        output_flags =
                arg_all * OUTPUT_SHOW_ALL |
                (arg_full > 0) * OUTPUT_FULL_WIDTH |
                arg_kernel_threads * OUTPUT_KERNEL_THREADS;

        if (arg_names) {
                _cleanup_(sd_bus_flush_close_unrefp) sd_bus *bus = NULL;
                _cleanup_free_ char *root = NULL;
                char **name;

                STRV_FOREACH(name, arg_names) {
                        int q;

                        if (arg_show_unit != SHOW_UNIT_NONE) {
                                /* Command line arguments are unit names */
                                _cleanup_free_ char *cgroup = NULL;

                                if (!bus) {
                                        /* Connect to the bus only if necessary */
                                        r = bus_connect_transport_systemd(BUS_TRANSPORT_LOCAL, NULL,
                                                                          arg_show_unit == SHOW_UNIT_USER,
                                                                          &bus);
                                        if (r < 0) {
                                                log_error_errno(r, "Failed to create bus connection: %m");
                                                goto finish;
                                        }
                                }

                                q = show_cgroup_get_unit_path_and_warn(bus, *name, &cgroup);
                                if (q < 0)
                                        goto failed;

                                if (isempty(cgroup)) {
                                        log_warning("Unit %s not found.", *name);
                                        q = -ENOENT;
                                        goto failed;
                                }

                                printf("Unit %s (%s):\n", *name, cgroup);
                                fflush(stdout);

                                q = show_cgroup_by_path(cgroup, NULL, 0, output_flags);

                        } else if (path_startswith(*name, "/sys/fs/cgroup")) {

                                printf("Directory %s:\n", *name);
                                fflush(stdout);

                                q = show_cgroup_by_path(*name, NULL, 0, output_flags);
                        } else {
                                _cleanup_free_ char *c = NULL, *p = NULL, *j = NULL;
                                const char *controller, *path;

                                if (!root) {
                                        /* Query root only if needed, treat error as fatal */
                                        r = show_cgroup_get_path_and_warn(arg_machine, NULL, &root);
                                        if (r < 0)
                                                goto finish;
                                }

                                q = cg_split_spec(*name, &c, &p);
                                if (q < 0) {
                                        log_error_errno(q, "Failed to split argument %s: %m", *name);
                                        goto failed;
                                }

                                controller = c ?: SYSTEMD_CGROUP_CONTROLLER;
                                if (p) {
                                        j = strjoin(root, "/", p);
                                        if (!j) {
                                                r = log_oom();
                                                goto finish;
                                        }

                                        path_kill_slashes(j);
                                        path = j;
                                } else
                                        path = root;

                                show_cg_info(controller, path);

                                q = show_cgroup(controller, path, NULL, 0, output_flags);
                        }

                failed:
                        if (q < 0 && r >= 0)
                                r = q;
                }

        } else {
                bool done = false;

                if (!arg_machine)  {
                        _cleanup_free_ char *cwd = NULL;

                        r = safe_getcwd(&cwd);
                        if (r < 0) {
                                log_error_errno(r, "Cannot determine current working directory: %m");
                                goto finish;
                        }

                        if (path_startswith(cwd, "/sys/fs/cgroup")) {
                                printf("Working directory %s:\n", cwd);
                                fflush(stdout);

                                r = show_cgroup_by_path(cwd, NULL, 0, output_flags);
                                done = true;
                        }
                }

                if (!done) {
                        _cleanup_free_ char *root = NULL;

                        r = show_cgroup_get_path_and_warn(arg_machine, NULL, &root);
                        if (r < 0)
                                goto finish;

                        show_cg_info(SYSTEMD_CGROUP_CONTROLLER, root);

                        printf("-.slice\n");
                        r = show_cgroup(SYSTEMD_CGROUP_CONTROLLER, root, NULL, 0, output_flags);
                }
        }

        if (r < 0)
                log_error_errno(r, "Failed to list cgroup tree: %m");

finish:
        pager_close();
        free(arg_names); /* don't free the strings */

        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
