/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "sd-journal.h"

#include "time-util.h"

char* format_timestamp_maybe_utc(char *buf, size_t l, usec_t t);
int acquire_journal(sd_journal **ret);
bool journal_boot_has_effect(sd_journal *j);
int journal_acquire_boot(sd_journal *j);
