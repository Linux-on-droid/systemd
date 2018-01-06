/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2011 Kay Sievers <kay@vrfy.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "path-util.h"
#include "string-util.h"
#include "udev.h"
#include "udevadm-util.h"

static void help(struct udev *udev) {
        printf("%s test-builtin [OPTIONS] COMMAND DEVPATH\n\n"
               "Test a built-in command.\n\n"
               "  -h --help     Print this message\n"
               "  -V --version  Print version of the program\n\n"
               "Commands:\n"
               , program_invocation_short_name);

        udev_builtin_list(udev);
}

static int adm_builtin(struct udev *udev, int argc, char *argv[]) {
        static const struct option options[] = {
                { "version", no_argument, NULL, 'V' },
                { "help",    no_argument, NULL, 'h' },
                {}
        };
        char *command = NULL;
        char *syspath = NULL;
        char filename[UTIL_PATH_SIZE];
        struct udev_device *dev = NULL;
        enum udev_builtin_cmd cmd;
        int rc = EXIT_SUCCESS, c;

        while ((c = getopt_long(argc, argv, "Vh", options, NULL)) >= 0)
                switch (c) {
                case 'V':
                        print_version();
                        goto out;
                case 'h':
                        help(udev);
                        goto out;
                }

        command = argv[optind++];
        if (command == NULL) {
                fprintf(stderr, "command missing\n");
                help(udev);
                rc = 2;
                goto out;
        }

        syspath = argv[optind++];
        if (syspath == NULL) {
                fprintf(stderr, "syspath missing\n");
                rc = 3;
                goto out;
        }

        udev_builtin_init(udev);

        cmd = udev_builtin_lookup(command);
        if (cmd >= UDEV_BUILTIN_MAX) {
                fprintf(stderr, "unknown command '%s'\n", command);
                help(udev);
                rc = 5;
                goto out;
        }

        /* add /sys if needed */
        if (!path_startswith(syspath, "/sys"))
                strscpyl(filename, sizeof(filename), "/sys", syspath, NULL);
        else
                strscpy(filename, sizeof(filename), syspath);
        delete_trailing_chars(filename, "/");

        dev = udev_device_new_from_syspath(udev, filename);
        if (dev == NULL) {
                fprintf(stderr, "unable to open device '%s'\n\n", filename);
                rc = 4;
                goto out;
        }

        rc = udev_builtin_run(dev, cmd, command, true);
        if (rc < 0) {
                fprintf(stderr, "error executing '%s', exit code %i\n\n", command, rc);
                rc = 6;
        }
out:
        udev_device_unref(dev);
        udev_builtin_exit(udev);
        return rc;
}

const struct udevadm_cmd udevadm_test_builtin = {
        .name = "test-builtin",
        .cmd = adm_builtin,
        .help = "Test a built-in command",
        .debug = true,
};
