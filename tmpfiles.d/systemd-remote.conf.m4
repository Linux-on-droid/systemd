#  This file is part of systemd.
#
#  systemd is free software; you can redistribute it and/or modify it
#  under the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation; either version 2.1 of the License, or
#  (at your option) any later version.

# See tmpfiles.d(5) for details
m4_ifdef(`HAVE_LIBCURL',

d /var/lib/systemd/journal-upload 0755 systemd-journal-upload systemd-journal-upload - -
)m4_dnl
m4_ifdef(`HAVE_MICROHTTPD',

z /var/log/journal/remote 2755 systemd-journal-remote systemd-journal-remote - -
z /run/log/journal/remote 2755 systemd-journal-remote systemd-journal-remote - -
)m4_dnl
