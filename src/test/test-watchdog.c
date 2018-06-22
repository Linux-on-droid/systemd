/* SPDX-License-Identifier: LGPL-2.1+ */

#include <string.h>
#include <unistd.h>

#include "env-util.h"
#include "log.h"
#include "watchdog.h"

int main(int argc, char *argv[]) {
        usec_t t;
        unsigned i, count;
        int r;
        bool slow;

        log_set_max_level(LOG_DEBUG);
        log_parse_environment();

        r = getenv_bool("SYSTEMD_SLOW_TESTS");
        slow = r >= 0 ? r : SYSTEMD_SLOW_TESTS_DEFAULT;

        t = slow ? 10 * USEC_PER_SEC : 1 * USEC_PER_SEC;
        count = slow ? 5 : 3;

        r = watchdog_set_timeout(&t);
        if (r < 0)
                log_warning_errno(r, "Failed to open watchdog: %m");
        if (r == -EPERM)
                t = 0;

        for (i = 0; i < count; i++) {
                log_info("Pinging...");
                r = watchdog_ping();
                if (r < 0)
                        log_warning_errno(r, "Failed to ping watchdog: %m");

                usleep(t/2);
        }

        watchdog_close(true);
        return 0;
}
