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
 * FIXME client lookups are slow
 *
 */

#include <memory.h>
#include "screen.h"

int screen_init(struct screen *self)
{
	memset(self, 0, sizeof(struct screen));
	return 0;
}

int screen_add_client(struct screen *self, struct client *cl)
{
	if (self->clients)
		return -1; /* FIXME */
	cl->next = self->clients;
	self->clients = cl;
	return 0;
}

int screen_focus_client()
{
	/* TODO set focused client (for input) at list head */
	return -1;
}

struct client *screen_find_client(struct screen *self, int cl_fd)
{
	struct client *cl;
	cl = self->clients;
	while (cl)
	{
		if (cl->socket == cl_fd)
			return cl;
		cl = cl->next;
	}
	return NULL;
}

struct client *screen_remove_client(struct screen *self, int cl_fd)
{
	struct client **trail, *cl;
	trail = &self->clients;
	cl    =  self->clients;
	while (cl)
	{
		if (cl->socket == cl_fd) {
			*trail = cl->next;
			return cl;
		}
		trail = &cl->next;
		cl = cl->next;
	}
	return NULL;
}
