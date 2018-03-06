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
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conf-files.h"
#include "def.h"
#include "fd-util.h"
#include "fileio.h"
#include "hashmap.h"
#include "log.h"
#include "path-util.h"
#include "string-util.h"
#include "strv.h"
#include "sysctl-util.h"
#include "util.h"

static char **arg_prefixes = NULL;

static const char conf_file_dirs[] = CONF_PATHS_NULSTR("sysctl.d");

static int apply_all(OrderedHashmap *sysctl_options) {
        char *property, *value;
        Iterator i;
        int r = 0;

        ORDERED_HASHMAP_FOREACH_KEY(value, property, sysctl_options, i) {
                int k;

                k = sysctl_write(property, value);
                if (k < 0) {
                        /* If the sysctl is not available in the kernel or we are running with reduced privileges and
                         * cannot write it, then log about the issue at LOG_NOTICE level, and proceed without
                         * failing. (EROFS is treated as a permission problem here, since that's how container managers
                         * usually protected their sysctls.) In all other cases log an error and make the tool fail. */

                        if (IN_SET(k, -EPERM, -EACCES, -EROFS, -ENOENT))
                                log_notice_errno(k, "Couldn't write '%s' to '%s', ignoring: %m", value, property);
                        else {
                                log_error_errno(k, "Couldn't write '%s' to '%s': %m", value, property);
                                if (r == 0)
                                        r = k;
                        }
                }
        }

        return r;
}

static bool test_prefix(const char *p) {
        char **i;

        if (strv_isempty(arg_prefixes))
                return true;

        STRV_FOREACH(i, arg_prefixes) {
                const char *t;

                t = path_startswith(*i, "/proc/sys/");
                if (!t)
                        t = *i;
                if (path_startswith(p, t))
                        return true;
        }

        return false;
}

static int parse_file(OrderedHashmap *sysctl_options, const char *path, bool ignore_enoent) {
        _cleanup_fclose_ FILE *f = NULL;
        unsigned c = 0;
        int r;

        assert(path);

        r = search_and_fopen_nulstr(path, "re", NULL, conf_file_dirs, &f);
        if (r < 0) {
                if (ignore_enoent && r == -ENOENT)
                        return 0;

                return log_error_errno(r, "Failed to open file '%s', ignoring: %m", path);
        }

        log_debug("Parsing %s", path);
        for (;;) {
                char *p, *value, *new_value, *property, *existing;
                _cleanup_free_ char *l = NULL;
                void *v;
                int k;

                k = read_line(f, LONG_LINE_MAX, &l);
                if (k == 0)
                        break;
                if (k < 0)
                        return log_error_errno(k, "Failed to read file '%s', ignoring: %m", path);

                c++;

                p = strstrip(l);

                if (isempty(p))
                        continue;
                if (strchr(COMMENTS "\n", *p))
                        continue;

                value = strchr(p, '=');
                if (!value) {
                        log_error("Line is not an assignment at '%s:%u': %s", path, c, value);

                        if (r == 0)
                                r = -EINVAL;
                        continue;
                }

                *value = 0;
                value++;

                p = sysctl_normalize(strstrip(p));
                value = strstrip(value);

                if (!test_prefix(p))
                        continue;

                existing = ordered_hashmap_get2(sysctl_options, p, &v);
                if (existing) {
                        if (streq(value, existing))
                                continue;

                        log_debug("Overwriting earlier assignment of %s at '%s:%u'.", p, path, c);
                        free(ordered_hashmap_remove(sysctl_options, p));
                        free(v);
                }

                property = strdup(p);
                if (!property)
                        return log_oom();

                new_value = strdup(value);
                if (!new_value) {
                        free(property);
                        return log_oom();
                }

                k = ordered_hashmap_put(sysctl_options, property, new_value);
                if (k < 0) {
                        log_error_errno(k, "Failed to add sysctl variable %s to hashmap: %m", property);
                        free(property);
                        free(new_value);
                        return k;
                }
        }

        return r;
}

static void help(void) {
        printf("%s [OPTIONS...] [CONFIGURATION FILE...]\n\n"
               "Applies kernel sysctl settings.\n\n"
               "  -h --help             Show this help\n"
               "     --version          Show package version\n"
               "     --prefix=PATH      Only apply rules with the specified prefix\n"
               , program_invocation_short_name);
}

static int parse_argv(int argc, char *argv[]) {

        enum {
                ARG_VERSION = 0x100,
                ARG_PREFIX
        };

        static const struct option options[] = {
                { "help",      no_argument,       NULL, 'h'           },
                { "version",   no_argument,       NULL, ARG_VERSION   },
                { "prefix",    required_argument, NULL, ARG_PREFIX    },
                {}
        };

        int c;

        assert(argc >= 0);
        assert(argv);

        while ((c = getopt_long(argc, argv, "h", options, NULL)) >= 0)

                switch (c) {

                case 'h':
                        help();
                        return 0;

                case ARG_VERSION:
                        return version();

                case ARG_PREFIX: {
                        char *p;

                        /* We used to require people to specify absolute paths
                         * in /proc/sys in the past. This is kinda useless, but
                         * we need to keep compatibility. We now support any
                         * sysctl name available. */
                        sysctl_normalize(optarg);

                        if (path_startswith(optarg, "/proc/sys"))
                                p = strdup(optarg);
                        else
                                p = strappend("/proc/sys/", optarg);
                        if (!p)
                                return log_oom();

                        if (strv_consume(&arg_prefixes, p) < 0)
                                return log_oom();

                        break;
                }

                case '?':
                        return -EINVAL;

                default:
                        assert_not_reached("Unhandled option");
                }

        return 1;
}

int main(int argc, char *argv[]) {
        OrderedHashmap *sysctl_options = NULL;
        int r = 0, k;

        r = parse_argv(argc, argv);
        if (r <= 0)
                goto finish;

        log_set_target(LOG_TARGET_AUTO);
        log_parse_environment();
        log_open();

        umask(0022);

        sysctl_options = ordered_hashmap_new(&path_hash_ops);
        if (!sysctl_options) {
                r = log_oom();
                goto finish;
        }

        r = 0;

        if (argc > optind) {
                int i;

                for (i = optind; i < argc; i++) {
                        k = parse_file(sysctl_options, argv[i], false);
                        if (k < 0 && r == 0)
                                r = k;
                }
        } else {
                _cleanup_strv_free_ char **files = NULL;
                char **f;

                r = conf_files_list_nulstr(&files, ".conf", NULL, 0, conf_file_dirs);
                if (r < 0) {
                        log_error_errno(r, "Failed to enumerate sysctl.d files: %m");
                        goto finish;
                }

                STRV_FOREACH(f, files) {
                        k = parse_file(sysctl_options, *f, true);
                        if (k < 0 && r == 0)
                                r = k;
                }
        }

        k = apply_all(sysctl_options);
        if (k < 0 && r == 0)
                r = k;

finish:
        ordered_hashmap_free_free_free(sysctl_options);
        strv_free(arg_prefixes);

        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
