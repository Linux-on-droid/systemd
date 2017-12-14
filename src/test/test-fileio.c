/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2013 Lennart Poettering

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
#include <stdio.h>
#include <unistd.h>

#include "alloc-util.h"
#include "ctype.h"
#include "def.h"
#include "env-util.h"
#include "fd-util.h"
#include "fileio.h"
#include "io-util.h"
#include "parse-util.h"
#include "process-util.h"
#include "string-util.h"
#include "strv.h"
#include "util.h"

static void test_parse_env_file(void) {
        char    t[] = "/tmp/test-fileio-in-XXXXXX",
                p[] = "/tmp/test-fileio-out-XXXXXX";
        int fd, r;
        FILE *f;
        _cleanup_free_ char *one = NULL, *two = NULL, *three = NULL, *four = NULL, *five = NULL,
                        *six = NULL, *seven = NULL, *eight = NULL, *nine = NULL, *ten = NULL;
        _cleanup_strv_free_ char **a = NULL, **b = NULL;
        char **i;
        unsigned k;

        fd = mkostemp_safe(p);
        assert_se(fd >= 0);
        close(fd);

        fd = mkostemp_safe(t);
        assert_se(fd >= 0);

        f = fdopen(fd, "w");
        assert_se(f);

        fputs("one=BAR   \n"
              "# comment\n"
              " # comment \n"
              " ; comment \n"
              "  two   =   bar    \n"
              "invalid line\n"
              "invalid line #comment\n"
              "three = \"333\n"
              "xxxx\"\n"
              "four = \'44\\\"44\'\n"
              "five = \'55\\\'55\' \"FIVE\" cinco   \n"
              "six = seis sechs\\\n"
              " sis\n"
              "seven=\"sevenval\" #nocomment\n"
              "eight=eightval #nocomment\n"
              "export nine=nineval\n"
              "ten=ignored\n"
              "ten=ignored\n"
              "ten=", f);

        fflush(f);
        fclose(f);

        r = load_env_file(NULL, t, NULL, &a);
        assert_se(r >= 0);

        STRV_FOREACH(i, a)
                log_info("Got: <%s>", *i);

        assert_se(streq_ptr(a[0], "one=BAR"));
        assert_se(streq_ptr(a[1], "two=bar"));
        assert_se(streq_ptr(a[2], "three=333\nxxxx"));
        assert_se(streq_ptr(a[3], "four=44\"44"));
        assert_se(streq_ptr(a[4], "five=55\'55FIVEcinco"));
        assert_se(streq_ptr(a[5], "six=seis sechs sis"));
        assert_se(streq_ptr(a[6], "seven=sevenval#nocomment"));
        assert_se(streq_ptr(a[7], "eight=eightval #nocomment"));
        assert_se(streq_ptr(a[8], "export nine=nineval"));
        assert_se(streq_ptr(a[9], "ten="));
        assert_se(a[10] == NULL);

        strv_env_clean(a);

        k = 0;
        STRV_FOREACH(i, b) {
                log_info("Got2: <%s>", *i);
                assert_se(streq(*i, a[k++]));
        }

        r = parse_env_file(
                        t, NULL,
                       "one", &one,
                       "two", &two,
                       "three", &three,
                       "four", &four,
                       "five", &five,
                       "six", &six,
                       "seven", &seven,
                       "eight", &eight,
                       "export nine", &nine,
                       "ten", &ten,
                       NULL);

        assert_se(r >= 0);

        log_info("one=[%s]", strna(one));
        log_info("two=[%s]", strna(two));
        log_info("three=[%s]", strna(three));
        log_info("four=[%s]", strna(four));
        log_info("five=[%s]", strna(five));
        log_info("six=[%s]", strna(six));
        log_info("seven=[%s]", strna(seven));
        log_info("eight=[%s]", strna(eight));
        log_info("export nine=[%s]", strna(nine));
        log_info("ten=[%s]", strna(nine));

        assert_se(streq(one, "BAR"));
        assert_se(streq(two, "bar"));
        assert_se(streq(three, "333\nxxxx"));
        assert_se(streq(four, "44\"44"));
        assert_se(streq(five, "55\'55FIVEcinco"));
        assert_se(streq(six, "seis sechs sis"));
        assert_se(streq(seven, "sevenval#nocomment"));
        assert_se(streq(eight, "eightval #nocomment"));
        assert_se(streq(nine, "nineval"));
        assert_se(ten == NULL);

        r = write_env_file(p, a);
        assert_se(r >= 0);

        r = load_env_file(NULL, p, NULL, &b);
        assert_se(r >= 0);

        unlink(t);
        unlink(p);
}

static void test_parse_multiline_env_file(void) {
        char    t[] = "/tmp/test-fileio-in-XXXXXX",
                p[] = "/tmp/test-fileio-out-XXXXXX";
        int fd, r;
        FILE *f;
        _cleanup_strv_free_ char **a = NULL, **b = NULL;
        char **i;

        fd = mkostemp_safe(p);
        assert_se(fd >= 0);
        close(fd);

        fd = mkostemp_safe(t);
        assert_se(fd >= 0);

        f = fdopen(fd, "w");
        assert_se(f);

        fputs("one=BAR\\\n"
              "    VAR\\\n"
              "\tGAR\n"
              "#comment\n"
              "two=\"bar\\\n"
              "    var\\\n"
              "\tgar\"\n"
              "#comment\n"
              "tri=\"bar \\\n"
              "    var \\\n"
              "\tgar \"\n", f);

        fflush(f);
        fclose(f);

        r = load_env_file(NULL, t, NULL, &a);
        assert_se(r >= 0);

        STRV_FOREACH(i, a)
                log_info("Got: <%s>", *i);

        assert_se(streq_ptr(a[0], "one=BAR    VAR\tGAR"));
        assert_se(streq_ptr(a[1], "two=bar    var\tgar"));
        assert_se(streq_ptr(a[2], "tri=bar     var \tgar "));
        assert_se(a[3] == NULL);

        r = write_env_file(p, a);
        assert_se(r >= 0);

        r = load_env_file(NULL, p, NULL, &b);
        assert_se(r >= 0);

        unlink(t);
        unlink(p);
}

static void test_merge_env_file(void) {
        char t[] = "/tmp/test-fileio-XXXXXX";
        int fd, r;
        _cleanup_fclose_ FILE *f = NULL;
        _cleanup_strv_free_ char **a = NULL;
        char **i;

        fd = mkostemp_safe(t);
        assert_se(fd >= 0);

        log_info("/* %s (%s) */", __func__, t);

        f = fdopen(fd, "w");
        assert_se(f);

        r = write_string_stream(f,
                                "one=1   \n"
                                "twelve=${one}2\n"
                                "twentyone=2${one}\n"
                                "one=2\n"
                                "twentytwo=2${one}\n"
                                "xxx_minus_three=$xxx - 3\n"
                                "xxx=0x$one$one$one\n"
                                "yyy=${one:-fallback}\n"
                                "zzz=${one:+replacement}\n"
                                "zzzz=${foobar:-${nothing}}\n"
                                "zzzzz=${nothing:+${nothing}}\n"
                                , WRITE_STRING_FILE_AVOID_NEWLINE);
        assert(r >= 0);

        r = merge_env_file(&a, NULL, t);
        assert_se(r >= 0);
        strv_sort(a);

        STRV_FOREACH(i, a)
                log_info("Got: <%s>", *i);

        assert_se(streq(a[0], "one=2"));
        assert_se(streq(a[1], "twelve=12"));
        assert_se(streq(a[2], "twentyone=21"));
        assert_se(streq(a[3], "twentytwo=22"));
        assert_se(streq(a[4], "xxx=0x222"));
        assert_se(streq(a[5], "xxx_minus_three= - 3"));
        assert_se(streq(a[6], "yyy=2"));
        assert_se(streq(a[7], "zzz=replacement"));
        assert_se(streq(a[8], "zzzz="));
        assert_se(streq(a[9], "zzzzz="));
        assert_se(a[10] == NULL);

        r = merge_env_file(&a, NULL, t);
        assert_se(r >= 0);
        strv_sort(a);

        STRV_FOREACH(i, a)
                log_info("Got2: <%s>", *i);

        assert_se(streq(a[0], "one=2"));
        assert_se(streq(a[1], "twelve=12"));
        assert_se(streq(a[2], "twentyone=21"));
        assert_se(streq(a[3], "twentytwo=22"));
        assert_se(streq(a[4], "xxx=0x222"));
        assert_se(streq(a[5], "xxx_minus_three=0x222 - 3"));
        assert_se(streq(a[6], "yyy=2"));
        assert_se(streq(a[7], "zzz=replacement"));
        assert_se(streq(a[8], "zzzz="));
        assert_se(streq(a[9], "zzzzz="));
        assert_se(a[10] == NULL);
}

static void test_merge_env_file_invalid(void) {
        char t[] = "/tmp/test-fileio-XXXXXX";
        int fd, r;
        _cleanup_fclose_ FILE *f = NULL;
        _cleanup_strv_free_ char **a = NULL;
        char **i;

        fd = mkostemp_safe(t);
        assert_se(fd >= 0);

        log_info("/* %s (%s) */", __func__, t);

        f = fdopen(fd, "w");
        assert_se(f);

        r = write_string_stream(f,
                                "unset one   \n"
                                "unset one=   \n"
                                "unset one=1   \n"
                                "one   \n"
                                "one =  \n"
                                "one two =\n"
                                "\x20two=\n"
                                "#comment=comment\n"
                                ";comment2=comment2\n"
                                "#\n"
                                "\n\n"                  /* empty line */
                                , WRITE_STRING_FILE_AVOID_NEWLINE);
        assert(r >= 0);

        r = merge_env_file(&a, NULL, t);
        assert_se(r >= 0);

        STRV_FOREACH(i, a)
                log_info("Got: <%s>", *i);

        assert_se(strv_isempty(a));
}

static void test_executable_is_script(void) {
        char t[] = "/tmp/test-executable-XXXXXX";
        int fd, r;
        FILE *f;
        char *command;

        fd = mkostemp_safe(t);
        assert_se(fd >= 0);

        f = fdopen(fd, "w");
        assert_se(f);

        fputs("#! /bin/script -a -b \ngoo goo", f);
        fflush(f);

        r = executable_is_script(t, &command);
        assert_se(r > 0);
        assert_se(streq(command, "/bin/script"));
        free(command);

        r = executable_is_script("/bin/sh", &command);
        assert_se(r == 0);

        r = executable_is_script("/usr/bin/yum", &command);
        assert_se(r > 0 || r == -ENOENT);
        if (r > 0) {
                assert_se(startswith(command, "/"));
                free(command);
        }

        fclose(f);
        unlink(t);
}

static void test_status_field(void) {
        _cleanup_free_ char *t = NULL, *p = NULL, *s = NULL, *z = NULL;
        unsigned long long total = 0, buffers = 0;
        int r;

        assert_se(get_proc_field("/proc/self/status", "Threads", WHITESPACE, &t) == 0);
        puts(t);
        assert_se(streq(t, "1"));

        r = get_proc_field("/proc/meminfo", "MemTotal", WHITESPACE, &p);
        if (r != -ENOENT) {
                assert_se(r == 0);
                puts(p);
                assert_se(safe_atollu(p, &total) == 0);
        }

        r = get_proc_field("/proc/meminfo", "Buffers", WHITESPACE, &s);
        if (r != -ENOENT) {
                assert_se(r == 0);
                puts(s);
                assert_se(safe_atollu(s, &buffers) == 0);
        }

        if (p)
                assert_se(buffers < total);

        /* Seccomp should be a good test for field full of zeros. */
        r = get_proc_field("/proc/meminfo", "Seccomp", WHITESPACE, &z);
        if (r != -ENOENT) {
                assert_se(r == 0);
                puts(z);
                assert_se(safe_atollu(z, &buffers) == 0);
        }
}

static void test_capeff(void) {
        int pid, p;

        for (pid = 0; pid < 2; pid++) {
                _cleanup_free_ char *capeff = NULL;
                int r;

                r = get_process_capeff(0, &capeff);
                log_info("capeff: '%s' (r=%d)", capeff, r);

                if (IN_SET(r, -ENOENT, -EPERM))
                        return;

                assert_se(r == 0);
                assert_se(*capeff);
                p = capeff[strspn(capeff, HEXDIGITS)];
                assert_se(!p || isspace(p));
        }
}

static void test_write_string_stream(void) {
        char fn[] = "/tmp/test-write_string_stream-XXXXXX";
        _cleanup_fclose_ FILE *f = NULL;
        int fd;
        char buf[64];

        fd = mkostemp_safe(fn);
        assert_se(fd >= 0);

        f = fdopen(fd, "r");
        assert_se(f);
        assert_se(write_string_stream(f, "boohoo", 0) < 0);

        f = freopen(fn, "r+", f);
        assert_se(f);

        assert_se(write_string_stream(f, "boohoo", 0) == 0);
        rewind(f);

        assert_se(fgets(buf, sizeof(buf), f));
        assert_se(streq(buf, "boohoo\n"));

        f = freopen(fn, "w+", f);
        assert_se(f);

        assert_se(write_string_stream(f, "boohoo", WRITE_STRING_FILE_AVOID_NEWLINE) == 0);
        rewind(f);

        assert_se(fgets(buf, sizeof(buf), f));
        printf(">%s<", buf);
        assert_se(streq(buf, "boohoo"));

        unlink(fn);
}

static void test_write_string_file(void) {
        char fn[] = "/tmp/test-write_string_file-XXXXXX";
        char buf[64] = {};
        _cleanup_close_ int fd;

        fd = mkostemp_safe(fn);
        assert_se(fd >= 0);

        assert_se(write_string_file(fn, "boohoo", WRITE_STRING_FILE_CREATE) == 0);

        assert_se(read(fd, buf, sizeof(buf)) == 7);
        assert_se(streq(buf, "boohoo\n"));

        unlink(fn);
}

static void test_write_string_file_no_create(void) {
        char fn[] = "/tmp/test-write_string_file_no_create-XXXXXX";
        _cleanup_close_ int fd;
        char buf[64] = {0};

        fd = mkostemp_safe(fn);
        assert_se(fd >= 0);

        assert_se(write_string_file("/a/file/which/does/not/exists/i/guess", "boohoo", 0) < 0);
        assert_se(write_string_file(fn, "boohoo", 0) == 0);

        assert_se(read(fd, buf, sizeof(buf)) == STRLEN("boohoo\n"));
        assert_se(streq(buf, "boohoo\n"));

        unlink(fn);
}

static void test_write_string_file_verify(void) {
        _cleanup_free_ char *buf = NULL, *buf2 = NULL;
        int r;

        assert_se(read_one_line_file("/proc/cmdline", &buf) >= 0);
        assert_se((buf2 = strjoin(buf, "\n")));

        r = write_string_file("/proc/cmdline", buf, 0);
        assert_se(IN_SET(r, -EACCES, -EIO));
        r = write_string_file("/proc/cmdline", buf2, 0);
        assert_se(IN_SET(r, -EACCES, -EIO));

        assert_se(write_string_file("/proc/cmdline", buf, WRITE_STRING_FILE_VERIFY_ON_FAILURE) == 0);
        assert_se(write_string_file("/proc/cmdline", buf2, WRITE_STRING_FILE_VERIFY_ON_FAILURE) == 0);

        r = write_string_file("/proc/cmdline", buf, WRITE_STRING_FILE_VERIFY_ON_FAILURE|WRITE_STRING_FILE_AVOID_NEWLINE);
        assert_se(IN_SET(r, -EACCES, -EIO));
        assert_se(write_string_file("/proc/cmdline", buf2, WRITE_STRING_FILE_VERIFY_ON_FAILURE|WRITE_STRING_FILE_AVOID_NEWLINE) == 0);
}

static void test_load_env_file_pairs(void) {
        char fn[] = "/tmp/test-load_env_file_pairs-XXXXXX";
        int fd;
        int r;
        _cleanup_fclose_ FILE *f = NULL;
        _cleanup_strv_free_ char **l = NULL;
        char **k, **v;

        fd = mkostemp_safe(fn);
        assert_se(fd >= 0);

        r = write_string_file(fn,
                        "NAME=\"Arch Linux\"\n"
                        "ID=arch\n"
                        "PRETTY_NAME=\"Arch Linux\"\n"
                        "ANSI_COLOR=\"0;36\"\n"
                        "HOME_URL=\"https://www.archlinux.org/\"\n"
                        "SUPPORT_URL=\"https://bbs.archlinux.org/\"\n"
                        "BUG_REPORT_URL=\"https://bugs.archlinux.org/\"\n",
                        WRITE_STRING_FILE_CREATE);
        assert_se(r == 0);

        f = fdopen(fd, "r");
        assert_se(f);

        r = load_env_file_pairs(f, fn, NULL, &l);
        assert_se(r >= 0);

        assert_se(strv_length(l) == 14);
        STRV_FOREACH_PAIR(k, v, l) {
                assert_se(STR_IN_SET(*k, "NAME", "ID", "PRETTY_NAME", "ANSI_COLOR", "HOME_URL", "SUPPORT_URL", "BUG_REPORT_URL"));
                printf("%s=%s\n", *k, *v);
                if (streq(*k, "NAME")) assert_se(streq(*v, "Arch Linux"));
                if (streq(*k, "ID")) assert_se(streq(*v, "arch"));
                if (streq(*k, "PRETTY_NAME")) assert_se(streq(*v, "Arch Linux"));
                if (streq(*k, "ANSI_COLOR")) assert_se(streq(*v, "0;36"));
                if (streq(*k, "HOME_URL")) assert_se(streq(*v, "https://www.archlinux.org/"));
                if (streq(*k, "SUPPORT_URL")) assert_se(streq(*v, "https://bbs.archlinux.org/"));
                if (streq(*k, "BUG_REPORT_URL")) assert_se(streq(*v, "https://bugs.archlinux.org/"));
        }

        unlink(fn);
}

static void test_search_and_fopen(void) {
        const char *dirs[] = {"/tmp/foo/bar", "/tmp", NULL};
        char name[] = "/tmp/test-search_and_fopen.XXXXXX";
        int fd = -1;
        int r;
        FILE *f;

        fd = mkostemp_safe(name);
        assert_se(fd >= 0);
        close(fd);

        r = search_and_fopen(basename(name), "r", NULL, dirs, &f);
        assert_se(r >= 0);
        fclose(f);

        r = search_and_fopen(name, "r", NULL, dirs, &f);
        assert_se(r >= 0);
        fclose(f);

        r = search_and_fopen(basename(name), "r", "/", dirs, &f);
        assert_se(r >= 0);
        fclose(f);

        r = search_and_fopen("/a/file/which/does/not/exist/i/guess", "r", NULL, dirs, &f);
        assert_se(r < 0);
        r = search_and_fopen("afilewhichdoesnotexistiguess", "r", NULL, dirs, &f);
        assert_se(r < 0);

        r = unlink(name);
        assert_se(r == 0);

        r = search_and_fopen(basename(name), "r", NULL, dirs, &f);
        assert_se(r < 0);
}


static void test_search_and_fopen_nulstr(void) {
        const char dirs[] = "/tmp/foo/bar\0/tmp\0";
        char name[] = "/tmp/test-search_and_fopen.XXXXXX";
        int fd = -1;
        int r;
        FILE *f;

        fd = mkostemp_safe(name);
        assert_se(fd >= 0);
        close(fd);

        r = search_and_fopen_nulstr(basename(name), "r", NULL, dirs, &f);
        assert_se(r >= 0);
        fclose(f);

        r = search_and_fopen_nulstr(name, "r", NULL, dirs, &f);
        assert_se(r >= 0);
        fclose(f);

        r = search_and_fopen_nulstr("/a/file/which/does/not/exist/i/guess", "r", NULL, dirs, &f);
        assert_se(r < 0);
        r = search_and_fopen_nulstr("afilewhichdoesnotexistiguess", "r", NULL, dirs, &f);
        assert_se(r < 0);

        r = unlink(name);
        assert_se(r == 0);

        r = search_and_fopen_nulstr(basename(name), "r", NULL, dirs, &f);
        assert_se(r < 0);
}

static void test_writing_tmpfile(void) {
        char name[] = "/tmp/test-systemd_writing_tmpfile.XXXXXX";
        _cleanup_free_ char *contents = NULL;
        size_t size;
        int fd, r;
        struct iovec iov[3];

        iov[0] = IOVEC_MAKE_STRING("abc\n");
        iov[1] = IOVEC_MAKE_STRING(ALPHANUMERICAL "\n");
        iov[2] = IOVEC_MAKE_STRING("");

        fd = mkostemp_safe(name);
        printf("tmpfile: %s", name);

        r = writev(fd, iov, 3);
        assert_se(r >= 0);

        r = read_full_file(name, &contents, &size);
        assert_se(r == 0);
        printf("contents: %s", contents);
        assert_se(streq(contents, "abc\n" ALPHANUMERICAL "\n"));

        unlink(name);
}

static void test_tempfn(void) {
        char *ret = NULL, *p;

        assert_se(tempfn_xxxxxx("/foo/bar/waldo", NULL, &ret) >= 0);
        assert_se(streq_ptr(ret, "/foo/bar/.#waldoXXXXXX"));
        free(ret);

        assert_se(tempfn_xxxxxx("/foo/bar/waldo", "[miau]", &ret) >= 0);
        assert_se(streq_ptr(ret, "/foo/bar/.#[miau]waldoXXXXXX"));
        free(ret);

        assert_se(tempfn_random("/foo/bar/waldo", NULL, &ret) >= 0);
        assert_se(p = startswith(ret, "/foo/bar/.#waldo"));
        assert_se(strlen(p) == 16);
        assert_se(in_charset(p, "0123456789abcdef"));
        free(ret);

        assert_se(tempfn_random("/foo/bar/waldo", "[wuff]", &ret) >= 0);
        assert_se(p = startswith(ret, "/foo/bar/.#[wuff]waldo"));
        assert_se(strlen(p) == 16);
        assert_se(in_charset(p, "0123456789abcdef"));
        free(ret);

        assert_se(tempfn_random_child("/foo/bar/waldo", NULL, &ret) >= 0);
        assert_se(p = startswith(ret, "/foo/bar/waldo/.#"));
        assert_se(strlen(p) == 16);
        assert_se(in_charset(p, "0123456789abcdef"));
        free(ret);

        assert_se(tempfn_random_child("/foo/bar/waldo", "[kikiriki]", &ret) >= 0);
        assert_se(p = startswith(ret, "/foo/bar/waldo/.#[kikiriki]"));
        assert_se(strlen(p) == 16);
        assert_se(in_charset(p, "0123456789abcdef"));
        free(ret);
}

static const char buffer[] =
        "Some test data\n"
        "With newlines, and a NUL byte\0"
        "\n"
        "an empty line\n"
        "an ignored line\n"
        "and a very long line that is supposed to be truncated, because it is so long\n";

static void test_read_line_one_file(FILE *f) {
        _cleanup_free_ char *line = NULL;

        assert_se(read_line(f, (size_t) -1, &line) == 15 && streq(line, "Some test data"));
        line = mfree(line);

        assert_se(read_line(f, 1024, &line) == 30 && streq(line, "With newlines, and a NUL byte"));
        line = mfree(line);

        assert_se(read_line(f, 1024, &line) == 1 && streq(line, ""));
        line = mfree(line);

        assert_se(read_line(f, 1024, &line) == 14 && streq(line, "an empty line"));
        line = mfree(line);

        assert_se(read_line(f, (size_t) -1, NULL) == 16);

        assert_se(read_line(f, 16, &line) == -ENOBUFS);
        line = mfree(line);

        /* read_line() stopped when it hit the limit, that means when we continue reading we'll read at the first
         * character after the previous limit. Let's make use of tha to continue our test. */
        assert_se(read_line(f, 1024, &line) == 61 && streq(line, "line that is supposed to be truncated, because it is so long"));
        line = mfree(line);

        assert_se(read_line(f, 1024, &line) == 1 && streq(line, ""));
        line = mfree(line);

        assert_se(read_line(f, 1024, &line) == 0 && streq(line, ""));
}

static void test_read_line(void) {
        _cleanup_fclose_ FILE *f = NULL;

        f = fmemopen((void*) buffer, sizeof(buffer), "re");
        assert_se(f);

        test_read_line_one_file(f);
}

static void test_read_line2(void) {
        char name[] = "/tmp/test-fileio.XXXXXX";
        int fd;
        _cleanup_fclose_ FILE *f = NULL;

        fd = mkostemp_safe(name);
        assert_se(fd >= 0);
        assert_se((size_t) write(fd, buffer, sizeof(buffer)) == sizeof(buffer));

        assert_se(lseek(fd, 0, SEEK_SET) == 0);
        assert_se(f = fdopen(fd, "r"));

        test_read_line_one_file(f);
}

static void test_read_line3(void) {
        _cleanup_fclose_ FILE *f = NULL;
        _cleanup_free_ char *line = NULL;
        int r;

        f = fopen("/proc/cmdline", "re");
        if (!f && IN_SET(errno, ENOENT, EPERM))
                return;
        assert_se(f);

        r = read_line(f, LINE_MAX, &line);
        assert_se((size_t) r == strlen(line) + 1);
        assert_se(read_line(f, LINE_MAX, NULL) == 0);
}

int main(int argc, char *argv[]) {
        log_set_max_level(LOG_DEBUG);
        log_parse_environment();
        log_open();

        test_parse_env_file();
        test_parse_multiline_env_file();
        test_merge_env_file();
        test_merge_env_file_invalid();
        test_executable_is_script();
        test_status_field();
        test_capeff();
        test_write_string_stream();
        test_write_string_file();
        test_write_string_file_no_create();
        test_write_string_file_verify();
        test_load_env_file_pairs();
        test_search_and_fopen();
        test_search_and_fopen_nulstr();
        test_writing_tmpfile();
        test_tempfn();
        test_read_line();
        test_read_line2();
        test_read_line3();

        return 0;
}
