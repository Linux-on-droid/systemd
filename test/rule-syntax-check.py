#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1+
#
# Simple udev rules syntax checker
#
# (C) 2010 Canonical Ltd.
# Author: Martin Pitt <martin.pitt@ubuntu.com>
#
# systemd is free software; you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 2.1 of the License, or
# (at your option) any later version.

# systemd is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with systemd; If not, see <http://www.gnu.org/licenses/>.

import re
import sys
import os
from glob import glob

rules_files = sys.argv[1:]
if not rules_files:
    sys.exit('Specify files to test as arguments')

quoted_string_re = r'"(?:[^\\"]|\\.)*"'
no_args_tests = re.compile(r'(ACTION|DEVPATH|KERNELS?|NAME|SYMLINK|SUBSYSTEMS?|DRIVERS?|TAG|PROGRAM|RESULT|TEST)\s*(?:=|!)=\s*' + quoted_string_re + '$')
args_tests = re.compile(r'(ATTRS?|ENV|TEST){([a-zA-Z0-9/_.*%-]+)}\s*(?:=|!)=\s*' + quoted_string_re + '$')
no_args_assign = re.compile(r'(NAME|SYMLINK|OWNER|GROUP|MODE|TAG|RUN|LABEL|GOTO|OPTIONS|IMPORT)\s*(?:\+=|:=|=)\s*' + quoted_string_re + '$')
args_assign = re.compile(r'(ATTR|ENV|IMPORT|RUN){([a-zA-Z0-9/_.*%-]+)}\s*(=|\+=)\s*' + quoted_string_re + '$')
# Find comma-separated groups, but allow commas that are inside quoted strings.
# Using quoted_string_re + '?' so that strings missing the last double quote
# will still match for this part that splits on commas.
comma_separated_group_re = re.compile(r'(?:[^,"]|' + quoted_string_re + '?)+')

result = 0
buffer = ''
for path in rules_files:
    print('# looking at {}'.format(path))
    lineno = 0
    for line in open(path):
        lineno += 1

        # handle line continuation
        if line.endswith('\\\n'):
            buffer += line[:-2]
            continue
        else:
            line = buffer + line
            buffer = ''

        # filter out comments and empty lines
        line = line.strip()
        if not line or line.startswith('#'):
            continue

        # Separator ',' is normally optional but we make it mandatory here as
        # it generally improves the readability of the rules.
        for clause_match in comma_separated_group_re.finditer(line):
            clause = clause_match.group().strip()
            if not (no_args_tests.match(clause) or args_tests.match(clause) or
                    no_args_assign.match(clause) or args_assign.match(clause)):

                print('Invalid line {}:{}: {}'.format(path, lineno, line))
                print('  clause:', clause)
                print()
                result = 1
                break

sys.exit(result)
