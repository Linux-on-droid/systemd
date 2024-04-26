/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "dlfcn-util.h"

#if HAVE_XKBCOMMON
#include <xkbcommon/xkbcommon.h>

DLSYM_PROTOTYPE(xkb_context_new);
DLSYM_PROTOTYPE(xkb_context_unref);
DLSYM_PROTOTYPE(xkb_context_set_log_fn);
DLSYM_PROTOTYPE(xkb_keymap_new_from_names);
DLSYM_PROTOTYPE(xkb_keymap_unref);

int verify_xkb_rmlvo(const char *model, const char *layout, const char *variant, const char *options);

#else

static inline int verify_xkb_rmlvo(const char *model, const char *layout, const char *variant, const char *options) {
        return 0;
}

#endif
