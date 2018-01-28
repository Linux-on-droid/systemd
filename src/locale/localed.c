/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2011 Lennart Poettering
  Copyright 2013 Kay Sievers

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
#include <string.h>
#include <unistd.h>

#if HAVE_XKBCOMMON
#include <xkbcommon/xkbcommon.h>
#include <dlfcn.h>
#endif

#include "sd-bus.h"

#include "alloc-util.h"
#include "bus-error.h"
#include "bus-message.h"
#include "bus-util.h"
#include "def.h"
#include "keymap-util.h"
#include "locale-util.h"
#include "macro.h"
#include "path-util.h"
#include "selinux-util.h"
#include "string-util.h"
#include "strv.h"
#include "user-util.h"

static Hashmap *polkit_registry = NULL;

static int locale_update_system_manager(Context *c, sd_bus *bus) {
        _cleanup_free_ char **l_unset = NULL;
        _cleanup_strv_free_ char **l_set = NULL;
        _cleanup_(sd_bus_message_unrefp) sd_bus_message *m = NULL;
        sd_bus_error error = SD_BUS_ERROR_NULL;
        unsigned c_set, c_unset, p;
        int r;

        assert(bus);

        l_unset = new0(char*, _VARIABLE_LC_MAX);
        if (!l_unset)
                return -ENOMEM;

        l_set = new0(char*, _VARIABLE_LC_MAX);
        if (!l_set)
                return -ENOMEM;

        for (p = 0, c_set = 0, c_unset = 0; p < _VARIABLE_LC_MAX; p++) {
                const char *name;

                name = locale_variable_to_string(p);
                assert(name);

                if (isempty(c->locale[p]))
                        l_unset[c_set++] = (char*) name;
                else {
                        char *s;

                        if (asprintf(&s, "%s=%s", name, c->locale[p]) < 0)
                                return -ENOMEM;

                        l_set[c_unset++] = s;
                }
        }

        assert(c_set + c_unset == _VARIABLE_LC_MAX);
        r = sd_bus_message_new_method_call(bus, &m,
                        "org.freedesktop.systemd1",
                        "/org/freedesktop/systemd1",
                        "org.freedesktop.systemd1.Manager",
                        "UnsetAndSetEnvironment");
        if (r < 0)
                return r;

        r = sd_bus_message_append_strv(m, l_unset);
        if (r < 0)
                return r;

        r = sd_bus_message_append_strv(m, l_set);
        if (r < 0)
                return r;

        r = sd_bus_call(bus, m, 0, &error, NULL);
        if (r < 0)
                log_error_errno(r, "Failed to update the manager environment: %m");

        return 0;
}

static int vconsole_reload(sd_bus *bus) {
        _cleanup_(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
        int r;

        assert(bus);

        r = sd_bus_call_method(bus,
                        "org.freedesktop.systemd1",
                        "/org/freedesktop/systemd1",
                        "org.freedesktop.systemd1.Manager",
                        "RestartUnit",
                        &error,
                        NULL,
                        "ss", "systemd-vconsole-setup.service", "replace");

        if (r < 0)
                log_error("Failed to issue method call: %s", bus_error_message(&error, -r));
        return r;
}

static int vconsole_convert_to_x11_and_emit(Context *c, sd_bus *bus) {
        int r;

        assert(bus);

        r = vconsole_convert_to_x11(c);
        if (r <= 0)
                return r;

        /* modified */
        r = x11_write_data(c);
        if (r < 0)
                return log_error_errno(r, "Failed to write X11 keyboard layout: %m");

        sd_bus_emit_properties_changed(bus,
                                       "/org/freedesktop/locale1",
                                       "org.freedesktop.locale1",
                                       "X11Layout", "X11Model", "X11Variant", "X11Options", NULL);

        return 1;
}

static int x11_convert_to_vconsole_and_emit(Context *c, sd_bus *bus) {
        int r;

        assert(bus);

        r = x11_convert_to_vconsole(c);
        if (r <= 0)
                return r;

        /* modified */
        r = vconsole_write_data(c);
        if (r < 0)
                log_error_errno(r, "Failed to save virtual console keymap: %m");

        sd_bus_emit_properties_changed(bus,
                                       "/org/freedesktop/locale1",
                                       "org.freedesktop.locale1",
                                       "VConsoleKeymap", "VConsoleKeymapToggle", NULL);

        return vconsole_reload(bus);
}

static int property_get_locale(
                sd_bus *bus,
                const char *path,
                const char *interface,
                const char *property,
                sd_bus_message *reply,
                void *userdata,
                sd_bus_error *error) {

        Context *c = userdata;
        _cleanup_strv_free_ char **l = NULL;
        int p, q;

        l = new0(char*, _VARIABLE_LC_MAX+1);
        if (!l)
                return -ENOMEM;

        for (p = 0, q = 0; p < _VARIABLE_LC_MAX; p++) {
                char *t;
                const char *name;

                name = locale_variable_to_string(p);
                assert(name);

                if (isempty(c->locale[p]))
                        continue;

                if (asprintf(&t, "%s=%s", name, c->locale[p]) < 0)
                        return -ENOMEM;

                l[q++] = t;
        }

        return sd_bus_message_append_strv(reply, l);
}

static int method_set_locale(sd_bus_message *m, void *userdata, sd_bus_error *error) {
        Context *c = userdata;
        _cleanup_strv_free_ char **l = NULL;
        char **i;
        const char *lang = NULL;
        int interactive;
        bool modified = false;
        bool have[_VARIABLE_LC_MAX] = {};
        int p;
        int r;

        assert(m);
        assert(c);

        r = bus_message_read_strv_extend(m, &l);
        if (r < 0)
                return r;

        r = sd_bus_message_read_basic(m, 'b', &interactive);
        if (r < 0)
                return r;

        /* Check whether a variable changed and if it is valid */
        STRV_FOREACH(i, l) {
                bool valid = false;

                for (p = 0; p < _VARIABLE_LC_MAX; p++) {
                        size_t k;
                        const char *name;

                        name = locale_variable_to_string(p);
                        assert(name);

                        k = strlen(name);
                        if (startswith(*i, name) &&
                            (*i)[k] == '=' &&
                            locale_is_valid((*i) + k + 1)) {
                                valid = true;
                                have[p] = true;

                                if (p == VARIABLE_LANG)
                                        lang = (*i) + k + 1;

                                if (!streq_ptr(*i + k + 1, c->locale[p]))
                                        modified = true;

                                break;
                        }
                }

                if (!valid)
                        return sd_bus_error_setf(error, SD_BUS_ERROR_INVALID_ARGS, "Invalid Locale data.");
        }

        /* If LANG was specified, but not LANGUAGE, check if we should
         * set it based on the language fallback table. */
        if (have[VARIABLE_LANG] && !have[VARIABLE_LANGUAGE]) {
                _cleanup_free_ char *language = NULL;

                assert(lang);

                (void) find_language_fallback(lang, &language);
                if (language) {
                        log_debug("Converted LANG=%s to LANGUAGE=%s", lang, language);
                        if (!streq_ptr(language, c->locale[VARIABLE_LANGUAGE])) {
                                r = strv_extendf(&l, "LANGUAGE=%s", language);
                                if (r < 0)
                                        return r;

                                have[VARIABLE_LANGUAGE] = true;
                                modified = true;
                        }
                }
        }

        /* Check whether a variable is unset */
        if (!modified)
                for (p = 0; p < _VARIABLE_LC_MAX; p++)
                        if (!isempty(c->locale[p]) && !have[p]) {
                                modified = true;
                                break;
                        }

        if (modified) {
                _cleanup_strv_free_ char **settings = NULL;

                r = bus_verify_polkit_async(
                                m,
                                CAP_SYS_ADMIN,
                                "org.freedesktop.locale1.set-locale",
                                NULL,
                                interactive,
                                UID_INVALID,
                                &polkit_registry,
                                error);
                if (r < 0)
                        return r;
                if (r == 0)
                        return 1; /* No authorization for now, but the async polkit stuff will call us again when it has it */

                STRV_FOREACH(i, l)
                        for (p = 0; p < _VARIABLE_LC_MAX; p++) {
                                size_t k;
                                const char *name;

                                name = locale_variable_to_string(p);
                                assert(name);

                                k = strlen(name);
                                if (startswith(*i, name) && (*i)[k] == '=') {
                                        r = free_and_strdup(&c->locale[p], *i + k + 1);
                                        if (r < 0)
                                                return r;
                                        break;
                                }
                        }

                for (p = 0; p < _VARIABLE_LC_MAX; p++) {
                        if (have[p])
                                continue;

                        c->locale[p] = mfree(c->locale[p]);
                }

                locale_simplify(c);

                r = locale_write_data(c, &settings);
                if (r < 0) {
                        log_error_errno(r, "Failed to set locale: %m");
                        return sd_bus_error_set_errnof(error, r, "Failed to set locale: %m");
                }

                locale_update_system_manager(c, sd_bus_message_get_bus(m));

                if (settings) {
                        _cleanup_free_ char *line;

                        line = strv_join(settings, ", ");
                        log_info("Changed locale to %s.", strnull(line));
                } else
                        log_info("Changed locale to unset.");

                (void) sd_bus_emit_properties_changed(
                                sd_bus_message_get_bus(m),
                                "/org/freedesktop/locale1",
                                "org.freedesktop.locale1",
                                "Locale", NULL);
        } else
                log_debug("Locale settings were not modified.");


        return sd_bus_reply_method_return(m, NULL);
}

static int method_set_vc_keyboard(sd_bus_message *m, void *userdata, sd_bus_error *error) {
        Context *c = userdata;
        const char *keymap, *keymap_toggle;
        int convert, interactive;
        int r;

        assert(m);
        assert(c);

        r = sd_bus_message_read(m, "ssbb", &keymap, &keymap_toggle, &convert, &interactive);
        if (r < 0)
                return r;

        keymap = empty_to_null(keymap);
        keymap_toggle = empty_to_null(keymap_toggle);

        if (!streq_ptr(keymap, c->vc_keymap) ||
            !streq_ptr(keymap_toggle, c->vc_keymap_toggle)) {

                if ((keymap && (!filename_is_valid(keymap) || !string_is_safe(keymap))) ||
                    (keymap_toggle && (!filename_is_valid(keymap_toggle) || !string_is_safe(keymap_toggle))))
                        return sd_bus_error_setf(error, SD_BUS_ERROR_INVALID_ARGS, "Received invalid keymap data");

                r = bus_verify_polkit_async(
                                m,
                                CAP_SYS_ADMIN,
                                "org.freedesktop.locale1.set-keyboard",
                                NULL,
                                interactive,
                                UID_INVALID,
                                &polkit_registry,
                                error);
                if (r < 0)
                        return r;
                if (r == 0)
                        return 1; /* No authorization for now, but the async polkit stuff will call us again when it has it */

                if (free_and_strdup(&c->vc_keymap, keymap) < 0 ||
                    free_and_strdup(&c->vc_keymap_toggle, keymap_toggle) < 0)
                        return -ENOMEM;

                r = vconsole_write_data(c);
                if (r < 0) {
                        log_error_errno(r, "Failed to set virtual console keymap: %m");
                        return sd_bus_error_set_errnof(error, r, "Failed to set virtual console keymap: %m");
                }

                log_info("Changed virtual console keymap to '%s' toggle '%s'",
                         strempty(c->vc_keymap), strempty(c->vc_keymap_toggle));

                r = vconsole_reload(sd_bus_message_get_bus(m));
                if (r < 0)
                        log_error_errno(r, "Failed to request keymap reload: %m");

                (void) sd_bus_emit_properties_changed(
                                sd_bus_message_get_bus(m),
                                "/org/freedesktop/locale1",
                                "org.freedesktop.locale1",
                                "VConsoleKeymap", "VConsoleKeymapToggle", NULL);

                if (convert) {
                        r = vconsole_convert_to_x11_and_emit(c, sd_bus_message_get_bus(m));
                        if (r < 0)
                                log_error_errno(r, "Failed to convert keymap data: %m");
                }
        }

        return sd_bus_reply_method_return(m, NULL);
}

#if HAVE_XKBCOMMON

_printf_(3, 0)
static void log_xkb(struct xkb_context *ctx, enum xkb_log_level lvl, const char *format, va_list args) {
        const char *fmt;

        fmt = strjoina("libxkbcommon: ", format);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        log_internalv(LOG_DEBUG, 0, __FILE__, __LINE__, __func__, fmt, args);
#pragma GCC diagnostic pop
}

#define LOAD_SYMBOL(symbol, dl, name)                                   \
        ({                                                              \
                (symbol) = (typeof(symbol)) dlvsym((dl), (name), "V_0.5.0"); \
                (symbol) ? 0 : -EOPNOTSUPP;                             \
        })

static int verify_xkb_rmlvo(const char *model, const char *layout, const char *variant, const char *options) {

        /* We dlopen() the library in order to make the dependency soft. The library (and what it pulls in) is huge
         * after all, hence let's support XKB maps when the library is around, and refuse otherwise. The function
         * pointers to the shared library are below: */

        struct xkb_context* (*symbol_xkb_context_new)(enum xkb_context_flags flags) = NULL;
        void (*symbol_xkb_context_unref)(struct xkb_context *context) = NULL;
        void (*symbol_xkb_context_set_log_fn)(struct xkb_context *context, void (*log_fn)(struct xkb_context *context, enum xkb_log_level level, const char *format, va_list args)) = NULL;
        struct xkb_keymap* (*symbol_xkb_keymap_new_from_names)(struct xkb_context *context, const struct xkb_rule_names *names, enum xkb_keymap_compile_flags flags) = NULL;
        void (*symbol_xkb_keymap_unref)(struct xkb_keymap *keymap) = NULL;

        const struct xkb_rule_names rmlvo = {
                .model          = model,
                .layout         = layout,
                .variant        = variant,
                .options        = options,
        };
        struct xkb_context *ctx = NULL;
        struct xkb_keymap *km = NULL;
        void *dl;
        int r;

        /* Compile keymap from RMLVO information to check out its validity */

        dl = dlopen("libxkbcommon.so.0", RTLD_LAZY);
        if (!dl)
                return -EOPNOTSUPP;

        r = LOAD_SYMBOL(symbol_xkb_context_new, dl, "xkb_context_new");
        if (r < 0)
                goto finish;

        r = LOAD_SYMBOL(symbol_xkb_context_unref, dl, "xkb_context_unref");
        if (r < 0)
                goto finish;

        r = LOAD_SYMBOL(symbol_xkb_context_set_log_fn, dl, "xkb_context_set_log_fn");
        if (r < 0)
                goto finish;

        r = LOAD_SYMBOL(symbol_xkb_keymap_new_from_names, dl, "xkb_keymap_new_from_names");
        if (r < 0)
                goto finish;

        r = LOAD_SYMBOL(symbol_xkb_keymap_unref, dl, "xkb_keymap_unref");
        if (r < 0)
                goto finish;

        ctx = symbol_xkb_context_new(XKB_CONTEXT_NO_ENVIRONMENT_NAMES);
        if (!ctx) {
                r = -ENOMEM;
                goto finish;
        }

        symbol_xkb_context_set_log_fn(ctx, log_xkb);

        km = symbol_xkb_keymap_new_from_names(ctx, &rmlvo, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!km) {
                r = -EINVAL;
                goto finish;
        }

        r = 0;

finish:
        if (symbol_xkb_keymap_unref && km)
                symbol_xkb_keymap_unref(km);

        if (symbol_xkb_context_unref && ctx)
                symbol_xkb_context_unref(ctx);

        (void) dlclose(dl);
        return r;
}

#else

static int verify_xkb_rmlvo(const char *model, const char *layout, const char *variant, const char *options) {
        return 0;
}

#endif

static int method_set_x11_keyboard(sd_bus_message *m, void *userdata, sd_bus_error *error) {
        Context *c = userdata;
        const char *layout, *model, *variant, *options;
        int convert, interactive;
        int r;

        assert(m);
        assert(c);

        r = sd_bus_message_read(m, "ssssbb", &layout, &model, &variant, &options, &convert, &interactive);
        if (r < 0)
                return r;

        layout = empty_to_null(layout);
        model = empty_to_null(model);
        variant = empty_to_null(variant);
        options = empty_to_null(options);

        if (!streq_ptr(layout, c->x11_layout) ||
            !streq_ptr(model, c->x11_model) ||
            !streq_ptr(variant, c->x11_variant) ||
            !streq_ptr(options, c->x11_options)) {

                if ((layout && !string_is_safe(layout)) ||
                    (model && !string_is_safe(model)) ||
                    (variant && !string_is_safe(variant)) ||
                    (options && !string_is_safe(options)))
                        return sd_bus_error_setf(error, SD_BUS_ERROR_INVALID_ARGS, "Received invalid keyboard data");

                r = bus_verify_polkit_async(
                                m,
                                CAP_SYS_ADMIN,
                                "org.freedesktop.locale1.set-keyboard",
                                NULL,
                                interactive,
                                UID_INVALID,
                                &polkit_registry,
                                error);
                if (r < 0)
                        return r;
                if (r == 0)
                        return 1; /* No authorization for now, but the async polkit stuff will call us again when it has it */

                r = verify_xkb_rmlvo(model, layout, variant, options);
                if (r < 0) {
                        log_error_errno(r, "Cannot compile XKB keymap for new x11 keyboard layout ('%s' / '%s' / '%s' / '%s'): %m",
                                        strempty(model), strempty(layout), strempty(variant), strempty(options));

                        if (r == -EOPNOTSUPP)
                                return sd_bus_error_setf(error, SD_BUS_ERROR_NOT_SUPPORTED, "Local keyboard configuration not supported on this system.");

                        return sd_bus_error_set(error, SD_BUS_ERROR_INVALID_ARGS, "Specified keymap cannot be compiled, refusing as invalid.");
                }

                if (free_and_strdup(&c->x11_layout, layout) < 0 ||
                    free_and_strdup(&c->x11_model, model) < 0 ||
                    free_and_strdup(&c->x11_variant, variant) < 0 ||
                    free_and_strdup(&c->x11_options, options) < 0)
                        return -ENOMEM;

                r = x11_write_data(c);
                if (r < 0) {
                        log_error_errno(r, "Failed to set X11 keyboard layout: %m");
                        return sd_bus_error_set_errnof(error, r, "Failed to set X11 keyboard layout: %m");
                }

                log_info("Changed X11 keyboard layout to '%s' model '%s' variant '%s' options '%s'",
                         strempty(c->x11_layout),
                         strempty(c->x11_model),
                         strempty(c->x11_variant),
                         strempty(c->x11_options));

                (void) sd_bus_emit_properties_changed(
                                sd_bus_message_get_bus(m),
                                "/org/freedesktop/locale1",
                                "org.freedesktop.locale1",
                                "X11Layout", "X11Model", "X11Variant", "X11Options", NULL);

                if (convert) {
                        r = x11_convert_to_vconsole_and_emit(c, sd_bus_message_get_bus(m));
                        if (r < 0)
                                log_error_errno(r, "Failed to convert keymap data: %m");
                }
        }

        return sd_bus_reply_method_return(m, NULL);
}

static const sd_bus_vtable locale_vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_PROPERTY("Locale", "as", property_get_locale, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("X11Layout", "s", NULL, offsetof(Context, x11_layout), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("X11Model", "s", NULL, offsetof(Context, x11_model), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("X11Variant", "s", NULL, offsetof(Context, x11_variant), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("X11Options", "s", NULL, offsetof(Context, x11_options), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("VConsoleKeymap", "s", NULL, offsetof(Context, vc_keymap), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("VConsoleKeymapToggle", "s", NULL, offsetof(Context, vc_keymap_toggle), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_METHOD("SetLocale", "asb", NULL, method_set_locale, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("SetVConsoleKeyboard", "ssbb", NULL, method_set_vc_keyboard, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("SetX11Keyboard", "ssssbb", NULL, method_set_x11_keyboard, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END
};

static int connect_bus(Context *c, sd_event *event, sd_bus **_bus) {
        _cleanup_(sd_bus_flush_close_unrefp) sd_bus *bus = NULL;
        int r;

        assert(c);
        assert(event);
        assert(_bus);

        r = sd_bus_default_system(&bus);
        if (r < 0)
                return log_error_errno(r, "Failed to get system bus connection: %m");

        r = sd_bus_add_object_vtable(bus, NULL, "/org/freedesktop/locale1", "org.freedesktop.locale1", locale_vtable, c);
        if (r < 0)
                return log_error_errno(r, "Failed to register object: %m");

        r = sd_bus_request_name_async(bus, NULL, "org.freedesktop.locale1", 0, NULL, NULL);
        if (r < 0)
                return log_error_errno(r, "Failed to request name: %m");

        r = sd_bus_attach_event(bus, event, 0);
        if (r < 0)
                return log_error_errno(r, "Failed to attach bus to event loop: %m");

        *_bus = bus;
        bus = NULL;

        return 0;
}

int main(int argc, char *argv[]) {
        _cleanup_(context_free) Context context = {};
        _cleanup_(sd_event_unrefp) sd_event *event = NULL;
        _cleanup_(sd_bus_flush_close_unrefp) sd_bus *bus = NULL;
        int r;

        log_set_target(LOG_TARGET_AUTO);
        log_parse_environment();
        log_open();

        umask(0022);
        mac_selinux_init();

        if (argc != 1) {
                log_error("This program takes no arguments.");
                r = -EINVAL;
                goto finish;
        }

        r = sd_event_default(&event);
        if (r < 0) {
                log_error_errno(r, "Failed to allocate event loop: %m");
                goto finish;
        }

        sd_event_set_watchdog(event, true);

        r = connect_bus(&context, event, &bus);
        if (r < 0)
                goto finish;

        r = context_read_data(&context);
        if (r < 0) {
                log_error_errno(r, "Failed to read locale data: %m");
                goto finish;
        }

        r = bus_event_loop_with_idle(event, bus, "org.freedesktop.locale1", DEFAULT_EXIT_USEC, NULL, NULL);
        if (r < 0)
                log_error_errno(r, "Failed to run event loop: %m");

finish:
        bus_verify_polkit_async_registry_free(polkit_registry);

        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
