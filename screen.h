/* Copyright (C) 2017 Michael R. Tirado <mtirado418@gmail.com> -- GPLv3+
 *
 * This program is libre software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. You should have
 * received a copy of the GNU General Public License version 3
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * TODO currently only does 1 sprite per screen which is not very sprite-like :(
 * need to add the good stuff like sprite offsets, tiling, splits, resizing (at least
 * growing), all the basic window manager type features.
 *
 */
#ifndef SCREEN_H__
#define SCREEN_H__

#include "spr16.h"

struct screen {
	struct screen *next;
	struct client *clients; /* head is focused client */
};

int screen_init(struct screen *self);
int screen_add_client(struct screen *self, struct client *cl);
struct client *screen_find_client(struct screen *self, int cl_fd);
struct client *screen_remove_client(struct screen *self, int cl_fd);
int screen_free(struct screen *self);

#endif
