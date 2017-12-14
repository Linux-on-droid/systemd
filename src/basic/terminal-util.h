/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

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

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

#include "macro.h"
#include "time-util.h"

#define ANSI_RED "\x1B[0;31m"
#define ANSI_GREEN "\x1B[0;32m"
#define ANSI_UNDERLINE "\x1B[0;4m"
#define ANSI_HIGHLIGHT "\x1B[0;1;39m"
#define ANSI_HIGHLIGHT_RED "\x1B[0;1;31m"
#define ANSI_HIGHLIGHT_GREEN "\x1B[0;1;32m"
#define ANSI_HIGHLIGHT_YELLOW "\x1B[0;1;33m"
#define ANSI_HIGHLIGHT_BLUE "\x1B[0;1;34m"
#define ANSI_HIGHLIGHT_UNDERLINE "\x1B[0;1;4m"
#define ANSI_HIGHLIGHT_RED_UNDERLINE "\x1B[0;1;4;31m"
#define ANSI_HIGHLIGHT_GREEN_UNDERLINE "\x1B[0;1;4;32m"
#define ANSI_HIGHLIGHT_YELLOW_UNDERLINE "\x1B[0;1;4;33m"
#define ANSI_HIGHLIGHT_BLUE_UNDERLINE "\x1B[0;1;4;34m"
#define ANSI_NORMAL "\x1B[0m"

#define ANSI_ERASE_TO_END_OF_LINE "\x1B[K"

/* Set cursor to top left corner and clear screen */
#define ANSI_HOME_CLEAR "\x1B[H\x1B[2J"

int reset_terminal_fd(int fd, bool switch_to_text);
int reset_terminal(const char *name);

int open_terminal(const char *name, int mode);
int acquire_terminal(const char *name, bool fail, bool force, bool ignore_tiocstty_eperm, usec_t timeout);
int release_terminal(void);

int terminal_vhangup_fd(int fd);
int terminal_vhangup(const char *name);

int chvt(int vt);

int read_one_char(FILE *f, char *ret, usec_t timeout, bool *need_nl);
int ask_char(char *ret, const char *replies, const char *text, ...) _printf_(3, 4);
int ask_string(char **ret, const char *text, ...) _printf_(2, 3);

int vt_disallocate(const char *name);

char *resolve_dev_console(char **active);
int get_kernel_consoles(char ***consoles);
bool tty_is_vc(const char *tty);
bool tty_is_vc_resolve(const char *tty);
bool tty_is_console(const char *tty) _pure_;
int vtnr_from_tty(const char *tty);
const char *default_term_for_tty(const char *tty);

int make_stdio(int fd);
int make_null_stdio(void);
int make_console_stdio(void);

int fd_columns(int fd);
unsigned columns(void);
int fd_lines(int fd);
unsigned lines(void);
void columns_lines_cache_reset(int _unused_ signum);

bool on_tty(void);
bool terminal_is_dumb(void);
bool colors_enabled(void);
bool underline_enabled(void);

#define DEFINE_ANSI_FUNC(name, NAME)                            \
        static inline const char *ansi_##name(void) {           \
                return colors_enabled() ? ANSI_##NAME : "";     \
        }                                                       \
        struct __useless_struct_to_allow_trailing_semicolon__

#define DEFINE_ANSI_FUNC_UNDERLINE(name, NAME, REPLACEMENT)             \
        static inline const char *ansi_##name(void) {                   \
                return underline_enabled() ? ANSI_##NAME :              \
                        colors_enabled() ? ANSI_##REPLACEMENT : "";     \
        }                                                               \
        struct __useless_struct_to_allow_trailing_semicolon__


DEFINE_ANSI_FUNC(highlight,                  HIGHLIGHT);
DEFINE_ANSI_FUNC(highlight_red,              HIGHLIGHT_RED);
DEFINE_ANSI_FUNC(highlight_green,            HIGHLIGHT_GREEN);
DEFINE_ANSI_FUNC(highlight_yellow,           HIGHLIGHT_YELLOW);
DEFINE_ANSI_FUNC(highlight_blue,             HIGHLIGHT_BLUE);
DEFINE_ANSI_FUNC(normal,                     NORMAL);

DEFINE_ANSI_FUNC_UNDERLINE(underline,                  UNDERLINE, NORMAL);
DEFINE_ANSI_FUNC_UNDERLINE(highlight_underline,        HIGHLIGHT_UNDERLINE, HIGHLIGHT);
DEFINE_ANSI_FUNC_UNDERLINE(highlight_red_underline,    HIGHLIGHT_RED_UNDERLINE, HIGHLIGHT_RED);
DEFINE_ANSI_FUNC_UNDERLINE(highlight_green_underline,  HIGHLIGHT_GREEN_UNDERLINE, HIGHLIGHT_GREEN);
DEFINE_ANSI_FUNC_UNDERLINE(highlight_yellow_underline, HIGHLIGHT_YELLOW_UNDERLINE, HIGHLIGHT_YELLOW);
DEFINE_ANSI_FUNC_UNDERLINE(highlight_blue_underline,   HIGHLIGHT_BLUE_UNDERLINE, HIGHLIGHT_BLUE);

int get_ctty_devnr(pid_t pid, dev_t *d);
int get_ctty(pid_t, dev_t *_devnr, char **r);

int getttyname_malloc(int fd, char **r);
int getttyname_harder(int fd, char **r);

int ptsname_malloc(int fd, char **ret);
int ptsname_namespace(int pty, char **ret);

int openpt_in_namespace(pid_t pid, int flags);
int open_terminal_in_namespace(pid_t pid, const char *name, int mode);

int vt_default_utf8(void);
int vt_reset_keyboard(int fd);
