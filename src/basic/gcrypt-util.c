/* SPDX-License-Identifier: LGPL-2.1-or-later */

#if HAVE_GCRYPT

#include "gcrypt-util.h"
#include "hexdecoct.h"

static void *gcrypt_dl = NULL;

static DLSYM_FUNCTION(gcry_control);
static DLSYM_FUNCTION(gcry_check_version);
DLSYM_FUNCTION(gcry_md_close);
DLSYM_FUNCTION(gcry_md_copy);
DLSYM_FUNCTION(gcry_md_ctl);
DLSYM_FUNCTION(gcry_md_get_algo_dlen);
DLSYM_FUNCTION(gcry_md_open);
DLSYM_FUNCTION(gcry_md_read);
DLSYM_FUNCTION(gcry_md_reset);
DLSYM_FUNCTION(gcry_md_setkey);
DLSYM_FUNCTION(gcry_md_write);
DLSYM_FUNCTION(gcry_mpi_add);
DLSYM_FUNCTION(gcry_mpi_add_ui);
DLSYM_FUNCTION(gcry_mpi_cmp);
DLSYM_FUNCTION(gcry_mpi_cmp_ui);
DLSYM_FUNCTION(gcry_mpi_get_nbits);
DLSYM_FUNCTION(gcry_mpi_invm);
DLSYM_FUNCTION(gcry_mpi_mod);
DLSYM_FUNCTION(gcry_mpi_mul);
DLSYM_FUNCTION(gcry_mpi_mulm);
DLSYM_FUNCTION(gcry_mpi_new);
DLSYM_FUNCTION(gcry_mpi_powm);
DLSYM_FUNCTION(gcry_mpi_print);
DLSYM_FUNCTION(gcry_mpi_release);
DLSYM_FUNCTION(gcry_mpi_scan);
DLSYM_FUNCTION(gcry_mpi_set_ui);
DLSYM_FUNCTION(gcry_mpi_sub);
DLSYM_FUNCTION(gcry_mpi_subm);
DLSYM_FUNCTION(gcry_mpi_sub_ui);
DLSYM_FUNCTION(gcry_prime_check);
DLSYM_FUNCTION(gcry_randomize);
DLSYM_FUNCTION(gcry_strerror);

static int dlopen_gcrypt(void) {
        return dlopen_many_sym_or_warn(
                        &gcrypt_dl,
                        "libgcrypt.so.20", LOG_DEBUG,
                        DLSYM_ARG(gcry_control),
                        DLSYM_ARG(gcry_check_version),
                        DLSYM_ARG(gcry_md_close),
                        DLSYM_ARG(gcry_md_copy),
                        DLSYM_ARG(gcry_md_ctl),
                        DLSYM_ARG(gcry_md_get_algo_dlen),
                        DLSYM_ARG(gcry_md_open),
                        DLSYM_ARG(gcry_md_read),
                        DLSYM_ARG(gcry_md_reset),
                        DLSYM_ARG(gcry_md_setkey),
                        DLSYM_ARG(gcry_md_write),
                        DLSYM_ARG(gcry_mpi_add),
                        DLSYM_ARG(gcry_mpi_add_ui),
                        DLSYM_ARG(gcry_mpi_cmp),
                        DLSYM_ARG(gcry_mpi_cmp_ui),
                        DLSYM_ARG(gcry_mpi_get_nbits),
                        DLSYM_ARG(gcry_mpi_invm),
                        DLSYM_ARG(gcry_mpi_mod),
                        DLSYM_ARG(gcry_mpi_mul),
                        DLSYM_ARG(gcry_mpi_mulm),
                        DLSYM_ARG(gcry_mpi_new),
                        DLSYM_ARG(gcry_mpi_powm),
                        DLSYM_ARG(gcry_mpi_print),
                        DLSYM_ARG(gcry_mpi_release),
                        DLSYM_ARG(gcry_mpi_scan),
                        DLSYM_ARG(gcry_mpi_set_ui),
                        DLSYM_ARG(gcry_mpi_sub),
                        DLSYM_ARG(gcry_mpi_subm),
                        DLSYM_ARG(gcry_mpi_sub_ui),
                        DLSYM_ARG(gcry_prime_check),
                        DLSYM_ARG(gcry_randomize),
                        DLSYM_ARG(gcry_strerror));
}

int initialize_libgcrypt(bool secmem) {
        int r;

        r = dlopen_gcrypt();
        if (r < 0)
                return r;

        if (sym_gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P))
                return 0;

        sym_gcry_control(GCRYCTL_SET_PREFERRED_RNG_TYPE, GCRY_RNG_TYPE_SYSTEM);
        assert_se(sym_gcry_check_version("1.4.5"));

        /* Turn off "secmem". Clients which wish to make use of this
         * feature should initialize the library manually */
        if (!secmem)
                sym_gcry_control(GCRYCTL_DISABLE_SECMEM);

        sym_gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);

        return 0;
}

#  if !PREFER_OPENSSL
int string_hashsum(const char *s, size_t len, int md_algorithm, char **out) {
        _cleanup_(sym_gcry_md_closep) gcry_md_hd_t md = NULL;
        gcry_error_t err;
        size_t hash_size;
        void *hash;
        char *enc;
        int r;

        r = initialize_libgcrypt(false);
        if (r < 0)
                return r;

        hash_size = sym_gcry_md_get_algo_dlen(md_algorithm);
        assert(hash_size > 0);

        err = sym_gcry_md_open(&md, md_algorithm, 0);
        if (gcry_err_code(err) != GPG_ERR_NO_ERROR || !md)
                return -EIO;

        sym_gcry_md_write(md, s, len);

        hash = sym_gcry_md_read(md, 0);
        if (!hash)
                return -EIO;

        enc = hexmem(hash, hash_size);
        if (!enc)
                return -ENOMEM;

        *out = enc;
        return 0;
}
#  endif
#endif
