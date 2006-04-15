/*
 *
 * Copyright (C) 2000 David Mazieres (dm@uun.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 * Translated to C and simplified by Geert Bosch (bosch@gnat.com)
 */

#include <assert.h>
#include <strings.h>
#include <sys/types.h>

#ifndef RABIN_WINDOW_SIZE
#define RABIN_WINDOW_SIZE 48
#endif
void rabin_reset();
u_int64_t rabin_slide8(u_char c);
