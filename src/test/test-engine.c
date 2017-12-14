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
#include <stdio.h>
#include <string.h>

#include "bus-util.h"
#include "manager.h"
#include "rm-rf.h"
#include "test-helper.h"
#include "tests.h"

int main(int argc, char *argv[]) {
        _cleanup_(rm_rf_physical_and_freep) char *runtime_dir = NULL;
        _cleanup_(sd_bus_error_free) sd_bus_error err = SD_BUS_ERROR_NULL;
        Manager *m = NULL;
        Unit *a = NULL, *b = NULL, *c = NULL, *d = NULL, *e = NULL, *g = NULL, *h = NULL;
        FILE *serial = NULL;
        FDSet *fdset = NULL;
        Job *j;
        int r;

        r = enter_cgroup_subroot();
        if (r == -ENOMEDIUM) {
                log_notice_errno(r, "Skipping test: cgroupfs not available");
                return EXIT_TEST_SKIP;
        }

        /* prepare the test */
        assert_se(set_unit_path(get_testdata_dir("")) >= 0);
        assert_se(runtime_dir = setup_fake_runtime_dir());
        r = manager_new(UNIT_FILE_USER, MANAGER_TEST_RUN_MINIMAL, &m);
        if (MANAGER_SKIP_TEST(r)) {
                log_notice_errno(r, "Skipping test: manager_new: %m");
                return EXIT_TEST_SKIP;
        }
        assert_se(r >= 0);
        assert_se(manager_startup(m, serial, fdset) >= 0);

        printf("Load1:\n");
        assert_se(manager_load_unit(m, "a.service", NULL, NULL, &a) >= 0);
        assert_se(manager_load_unit(m, "b.service", NULL, NULL, &b) >= 0);
        assert_se(manager_load_unit(m, "c.service", NULL, NULL, &c) >= 0);
        manager_dump_units(m, stdout, "\t");

        printf("Test1: (Trivial)\n");
        r = manager_add_job(m, JOB_START, c, JOB_REPLACE, &err, &j);
        if (sd_bus_error_is_set(&err))
                log_error("error: %s: %s", err.name, err.message);
        assert_se(r == 0);
        manager_dump_jobs(m, stdout, "\t");

        printf("Load2:\n");
        manager_clear_jobs(m);
        assert_se(manager_load_unit(m, "d.service", NULL, NULL, &d) >= 0);
        assert_se(manager_load_unit(m, "e.service", NULL, NULL, &e) >= 0);
        manager_dump_units(m, stdout, "\t");

        printf("Test2: (Cyclic Order, Unfixable)\n");
        assert_se(manager_add_job(m, JOB_START, d, JOB_REPLACE, NULL, &j) == -EDEADLK);
        manager_dump_jobs(m, stdout, "\t");

        printf("Test3: (Cyclic Order, Fixable, Garbage Collector)\n");
        assert_se(manager_add_job(m, JOB_START, e, JOB_REPLACE, NULL, &j) == 0);
        manager_dump_jobs(m, stdout, "\t");

        printf("Test4: (Identical transaction)\n");
        assert_se(manager_add_job(m, JOB_START, e, JOB_FAIL, NULL, &j) == 0);
        manager_dump_jobs(m, stdout, "\t");

        printf("Load3:\n");
        assert_se(manager_load_unit(m, "g.service", NULL, NULL, &g) >= 0);
        manager_dump_units(m, stdout, "\t");

        printf("Test5: (Colliding transaction, fail)\n");
        assert_se(manager_add_job(m, JOB_START, g, JOB_FAIL, NULL, &j) == -EDEADLK);

        printf("Test6: (Colliding transaction, replace)\n");
        assert_se(manager_add_job(m, JOB_START, g, JOB_REPLACE, NULL, &j) == 0);
        manager_dump_jobs(m, stdout, "\t");

        printf("Test7: (Unmergeable job type, fail)\n");
        assert_se(manager_add_job(m, JOB_STOP, g, JOB_FAIL, NULL, &j) == -EDEADLK);

        printf("Test8: (Mergeable job type, fail)\n");
        assert_se(manager_add_job(m, JOB_RESTART, g, JOB_FAIL, NULL, &j) == 0);
        manager_dump_jobs(m, stdout, "\t");

        printf("Test9: (Unmergeable job type, replace)\n");
        assert_se(manager_add_job(m, JOB_STOP, g, JOB_REPLACE, NULL, &j) == 0);
        manager_dump_jobs(m, stdout, "\t");

        printf("Load4:\n");
        assert_se(manager_load_unit(m, "h.service", NULL, NULL, &h) >= 0);
        manager_dump_units(m, stdout, "\t");

        printf("Test10: (Unmergeable job type of auxiliary job, fail)\n");
        assert_se(manager_add_job(m, JOB_START, h, JOB_FAIL, NULL, &j) == 0);
        manager_dump_jobs(m, stdout, "\t");

        assert_se(!hashmap_get(a->dependencies[UNIT_PROPAGATES_RELOAD_TO], b));
        assert_se(!hashmap_get(b->dependencies[UNIT_RELOAD_PROPAGATED_FROM], a));
        assert_se(!hashmap_get(a->dependencies[UNIT_PROPAGATES_RELOAD_TO], c));
        assert_se(!hashmap_get(c->dependencies[UNIT_RELOAD_PROPAGATED_FROM], a));

        assert_se(unit_add_dependency(a, UNIT_PROPAGATES_RELOAD_TO, b, true, UNIT_DEPENDENCY_UDEV) == 0);
        assert_se(unit_add_dependency(a, UNIT_PROPAGATES_RELOAD_TO, c, true, UNIT_DEPENDENCY_PROC_SWAP) == 0);

        assert_se(hashmap_get(a->dependencies[UNIT_PROPAGATES_RELOAD_TO], b));
        assert_se(hashmap_get(b->dependencies[UNIT_RELOAD_PROPAGATED_FROM], a));
        assert_se(hashmap_get(a->dependencies[UNIT_PROPAGATES_RELOAD_TO], c));
        assert_se(hashmap_get(c->dependencies[UNIT_RELOAD_PROPAGATED_FROM], a));

        unit_remove_dependencies(a, UNIT_DEPENDENCY_UDEV);

        assert_se(!hashmap_get(a->dependencies[UNIT_PROPAGATES_RELOAD_TO], b));
        assert_se(!hashmap_get(b->dependencies[UNIT_RELOAD_PROPAGATED_FROM], a));
        assert_se(hashmap_get(a->dependencies[UNIT_PROPAGATES_RELOAD_TO], c));
        assert_se(hashmap_get(c->dependencies[UNIT_RELOAD_PROPAGATED_FROM], a));

        unit_remove_dependencies(a, UNIT_DEPENDENCY_PROC_SWAP);

        assert_se(!hashmap_get(a->dependencies[UNIT_PROPAGATES_RELOAD_TO], b));
        assert_se(!hashmap_get(b->dependencies[UNIT_RELOAD_PROPAGATED_FROM], a));
        assert_se(!hashmap_get(a->dependencies[UNIT_PROPAGATES_RELOAD_TO], c));
        assert_se(!hashmap_get(c->dependencies[UNIT_RELOAD_PROPAGATED_FROM], a));

        manager_free(m);

        return 0;
}
