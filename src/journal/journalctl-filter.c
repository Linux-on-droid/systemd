/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "sd-device.h"

#include "chase.h"
#include "devnum-util.h"
#include "fileio.h"
#include "glob-util.h"
#include "journal-internal.h"
#include "journalctl.h"
#include "journalctl-filter.h"
#include "journalctl-util.h"
#include "logs-show.h"
#include "missing_sched.h"
#include "nulstr-util.h"
#include "path-util.h"
#include "unit-name.h"

static int add_boot(sd_journal *j) {
        int r;

        assert(j);

        if (!arg_boot)
                return 0;

        assert(!sd_id128_is_null(arg_boot_id));

        r = add_match_boot_id(j, arg_boot_id);
        if (r < 0)
                return r;

        return sd_journal_add_conjunction(j);
}

static int add_dmesg(sd_journal *j) {
        int r;

        assert(j);

        if (!arg_dmesg)
                return 0;

        r = sd_journal_add_match(j, "_TRANSPORT=kernel", SIZE_MAX);
        if (r < 0)
                return r;

        return sd_journal_add_conjunction(j);
}

static int get_possible_units(
                sd_journal *j,
                const char *fields,
                char **patterns,
                Set **ret) {

        _cleanup_set_free_ Set *found = NULL;
        int r;

        assert(j);
        assert(fields);
        assert(ret);

        NULSTR_FOREACH(field, fields) {
                const void *data;
                size_t size;

                r = sd_journal_query_unique(j, field);
                if (r < 0)
                        return r;

                SD_JOURNAL_FOREACH_UNIQUE(j, data, size) {
                        _cleanup_free_ char *u = NULL;
                        char *eq;

                        eq = memchr(data, '=', size);
                        if (eq) {
                                size -= eq - (char*) data + 1;
                                data = ++eq;
                        }

                        u = strndup(data, size);
                        if (!u)
                                return -ENOMEM;

                        size_t i;
                        if (!strv_fnmatch_full(patterns, u, FNM_NOESCAPE, &i))
                                continue;

                        log_debug("Matched %s with pattern %s=%s", u, field, patterns[i]);
                        r = set_ensure_consume(&found, &string_hash_ops_free, TAKE_PTR(u));
                        if (r < 0)
                                return r;
                }
        }

        *ret = TAKE_PTR(found);
        return 0;
}

/* This list is supposed to return the superset of unit names
 * possibly matched by rules added with add_matches_for_unit... */
#define SYSTEM_UNITS                 \
        "_SYSTEMD_UNIT\0"            \
        "COREDUMP_UNIT\0"            \
        "UNIT\0"                     \
        "OBJECT_SYSTEMD_UNIT\0"      \
        "_SYSTEMD_SLICE\0"

/* ... and add_matches_for_user_unit */
#define USER_UNITS                   \
        "_SYSTEMD_USER_UNIT\0"       \
        "USER_UNIT\0"                \
        "COREDUMP_USER_UNIT\0"       \
        "OBJECT_SYSTEMD_USER_UNIT\0" \
        "_SYSTEMD_USER_SLICE\0"

static int add_units(sd_journal *j) {
        _cleanup_strv_free_ char **patterns = NULL;
        bool added = false;
        int r;

        assert(j);

        if (strv_isempty(arg_system_units) && strv_isempty(arg_user_units))
                return 0;

        STRV_FOREACH(i, arg_system_units) {
                _cleanup_free_ char *u = NULL;

                r = unit_name_mangle(*i, UNIT_NAME_MANGLE_GLOB | (arg_quiet ? 0 : UNIT_NAME_MANGLE_WARN), &u);
                if (r < 0)
                        return r;

                if (string_is_glob(u)) {
                        r = strv_consume(&patterns, TAKE_PTR(u));
                        if (r < 0)
                                return r;
                } else {
                        r = add_matches_for_unit(j, u);
                        if (r < 0)
                                return r;
                        r = sd_journal_add_disjunction(j);
                        if (r < 0)
                                return r;
                        added = true;
                }
        }

        if (!strv_isempty(patterns)) {
                _cleanup_set_free_ Set *units = NULL;
                char *u;

                r = get_possible_units(j, SYSTEM_UNITS, patterns, &units);
                if (r < 0)
                        return r;

                SET_FOREACH(u, units) {
                        r = add_matches_for_unit(j, u);
                        if (r < 0)
                                return r;
                        r = sd_journal_add_disjunction(j);
                        if (r < 0)
                                return r;
                        added = true;
                }
        }

        patterns = strv_free(patterns);

        STRV_FOREACH(i, arg_user_units) {
                _cleanup_free_ char *u = NULL;

                r = unit_name_mangle(*i, UNIT_NAME_MANGLE_GLOB | (arg_quiet ? 0 : UNIT_NAME_MANGLE_WARN), &u);
                if (r < 0)
                        return r;

                if (string_is_glob(u)) {
                        r = strv_consume(&patterns, TAKE_PTR(u));
                        if (r < 0)
                                return r;
                } else {
                        r = add_matches_for_user_unit(j, u);
                        if (r < 0)
                                return r;
                        r = sd_journal_add_disjunction(j);
                        if (r < 0)
                                return r;
                        added = true;
                }
        }

        if (!strv_isempty(patterns)) {
                _cleanup_set_free_ Set *units = NULL;
                char *u;

                r = get_possible_units(j, USER_UNITS, patterns, &units);
                if (r < 0)
                        return r;

                SET_FOREACH(u, units) {
                        r = add_matches_for_user_unit(j, u);
                        if (r < 0)
                                return r;
                        r = sd_journal_add_disjunction(j);
                        if (r < 0)
                                return r;
                        added = true;
                }
        }

        /* Complain if the user request matches but nothing whatsoever was found, since otherwise everything
         * would be matched. */
        if (!added)
                return -ENODATA;

        return sd_journal_add_conjunction(j);
}

static int add_syslog_identifier(sd_journal *j) {
        int r;

        assert(j);

        if (strv_isempty(arg_syslog_identifier))
                return 0;

        STRV_FOREACH(i, arg_syslog_identifier) {
                r = journal_add_match_pair(j, "SYSLOG_IDENTIFIER", *i);
                if (r < 0)
                        return r;
                r = sd_journal_add_disjunction(j);
                if (r < 0)
                        return r;
        }

        return sd_journal_add_conjunction(j);
}

static int add_exclude_identifier(sd_journal *j) {
        _cleanup_set_free_ Set *excludes = NULL;
        int r;

        assert(j);

        r = set_put_strdupv(&excludes, arg_exclude_identifier);
        if (r < 0)
                return r;

        return set_free_and_replace(j->exclude_syslog_identifiers, excludes);
}

static int add_priorities(sd_journal *j) {
        int r;

        assert(j);

        if (arg_priorities == 0)
                return 0;

        for (int i = LOG_EMERG; i <= LOG_DEBUG; i++)
                if (arg_priorities & (1 << i)) {
                        r = journal_add_matchf(j, "PRIORITY=%d", i);
                        if (r < 0)
                                return r;
                }

        return sd_journal_add_conjunction(j);
}

static int add_facilities(sd_journal *j) {
        int r;

        assert(j);

        if (set_isempty(arg_facilities))
                return 0;

        void *p;
        SET_FOREACH(p, arg_facilities) {
                r = journal_add_matchf(j, "SYSLOG_FACILITY=%d", PTR_TO_INT(p));
                if (r < 0)
                        return r;
        }

        return sd_journal_add_conjunction(j);
}

static int add_matches_for_executable(sd_journal *j, const char *path) {
        _cleanup_free_ char *interpreter = NULL;
        int r;

        assert(j);
        assert(path);

        if (executable_is_script(path, &interpreter) > 0) {
                _cleanup_free_ char *comm = NULL;

                r = path_extract_filename(path, &comm);
                if (r < 0)
                        return log_error_errno(r, "Failed to extract filename of '%s': %m", path);

                r = journal_add_match_pair(j, "_COMM", strshorten(comm, TASK_COMM_LEN-1));
                if (r < 0)
                        return log_error_errno(r, "Failed to add match: %m");

                /* Append _EXE only if the interpreter is not a link. Otherwise, it might be outdated often. */
                path = is_symlink(interpreter) > 0 ? interpreter : NULL;
        }

        if (path) {
                r = journal_add_match_pair(j, "_EXE", path);
                if (r < 0)
                        return log_error_errno(r, "Failed to add match: %m");
        }

        return 0;
}

static int add_matches_for_device(sd_journal *j, const char *devpath) {
        _cleanup_(sd_device_unrefp) sd_device *device = NULL;
        int r;

        assert(j);
        assert(devpath);

        r = sd_device_new_from_devname(&device, devpath);
        if (r < 0)
                return log_error_errno(r, "Failed to get device '%s': %m", devpath);

        for (sd_device *d = device; d; ) {
                const char *subsys, *sysname;

                r = sd_device_get_subsystem(d, &subsys);
                if (r < 0)
                        goto get_parent;

                r = sd_device_get_sysname(d, &sysname);
                if (r < 0)
                        goto get_parent;

                r = journal_add_matchf(j, "_KERNEL_DEVICE=+%s:%s", subsys, sysname);
                if (r < 0)
                        return log_error_errno(r, "Failed to add match: %m");

                dev_t devnum;
                if (sd_device_get_devnum(d, &devnum) >= 0) {
                        r = journal_add_matchf(j, "_KERNEL_DEVICE=%c" DEVNUM_FORMAT_STR,
                                               streq(subsys, "block") ? 'b' : 'c',
                                               DEVNUM_FORMAT_VAL(devnum));
                        if (r < 0)
                                return log_error_errno(r, "Failed to add match: %m");
                }

get_parent:
                if (sd_device_get_parent(d, &d) < 0)
                        break;
        }

        return add_match_boot_id(j, SD_ID128_NULL);
}

static int add_matches_for_path(sd_journal *j, const char *path) {
        _cleanup_free_ char *p = NULL;
        struct stat st;
        int r;

        assert(j);
        assert(path);

        if (arg_root || arg_machine)
                return log_error_errno(SYNTHETIC_ERRNO(EOPNOTSUPP),
                                       "An extra path in match filter is currently not supported with --root, --image, or -M/--machine.");

        r = chase_and_stat(path, NULL, 0, &p, &st);
        if (r < 0)
                return log_error_errno(r, "Couldn't canonicalize path '%s': %m", path);

        if (S_ISREG(st.st_mode) && (0111 & st.st_mode))
                return add_matches_for_executable(j, p);

        if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))
                return add_matches_for_device(j, p);

        return log_error_errno(SYNTHETIC_ERRNO(EINVAL), "File is neither a device node nor executable: %s", p);
}

static int add_matches(sd_journal *j, char **args) {
        bool have_term = false;
        int r;

        assert(j);

        if (strv_isempty(args))
                return 0;

        STRV_FOREACH(i, args)
                if (streq(*i, "+")) {
                        if (!have_term)
                                break;

                        r = sd_journal_add_disjunction(j);
                        if (r < 0)
                                return log_error_errno(r, "Failed to add disjunction: %m");

                        have_term = false;

                } else if (path_is_absolute(*i)) {
                        r = add_matches_for_path(j, *i);
                        if (r < 0)
                                return r;
                        have_term = true;

                } else {
                        r = sd_journal_add_match(j, *i, SIZE_MAX);
                        if (r < 0)
                                return log_error_errno(r, "Failed to add match '%s': %m", *i);
                        have_term = true;
                }

        if (!have_term)
                return log_error_errno(SYNTHETIC_ERRNO(EINVAL), "\"+\" can only be used between terms.");

        return 0;
}

int add_filters(sd_journal *j, char **matches) {
        int r;

        assert(j);

        /* First, search boot ID, as that may set and flush matches and seek journal. */
        r = journal_acquire_boot(j);
        if (r < 0)
                return r;

        /* Clear unexpected matches for safety. */
        sd_journal_flush_matches(j);

        /* Then, add filters in the below. */
        r = add_boot(j);
        if (r < 0)
                return log_error_errno(r, "Failed to add filter for boot: %m");

        r = add_dmesg(j);
        if (r < 0)
                return log_error_errno(r, "Failed to add filter for dmesg: %m");

        r = add_units(j);
        if (r < 0)
                return log_error_errno(r, "Failed to add filter for units: %m");

        r = add_syslog_identifier(j);
        if (r < 0)
                return log_error_errno(r, "Failed to add filter for syslog identifiers: %m");

        r = add_exclude_identifier(j);
        if (r < 0)
                return log_error_errno(r, "Failed to add exclude filter for syslog identifiers: %m");

        r = add_priorities(j);
        if (r < 0)
                return log_error_errno(r, "Failed to add filter for priorities: %m");

        r = add_facilities(j);
        if (r < 0)
                return log_error_errno(r, "Failed to add filter for facilities: %m");

        r = add_matches(j, matches);
        if (r < 0)
                return r;

        if (DEBUG_LOGGING) {
                _cleanup_free_ char *filter = NULL;

                filter = journal_make_match_string(j);
                if (!filter)
                        return log_oom();

                log_debug("Journal filter: %s", filter);
        }

        return 0;
}
