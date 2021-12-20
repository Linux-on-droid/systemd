/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fuzz.h"

#include "sd-dhcp-server.c"

/* stub out network so that the server doesn't send */
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen) {
        return len;
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
        return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
        _cleanup_(sd_dhcp_server_unrefp) sd_dhcp_server *server = NULL;
        struct in_addr address = {.s_addr = htobe32(UINT32_C(10) << 24 | UINT32_C(1))};
        static const uint8_t chaddr[] = {3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3};
        uint8_t *client_id;
        DHCPLease *lease;

        if (size < sizeof(DHCPMessage))
                return 0;

        assert_se(sd_dhcp_server_new(&server, 1) >= 0);
        server->fd = open("/dev/null", O_RDWR|O_CLOEXEC|O_NOCTTY);
        assert_se(server->fd >= 0);
        assert_se(sd_dhcp_server_configure_pool(server, &address, 24, 0, 0) >= 0);

        /* add a lease to the pool to expose additional code paths */
        client_id = malloc(2);
        assert_se(client_id);
        client_id[0] = 2;
        client_id[1] = 2;
        lease = new0(DHCPLease, 1);
        assert_se(lease);
        lease->client_id.length = 2;
        lease->client_id.data = client_id;
        lease->address = htobe32(UINT32_C(10) << 24 | UINT32_C(2));
        lease->gateway = htobe32(UINT32_C(10) << 24 | UINT32_C(1));
        lease->expiration = UINT64_MAX;
        memcpy(lease->chaddr, chaddr, 16);
        assert_se(hashmap_ensure_put(&server->bound_leases_by_client_id, &dhcp_lease_hash_ops, &lease->client_id, lease) >= 0);
        assert_se(hashmap_ensure_put(&server->bound_leases_by_address, NULL, UINT32_TO_PTR(lease->address), lease) >= 0);
        lease->server = server;

        (void) dhcp_server_handle_message(server, (DHCPMessage*)data, size);

        return 0;
}
