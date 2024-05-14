/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <limits.h>

#include "sd-id128.h"

#include "hostname-util.h"
#include "json.h"
#include "machine-varlink.h"
#include "machine.h"
#include "path-util.h"
#include "pidref.h"
#include "process-util.h"
#include "socket-util.h"
#include "string-util.h"
#include "varlink.h"

static JSON_DISPATCH_ENUM_DEFINE(dispatch_machine_class, MachineClass, machine_class_from_string);

static int machine_name(const char *name, JsonVariant *variant, JsonDispatchFlags flags, void *userdata) {
        char **m = ASSERT_PTR(userdata);
        const char *hostname;
        int r;

        assert(variant);

        if (!json_variant_is_string(variant))
                return json_log(variant, flags, SYNTHETIC_ERRNO(EINVAL), "JSON field '%s' is not a string.", strna(name));

        hostname = json_variant_string(variant);
        if (!hostname_is_valid(hostname, /* flags= */ 0))
                return json_log(variant, flags, SYNTHETIC_ERRNO(EINVAL), "Invalid machine name");

        r = free_and_strdup(m, hostname);
        if (r < 0)
                return json_log_oom(variant, flags);

        return 0;
}

static int machine_leader(const char *name, JsonVariant *variant, JsonDispatchFlags flags, void *userdata) {
        PidRef *leader = ASSERT_PTR(userdata);
        _cleanup_(pidref_done) PidRef temp = PIDREF_NULL;
        uint64_t k;
        int r;

        if (!json_variant_is_unsigned(variant))
                return json_log(variant, flags, SYNTHETIC_ERRNO(EINVAL), "JSON field '%s' is not an integer.", strna(name));

        k = json_variant_unsigned(variant);
        if (k > PID_T_MAX || !pid_is_valid(k))
                return json_log(variant, flags, SYNTHETIC_ERRNO(EINVAL), "JSON field '%s' is not a valid PID.", strna(name));

        if (k == 1)
                return json_log(variant, flags, SYNTHETIC_ERRNO(EINVAL), "JSON field '%s' is not a valid leader PID.", strna(name));

        r = pidref_set_pid(&temp, k);
        if (r < 0)
                return json_log(variant, flags, r, "Failed to pin process " PID_FMT ": %m", leader->pid);

        pidref_done(leader);

        *leader = TAKE_PIDREF(temp);

        return 0;
}

static int machine_ifindices(const char *name, JsonVariant *variant, JsonDispatchFlags flags, void *userdata) {
        Machine *m = ASSERT_PTR(userdata);
        _cleanup_free_ int *netif = NULL;
        size_t n_netif, k = 0;

        assert(variant);

        if (!json_variant_is_array(variant))
                return json_log(variant, flags, SYNTHETIC_ERRNO(EINVAL), "JSON field '%s' is not an array.", strna(name));

        n_netif = json_variant_elements(variant);

        netif = new(int, n_netif);
        if (!netif)
                return json_log_oom(variant, flags);

        JsonVariant *i;
        JSON_VARIANT_ARRAY_FOREACH(i, variant) {
                uint64_t b;

                if (!json_variant_is_unsigned(i))
                        return json_log(variant, flags, SYNTHETIC_ERRNO(EINVAL), "Element %zu of JSON field '%s' is not an unsigned integer.", k, strna(name));

                b = json_variant_unsigned(i);
                if (b > INT_MAX || b <= 0)
                        return json_log(variant, flags, SYNTHETIC_ERRNO(EINVAL), "Invalid network interface index %"PRIu64, b);

                netif[k++] = (int) b;
        }
        assert(k == n_netif);

        free_and_replace(m->netif, netif);
        m->n_netif = n_netif;

        return 0;
}

static int machine_cid(const char *name, JsonVariant *variant, JsonDispatchFlags flags, void *userdata) {
        unsigned cid, *c = ASSERT_PTR(userdata);

        assert(variant);

        if (!json_variant_is_unsigned(variant))
                return json_log(variant, flags, SYNTHETIC_ERRNO(EINVAL), "JSON field '%s' is not a string.", strna(name));

        cid = json_variant_unsigned(variant);
        if (!VSOCK_CID_IS_REGULAR(cid))
                return json_log(variant, flags, SYNTHETIC_ERRNO(EINVAL), "JSON field '%s' is not a regular VSOCK CID.", strna(name));

        *c = cid;

        return 0;
}

int vl_method_register(Varlink *link, JsonVariant *parameters, VarlinkMethodFlags flags, void *userdata) {
        Manager *manager = ASSERT_PTR(userdata);
        _cleanup_(machine_freep) Machine *machine = NULL;
        int r;

        static const JsonDispatch dispatch_table[] = {
                { "name",              JSON_VARIANT_STRING,   machine_name,                offsetof(Machine, name),                 JSON_MANDATORY },
                { "id",                JSON_VARIANT_STRING,   json_dispatch_id128,         offsetof(Machine, id),                   0 },
                { "service",           JSON_VARIANT_STRING,   json_dispatch_string,        offsetof(Machine, service),              0 },
                { "class",             JSON_VARIANT_STRING,   dispatch_machine_class,      offsetof(Machine, class),                JSON_MANDATORY },
                { "leader",            JSON_VARIANT_UNSIGNED, machine_leader,              offsetof(Machine, leader),               0 },
                { "rootDirectory",     JSON_VARIANT_STRING,   json_dispatch_absolute_path, offsetof(Machine, root_directory),       0 },
                { "ifIndices",         JSON_VARIANT_ARRAY,    machine_ifindices,           0,                                       0 },
                { "vSockCid",          JSON_VARIANT_UNSIGNED, machine_cid,                 offsetof(Machine, vsock_cid),            0 },
                { "sshAddress",        JSON_VARIANT_STRING,   json_dispatch_string,        offsetof(Machine, ssh_address),          JSON_SAFE },
                { "sshPrivateKeyPath", JSON_VARIANT_STRING,   json_dispatch_absolute_path, offsetof(Machine, ssh_private_key_path), 0 },
                {}
        };

        r = machine_new(_MACHINE_CLASS_INVALID, NULL, &machine);
        if (r < 0)
                return r;

        r = varlink_dispatch(link, parameters, dispatch_table, machine);
        if (r != 0)
                return r;

        if (!pidref_is_set(&machine->leader)) {
                r = varlink_get_peer_pidref(link, &machine->leader);
                if (r < 0)
                        return r;
        }

        r = machine_link(manager, machine);
        if (r == -EEXIST)
                return varlink_error(link, "io.systemd.Machine.MachineExists", NULL);
        if (r < 0)
                return r;

        r = cg_pidref_get_unit(&machine->leader, &machine->unit);
        if (r < 0)
                return r;

        r = machine_start(machine, NULL, NULL);
        if (r < 0)
                return r;

        /* the manager will free this machine */
        TAKE_PTR(machine);

        return varlink_reply(link, NULL);
}
