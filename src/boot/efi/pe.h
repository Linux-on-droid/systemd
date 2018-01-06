/* SPDX-License-Identifier: LGPL-2.1+ */
/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * Copyright (C) 2015 Kay Sievers <kay@vrfy.org>
 */

#ifndef __SDBOOT_PEFILE_H
#define __SDBOOT_PEFILE_H

EFI_STATUS pe_memory_locate_sections(CHAR8 *base,
                                     CHAR8 **sections, UINTN *addrs, UINTN *offsets, UINTN *sizes);
EFI_STATUS pe_file_locate_sections(EFI_FILE *dir, CHAR16 *path,
                                   CHAR8 **sections, UINTN *addrs, UINTN *offsets, UINTN *sizes);
#endif
