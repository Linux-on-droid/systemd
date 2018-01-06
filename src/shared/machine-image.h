/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

/***
  This file is part of systemd.

  Copyright 2014 Lennart Poettering

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

#include <stdbool.h>
#include <stdint.h>

#include "hashmap.h"
#include "lockfile-util.h"
#include "macro.h"
#include "path-util.h"
#include "string-util.h"
#include "time-util.h"

typedef enum ImageType {
        IMAGE_DIRECTORY,
        IMAGE_SUBVOLUME,
        IMAGE_RAW,
        IMAGE_BLOCK,
        _IMAGE_TYPE_MAX,
        _IMAGE_TYPE_INVALID = -1
} ImageType;

typedef struct Image {
        ImageType type;
        char *name;
        char *path;
        bool read_only;

        usec_t crtime;
        usec_t mtime;

        uint64_t usage;
        uint64_t usage_exclusive;
        uint64_t limit;
        uint64_t limit_exclusive;

        char *hostname;
        sd_id128_t machine_id;
        char **machine_info;
        char **os_release;

        bool metadata_valid;

        void *userdata;
} Image;

Image *image_unref(Image *i);
static inline Hashmap* image_hashmap_free(Hashmap *map) {
        return hashmap_free_with_destructor(map, image_unref);
}

DEFINE_TRIVIAL_CLEANUP_FUNC(Image*, image_unref);
DEFINE_TRIVIAL_CLEANUP_FUNC(Hashmap*, image_hashmap_free);

int image_find(const char *name, Image **ret);
int image_discover(Hashmap *map);

int image_remove(Image *i);
int image_rename(Image *i, const char *new_name);
int image_clone(Image *i, const char *new_name, bool read_only);
int image_read_only(Image *i, bool b);

const char* image_type_to_string(ImageType t) _const_;
ImageType image_type_from_string(const char *s) _pure_;

bool image_name_is_valid(const char *s) _pure_;

int image_path_lock(const char *path, int operation, LockFile *global, LockFile *local);
int image_name_lock(const char *name, int operation, LockFile *ret);

int image_set_limit(Image *i, uint64_t referenced_max);

int image_read_metadata(Image *i);

static inline bool IMAGE_IS_HIDDEN(const struct Image *i) {
        assert(i);

        return i->name && i->name[0] == '.';
}

static inline bool IMAGE_IS_VENDOR(const struct Image *i) {
        assert(i);

        return i->path && path_startswith(i->path, "/usr");
}

static inline bool IMAGE_IS_HOST(const struct Image *i) {
        assert(i);

        if (i->name && streq(i->name, ".host"))
                return true;

        if (i->path && path_equal(i->path, "/"))
                return true;

        return false;
}
