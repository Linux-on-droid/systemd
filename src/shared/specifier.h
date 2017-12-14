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

#include "string-util.h"

typedef int (*SpecifierCallback)(char specifier, void *data, void *userdata, char **ret);

typedef struct Specifier {
        const char specifier;
        const SpecifierCallback lookup;
        void *data;
} Specifier;

int specifier_printf(const char *text, const Specifier table[], void *userdata, char **ret);

int specifier_string(char specifier, void *data, void *userdata, char **ret);

int specifier_machine_id(char specifier, void *data, void *userdata, char **ret);
int specifier_boot_id(char specifier, void *data, void *userdata, char **ret);
int specifier_host_name(char specifier, void *data, void *userdata, char **ret);
int specifier_kernel_release(char specifier, void *data, void *userdata, char **ret);

int specifier_user_name(char specifier, void *data, void *userdata, char **ret);
int specifier_user_id(char specifier, void *data, void *userdata, char **ret);
int specifier_user_home(char specifier, void *data, void *userdata, char **ret);
int specifier_user_shell(char specifier, void *data, void *userdata, char **ret);

static inline char* specifier_escape(const char *string) {
        return strreplace(string, "%", "%%");
}

int specifier_escape_strv(char **l, char ***ret);
