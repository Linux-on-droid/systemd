/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2010 Lennart Poettering
  Copyright 2013 Thomas H.P. Andersen
  Copyright 2015 Zbigniew Jędrzejewski-Szmek

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
#include "fileio.h"
#include "hostname-util.h"
#include "string-util.h"
#include "util.h"

static void test_hostname_is_valid(void) {
        assert_se(hostname_is_valid("foobar", false));
        assert_se(hostname_is_valid("foobar.com", false));
        assert_se(!hostname_is_valid("foobar.com.", false));
        assert_se(hostname_is_valid("fooBAR", false));
        assert_se(hostname_is_valid("fooBAR.com", false));
        assert_se(!hostname_is_valid("fooBAR.", false));
        assert_se(!hostname_is_valid("fooBAR.com.", false));
        assert_se(!hostname_is_valid("fööbar", false));
        assert_se(!hostname_is_valid("", false));
        assert_se(!hostname_is_valid(".", false));
        assert_se(!hostname_is_valid("..", false));
        assert_se(!hostname_is_valid("foobar.", false));
        assert_se(!hostname_is_valid(".foobar", false));
        assert_se(!hostname_is_valid("foo..bar", false));
        assert_se(!hostname_is_valid("foo.bar..", false));
        assert_se(!hostname_is_valid("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", false));
        assert_se(!hostname_is_valid("au-xph5-rvgrdsb5hcxc-47et3a5vvkrc-server-wyoz4elpdpe3.openstack.local", false));

        assert_se(hostname_is_valid("foobar", true));
        assert_se(hostname_is_valid("foobar.com", true));
        assert_se(hostname_is_valid("foobar.com.", true));
        assert_se(hostname_is_valid("fooBAR", true));
        assert_se(hostname_is_valid("fooBAR.com", true));
        assert_se(!hostname_is_valid("fooBAR.", true));
        assert_se(hostname_is_valid("fooBAR.com.", true));
        assert_se(!hostname_is_valid("fööbar", true));
        assert_se(!hostname_is_valid("", true));
        assert_se(!hostname_is_valid(".", true));
        assert_se(!hostname_is_valid("..", true));
        assert_se(!hostname_is_valid("foobar.", true));
        assert_se(!hostname_is_valid(".foobar", true));
        assert_se(!hostname_is_valid("foo..bar", true));
        assert_se(!hostname_is_valid("foo.bar..", true));
        assert_se(!hostname_is_valid("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", true));
}

static void test_hostname_cleanup(void) {
        char *s;

        s = strdupa("foobar");
        assert_se(streq(hostname_cleanup(s), "foobar"));
        s = strdupa("foobar.com");
        assert_se(streq(hostname_cleanup(s), "foobar.com"));
        s = strdupa("foobar.com.");
        assert_se(streq(hostname_cleanup(s), "foobar.com"));
        s = strdupa("fooBAR");
        assert_se(streq(hostname_cleanup(s), "fooBAR"));
        s = strdupa("fooBAR.com");
        assert_se(streq(hostname_cleanup(s), "fooBAR.com"));
        s = strdupa("fooBAR.");
        assert_se(streq(hostname_cleanup(s), "fooBAR"));
        s = strdupa("fooBAR.com.");
        assert_se(streq(hostname_cleanup(s), "fooBAR.com"));
        s = strdupa("fööbar");
        assert_se(streq(hostname_cleanup(s), "fbar"));
        s = strdupa("");
        assert_se(isempty(hostname_cleanup(s)));
        s = strdupa(".");
        assert_se(isempty(hostname_cleanup(s)));
        s = strdupa("..");
        assert_se(isempty(hostname_cleanup(s)));
        s = strdupa("foobar.");
        assert_se(streq(hostname_cleanup(s), "foobar"));
        s = strdupa(".foobar");
        assert_se(streq(hostname_cleanup(s), "foobar"));
        s = strdupa("foo..bar");
        assert_se(streq(hostname_cleanup(s), "foo.bar"));
        s = strdupa("foo.bar..");
        assert_se(streq(hostname_cleanup(s), "foo.bar"));
        s = strdupa("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
        assert_se(streq(hostname_cleanup(s), "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"));
}

static void test_read_etc_hostname(void) {
        char path[] = "/tmp/hostname.XXXXXX";
        char *hostname;
        int fd;

        fd = mkostemp_safe(path);
        assert(fd > 0);
        close(fd);

        /* simple hostname */
        assert_se(write_string_file(path, "foo", WRITE_STRING_FILE_CREATE) == 0);
        assert_se(read_etc_hostname(path, &hostname) == 0);
        assert_se(streq(hostname, "foo"));
        hostname = mfree(hostname);

        /* with comment */
        assert_se(write_string_file(path, "# comment\nfoo", WRITE_STRING_FILE_CREATE) == 0);
        assert_se(read_etc_hostname(path, &hostname) == 0);
        assert_se(hostname);
        assert_se(streq(hostname, "foo"));
        hostname = mfree(hostname);

        /* with comment and extra whitespace */
        assert_se(write_string_file(path, "# comment\n\n foo ", WRITE_STRING_FILE_CREATE) == 0);
        assert_se(read_etc_hostname(path, &hostname) == 0);
        assert_se(hostname);
        assert_se(streq(hostname, "foo"));
        hostname = mfree(hostname);

        /* cleans up name */
        assert_se(write_string_file(path, "!foo/bar.com", WRITE_STRING_FILE_CREATE) == 0);
        assert_se(read_etc_hostname(path, &hostname) == 0);
        assert_se(hostname);
        assert_se(streq(hostname, "foobar.com"));
        hostname = mfree(hostname);

        /* no value set */
        hostname = (char*) 0x1234;
        assert_se(write_string_file(path, "# nothing here\n", WRITE_STRING_FILE_CREATE) == 0);
        assert_se(read_etc_hostname(path, &hostname) == -ENOENT);
        assert_se(hostname == (char*) 0x1234);  /* does not touch argument on error */

        /* nonexisting file */
        assert_se(read_etc_hostname("/non/existing", &hostname) == -ENOENT);
        assert_se(hostname == (char*) 0x1234);  /* does not touch argument on error */

        unlink(path);
}

int main(int argc, char *argv[]) {
        log_parse_environment();
        log_open();

        test_hostname_is_valid();
        test_hostname_cleanup();
        test_read_etc_hostname();

        return 0;
}
